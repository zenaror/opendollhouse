#include "odh/inventory.hpp"
#include <iostream>
#include <limits>
#include <sstream>

namespace {
int checks=0;
void require(bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    bool rejected=false; try { f(); } catch(const odh::FormatError&) { rejected=true; }
    require(rejected,message);
}
void u32(std::string& s,std::uint32_t n) { for(int i=0;i<4;++i)s+=static_cast<char>(n>>(8*i)); }
void u16(std::string& s,std::uint16_t n) { s+=static_cast<char>(n);s+=static_cast<char>(n>>8); }
void patch(std::string& s,std::size_t offset,std::uint32_t n) { for(int i=0;i<4;++i)s.at(offset+i)=static_cast<char>(n>>(8*i)); }
std::string header() {
    std::string s;u32(s,0x12345678);u32(s,73);s+="HBGN";
    for(unsigned i=0;i<16;++i)u16(s,static_cast<std::uint16_t>(i==0?0x8000:i==1?0x7fff:i==2?0xffff:i));
    return s;
}
void entry(std::string& s,std::int16_t id,const std::vector<odh::InventoryItem>& items,std::int32_t marker=1) {
    u32(s,static_cast<std::uint32_t>(marker));u16(s,static_cast<std::uint16_t>(id));u32(s,static_cast<std::uint32_t>(items.size()));
    for(const auto& item:items) {u32(s,static_cast<std::uint32_t>(item.type));u32(s,item.guid);u16(s,item.count);}
}
odh::Ngbh read(const std::string& s,const odh::NgbhLimits& limits={}) {
    std::istringstream input(s,std::ios::binary);return odh::read_ngbh(odh::View(input,s.size()),limits);
}
std::string populated() {
    auto s=header();u32(s,3);
    entry(s,-32768,{{std::numeric_limits<std::int32_t>::min(),0xffffffffU,65535},{std::numeric_limits<std::int32_t>::max(),0,0}},-99);
    entry(s,32767,{});
    entry(s,-1,{{2,0x12345678,9},{2,0x12345678,8},{-1,0x12345678,7}});
    return s;
}
void structural() {
    const auto h=header();
    auto absent=read(h);require(!absent.inventory_present&&absent.inventory_by_id.empty()&&absent.inventory_markers.empty()&&absent.remaining_bytes==0,"EOF44 is known empty inventory");
    require(absent.reserved==0x12345678&&absent.version==73&&absent.magic==std::array<std::uint8_t,4>{'H','B','G','N'},"raw header retained");
    require(absent.neighborhood_data[0]==-32768&&absent.neighborhood_data[1]==32767&&absent.neighborhood_data[2]==-1&&absent.neighborhood_data[15]==15,"sixteen signed raw neighborhood words");
    auto raw=h;patch(raw,4,0xffffffffU);raw[8]='\0';raw[9]=static_cast<char>(0xff);
    const auto unknown=read(raw);require(unknown.version==0xffffffffU&&unknown.magic[0]==0&&unknown.magic[1]==255,"reference header fields not guessed or validated");
    auto empty=h;u32(empty,0);const auto zero=read(empty+"tail");
    require(zero.inventory_present&&zero.inventory_by_id.empty()&&zero.remaining_bytes==4,"present zero-count extension and tail distinguished");
    const auto s=populated();auto data=read(s+"tail");
    require(data.inventory_by_id.size()==3&&data.remaining_bytes==4,"signed ID maps and declared extent");
    const auto& first=data.inventory_by_id.at(-32768);
    require(first.size()==2&&first[0].type==std::numeric_limits<std::int32_t>::min()&&first[0].guid==0xffffffffU&&first[0].count==65535,"signed type unsigned GUID/count widths");
    require(first[1].type==std::numeric_limits<std::int32_t>::max()&&first[1].guid==0&&first[1].count==0,"zero count retained");
    require(data.inventory_markers.at(-32768)==-99&&data.inventory_by_id.at(32767).empty(),"raw non-one marker and empty list retained");
    const auto& duplicates=data.inventory_by_id.at(-1);
    require(duplicates.size()==3&&duplicates[0].count==9&&duplicates[1].count==8&&duplicates[2].type==-1,"item order duplicate GUID/type preserved");
    data.inventory_by_id.at(-1)[0].count=1;
    require(read(s).inventory_by_id.at(-1)[0].count==9,"owned mutable result independent of input lifetime");
    for(std::size_t size=0;size<s.size();++size) {
        if(size==44)continue;
        rejects([&]{read(s.substr(0,size));},"every malformed prefix rejects; only EOF44 is valid absent extension");
    }
    auto overwritten=h;u32(overwritten,3);entry(overwritten,4,{{1,9,1},{2,8,2}},17);entry(overwritten,-4,{{3,7,3}});entry(overwritten,4,{},-2);
    const auto last=read(overwritten);
    require(last.inventory_by_id.size()==2&&last.inventory_by_id.at(4).empty()&&last.inventory_markers.at(4)==-2,"last duplicate NID replaces entire list and marker including empty");
    require(last.inventory_by_id.begin()->first==-4,"map numeric ordering explicitly distinct from serialized order");
    rejects([&]{read(overwritten,{1024,3,2});},"cumulative item budget counts overwritten lists");
    require(read(overwritten,{1024,3,3}).inventory_by_id.size()==2,"exact cumulative item budget");
    rejects([&]{read(overwritten,{1024,2,3});},"entry budget counts duplicate NIDs");
}
void malformed_and_limits() {
    const auto h=header();
    for(auto count:{0xffffffffU,0x80000000U,0x7fffffffU}) {
        auto s=h;u32(s,count);rejects([&]{read(s);},"negative or impossible inventory entry count");
        s=h;u32(s,1);u32(s,1);u16(s,1);u32(s,count);
        rejects([&]{read(s);},"negative or impossible list count");
    }
    for(unsigned extra=1;extra<4;++extra)rejects([&]{read(h+std::string(extra,'\0'));},"partial extension never silently empty");
    auto empty=h;u32(empty,0);
    require(read(h,{44,0,0}).inventory_by_id.empty(),"absent extension exact header budget no inventory allocation");
    require(read(empty,{48,0,0}).inventory_present,"present empty exact limits");
    rejects([&]{read(h,{43,0,0});},"payload limit enforced before read");
    const auto s=populated();
    rejects([&]{read(s,{s.size()-1,3,5});},"payload budget");
    rejects([&]{read(s,{s.size(),2,5});},"entries budget");
    rejects([&]{read(s,{s.size(),3,4});},"cumulative item budget");
    require(read(s,{s.size(),3,5}).remaining_bytes==0,"exact structural budgets");
    auto no_items=h;u32(no_items,1);entry(no_items,0,{});
    require(read(no_items,{no_items.size(),1,0}).inventory_by_id.at(0).empty(),"empty list does not charge item budget");
}
}
int main() {
    try {structural();malformed_and_limits();std::cout<<"inventory: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<"inventory: "<<e.what()<<'\n';return 1;}
}
