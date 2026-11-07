#include "character_display_names.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void le16(std::string& out,std::size_t at,std::uint16_t n){out.at(at)=char(n);out.at(at+1)=char(n>>8);}
void le32(std::string& out,std::size_t at,std::uint32_t n){le16(out,at,std::uint16_t(n));le16(out,at+2,std::uint16_t(n>>16));}
void be32(std::string& out,std::uint32_t n){for(int shift:{24,16,8,0})out.push_back(char(n>>shift));}
std::string chunk(const char* type,std::uint16_t id,const std::string& payload){
    std::string out(type,4);be32(out,std::uint32_t(76+payload.size()));
    out.push_back(char(id>>8));out.push_back(char(id));out.append(66,'\0');return out+payload;
}
std::string objd(std::uint32_t guid,std::uint16_t strings){
    std::string out(84,'\0');le32(out,0,138);le16(out,18,2);le32(out,28,guid);le16(out,82,strings);return out;
}
std::string table(std::initializer_list<std::pair<std::uint8_t,std::string>> names){
    std::string out(4,'\0');le16(out,0,0xfffd);le16(out,2,std::uint16_t(names.size()));
    for(const auto& entry:names){out.push_back(char(entry.first));out+=entry.second;out.append(2,'\0');}return out;
}
std::string iff(const std::string& chunks){
    std::string out(64,'\0');const std::string sig="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    out.replace(0,sig.size(),sig);return out+chunks;
}
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream out(path,std::ios::binary);out.write(bytes.data(),std::streamsize(bytes.size()));
    require(bool(out),"write synthetic fixture");
}
void character(const std::filesystem::path& path,std::uint32_t guid,std::uint16_t owner,std::uint16_t ctss,
               const std::string& names){write(path,iff(chunk("OBJD",128,objd(guid,owner))+chunk("CTSS",ctss,names)));}
struct Temp{
    std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-display-names-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){std::filesystem::create_directories(path/"installed");std::filesystem::create_directory(path/"player");}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
}
int main(){try{
    Temp t;const auto installed=t.path/"installed",player=t.path/"player";
    character(installed/"user00011.iff",11,300,300,table({{1,"Cassandra"},{9,"Cassandra Local"}}));
    character(player/"iffname.iff",12,0,2000,table({{1,"Player Name"}}));
    character(installed/"no-private-fallback.iff",13,0,2000,table({{1,"Not admitted"}}));
    character(installed/"missing.iff",14,301,300,table({{1,"Wrong owner"}}));
    character(installed/"empty.iff",15,300,300,table({{1,"English"},{9,""}}));
    odh_host::CharacterDisplayNames names(installed,player,9);
    require(names.name(11)=="Cassandra Local","requested language is the person's CTSS, never filename");
    require(names.name(12)=="Player Name","private CAS CTSS2000 and English fallback");
    require(names.name(13).empty()&&names.name(14).empty()&&names.name(999).empty(),"missing ownership/admission has no asset or ID fallback");
    require(names.name(15).empty(),"present empty localized name is not replaced by English");
    odh_host::CharacterDisplayNames english(installed,player,2);
    require(english.name(11)=="Cassandra","absent language falls back to English");
    character(player/"duplicate.iff",11,300,300,table({{1,"Imposter"}}));names.reset();
    require(names.name(11).empty(),"ambiguous GUID across installed and private data is unavailable");
    std::filesystem::remove(player/"duplicate.iff");
    write(player/"malformed.iff","not IFF");names.reset();
    require(names.name(11).empty(),"malformed catalog fails closed without displaying stale name");
    std::filesystem::remove(player/"malformed.iff");
    write(player/"duplicate-table.iff",iff(chunk("OBJD",128,objd(16,300))+chunk("CTSS",300,table({{1,"A"}}))+chunk("CTSS",300,table({{1,"B"}}))));
    names.reset();require(names.name(16).empty(),"duplicate CTSS never leaks the first candidate");
    std::filesystem::remove(player/"duplicate-table.iff");
    character(player/"broken-table.iff",17,300,300,"bad");names.reset();
    require(names.name(17).empty(),"malformed owned CTSS is unavailable");
    std::filesystem::remove(player/"broken-table.iff");
    names.reset();const auto& held=names.name(11);
    for(std::uint32_t id=1000;id<1511;++id)require(names.name(id).empty(),"unknown names stay empty");
    require(held=="Cassandra Local"&&names.name(12).empty(),"512-entry bound preserves references and declines new names");
    names.reset();require(names.name(12)=="Player Name","reset admits newly needed names after budget exhaustion");
    std::filesystem::create_directory_symlink(installed,t.path/"alias");
    odh_host::CharacterDisplayNames alias(t.path/"alias",{},9);
    require(alias.name(11).empty(),"symlink directory is not an admitted character source");
    std::filesystem::create_symlink(installed/"user00011.iff",player/"link.iff");names.reset();
    require(names.name(12).empty(),"symlink IFF invalidates catalog admission");
    std::filesystem::remove(player/"link.iff");
    odh_host::CharacterDisplayNames normalized(installed/".."/"installed",player,9);
    require(normalized.name(11)=="Cassandra Local","lexical dot components normalize to the canonical directory");
    odh_host::CharacterDisplayNames missing(t.path/"absent",{},9);
    require(missing.name(11).empty(),"missing directories yield unavailable names");
    odh_host::CharacterDisplayNames shared(installed,installed,9);
    require(shared.name(11)=="Cassandra Local","same source twice is indexed once");
    std::cout<<"PASS owned localized character names, admission, malformed paths, cache and reset\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
