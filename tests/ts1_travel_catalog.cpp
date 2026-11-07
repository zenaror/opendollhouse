#include "ts1_travel_catalog.hpp"
#include "odh/ts1_travel.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
using Bytes=std::vector<std::uint8_t>;
void be(Bytes& out,std::size_t at,std::uint32_t value,unsigned n){for(unsigned i=0;i<n;++i)out.at(at+i)=static_cast<std::uint8_t>(value>>((n-i-1)*8));}
Bytes strings(const std::vector<std::pair<std::uint8_t,std::string>>& entries){
    Bytes out{253,255,static_cast<std::uint8_t>(entries.size()),static_cast<std::uint8_t>(entries.size()>>8)};
    for(const auto& [language,text]:entries){out.push_back(language);out.insert(out.end(),text.begin(),text.end());out.push_back(0);out.push_back(0);}return out;
}
Bytes iff(const std::vector<std::pair<std::uint16_t,Bytes>>& chunks){
    const std::string signature="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    Bytes out(64);std::copy(signature.begin(),signature.end(),out.begin());
    for(const auto& [id,data]:chunks){const auto at=out.size();out.resize(at+76);out[at]='S';out[at+1]='T';out[at+2]='R';out[at+3]='#';
        be(out,at+4,static_cast<std::uint32_t>(76+data.size()),4);be(out,at+8,id,2);out.insert(out.end(),data.begin(),data.end());}
    return out;
}
void write(const std::filesystem::path& path,const Bytes& data){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!file)throw std::runtime_error("fixture write failed");}
void check_policy(const std::filesystem::path& root,const std::filesystem::path& user) {
    const auto locations=std::vector<std::pair<std::uint16_t,Bytes>>{
        {2,strings({{1,"23,1,1"},{1,"25,2,2"},{1,"26,3,3"}})},
        {3,strings({{1,"40,2,2"}})}
    };
    write(user/"LotLocations.iff",iff(locations));
    for(const auto house:{23,25,40})write(user/"Houses"/("House"+std::to_string(house)+".iff"),iff({}));

    for(const auto type:{5,7,8,11,13}){
        require(odh_host::ts1_travel_selector_allowed(false,type)==(type==5),"normal selector admits only Downtown");
        require(odh_host::ts1_travel_selector_allowed(true,type),"development selector admits expansion maps");
    }
    require(!odh_host::ts1_travel_selector_allowed(true,6),"unknown selector rejected in development");

    // Policy checks do not create or authenticate a household. The launcher
    // separately matches the real home, occupancy lease and resource manifests.
    odh::Ts1TravelState home;
    home.home_house=home.current_house=5;
    auto downtown=home;
    downtown.current_mode=2;downtown.current_house=25;
    auto vacation=home;
    vacation.current_mode=3;vacation.current_house=40;
    auto wrong_map=vacation;wrong_map.current_mode=2;
    auto wrong_home_mode=home;wrong_home_mode.current_mode=2;
    auto resolve=[&](const auto& state,int destination,bool all=false){
        return odh_host::resolve_ts1_travel_destination(root,user,state,destination,14,all);
    };
    const auto expect_reject=[](const auto& action,const char* message){
        try{action();}catch(const odh_host::Ts1TravelPolicyDenied&){throw std::runtime_error(message);}
        catch(const std::exception&){return;}
        throw std::runtime_error(message);
    };

    const auto expect_policy_denial=[](const auto& action,const char* message){
        try{action();}catch(const odh_host::Ts1TravelPolicyDenied&){return;}
        throw std::runtime_error(message);
    };

    odh_host::validate_ts1_travel_source(root,user,downtown,14,false);
    for(const auto target:{23,25})require(resolve(home,target)==2,"resource-backed Downtown departure");
    for(const auto target:{-1,5})require(resolve(downtown,target)==1,"return to authenticated home");
    require(resolve(home,40,true)==3&&resolve(vacation,25,true)==2,"development allows other modes and onward");
    expect_policy_denial([&]{resolve(home,40);},"unselected expansion direct request admitted");
    expect_policy_denial([&]{resolve(downtown,23);},"onward normal request admitted");
    expect_reject([&]{resolve(home,-1);},"return from permanent home admitted");
    expect_reject([&]{resolve(downtown,25);},"duplicate current lot admitted");
    expect_reject([&]{resolve(home,26);},"missing destination file admitted");
    expect_reject([&]{resolve(downtown,26);},"missing onward destination treated as ordinary denial");
    expect_reject([&]{resolve(home,27);},"unlisted destination admitted");
    expect_reject([&]{resolve(vacation,-1);},"foreign visit admitted by return request");
    expect_reject([&]{odh_host::validate_ts1_travel_source(root,user,vacation,14,false);},
                  "pendingless foreign visit admitted");
    expect_reject([&]{odh_host::validate_ts1_travel_source(root,user,wrong_map,14,false);},
                  "spoofed Downtown source admitted");
    expect_reject([&]{resolve(wrong_home_mode,25);},"changed permanent home mode admitted");

    auto ambiguous=locations;
    ambiguous.emplace_back(5,strings({{1,"25,4,4"}}));
    write(user/"LotLocations.iff",iff(ambiguous));
    std::string reason;
    const auto offered=odh_host::load_ts1_travel_selector_choices(root,user,5,14,reason);
    require(offered&&offered->size()==1&&offered->front().value==23,"offer only unambiguous installed lots");
    const auto raw=odh_host::load_ts1_travel_choices(root,user,5,14,reason);
    require(raw&&raw->size()==2,"raw map still lists both installed resources");
    expect_reject([&]{resolve(home,25);},"ambiguous destination admitted");
    expect_reject([&]{resolve(downtown,-1);},"ambiguous returning source admitted");
}
void run(){
    const auto root=std::filesystem::temp_directory_path()/("odh-travel-catalog-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"UserData"/"Houses");const auto user=root/"UserData";
    const auto cleanup=[&]{std::error_code ignored;std::filesystem::remove_all(root,ignored);};
    try{
        std::vector<std::pair<std::uint16_t,Bytes>> modes;
        for(const auto mode:{2,3,4,5,7})modes.push_back({static_cast<std::uint16_t>(mode),strings({{1,"21,100,100"},{1,"22,200,200"},{1,"99,0,0"}})});
        write(user/"LotLocations.iff",iff(modes));write(user/"Houses"/"House21.iff",iff({}));
        write(user/"Houses"/"NeighborhoodDesc.iff",iff({{2021,strings({{1,std::string("Caf\xe9",4)},{14,"Nome brasileiro"}})}}));
        std::string reason;
        for(const auto type:{5,7,8,11,13}){
            const auto found=odh_host::load_ts1_travel_choices(root,user,static_cast<std::uint8_t>(type),14,reason);
            require(found&&found->size()==1&&found->front().value==21&&found->front().label=="Nome brasileiro"&&reason.empty(),"actual map mode and installed house select localized lot");
        }
        auto found=odh_host::load_ts1_travel_choices(root,user,5,15,reason);
        require(found&&found->front().label=="Caf\xc3\xa9","English fallback is decoded with entry language rather than requested Japanese");
        write(user/"Houses"/"House22.iff",iff({}));found=odh_host::load_ts1_travel_choices(root,user,5,14,reason);
        require(found&&found->size()==2&&found->at(1).label=="Lote 22","missing description uses meaningful localized lot number");
        std::filesystem::remove(user/"Houses"/"House21.iff");found=odh_host::load_ts1_travel_choices(root,user,5,1,reason);
        require(found&&found->size()==1&&found->front().value==22,"stale destination file is removed on reload");
        require(!odh_host::load_ts1_travel_choices(root,user,6,1,reason)&&!reason.empty(),"other selectors explicitly unsupported");
        require(!odh_host::load_ts1_travel_choices(root,root.parent_path(),5,1,reason),"user directory cannot escape installation");
        require(!odh_host::load_ts1_travel_choices(root,std::filesystem::path(std::string(4097,'x')),5,1,reason),"path bound checked before filesystem access");
        std::filesystem::remove(user/"Houses"/"House22.iff");std::filesystem::create_symlink("/etc/passwd",user/"Houses"/"House22.iff");
        require(!odh_host::load_ts1_travel_choices(root,user,5,1,reason),"destination symlink outside installation rejects");
        write(user/"LotLocations.iff",iff({{2,strings({{1,"21,1,1"}})},{2,strings({{1,"22,1,1"}})}}));
        require(!odh_host::load_ts1_travel_choices(root,user,5,1,reason),"ambiguous map mode rejects");
        check_policy(root,user);
        cleanup();
    }catch(...){cleanup();throw;}
}
}
int main(){try{run();std::cout<<"travel catalog resource checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
