#include "odh/object_types.hpp"
#include <iostream>
#include <sstream>

namespace {
int checks=0;
void require(bool yes,const char* text) {++checks;if(!yes)throw std::runtime_error(text);}
template<class F> void rejects(F f,const char* text) {
    bool caught=false;try{f();}catch(const odh::FormatError&){caught=true;}
    require(caught,text);
}
void u16(std::string& s,std::uint16_t n) {s+=static_cast<char>(n);s+=static_cast<char>(n>>8);}
void u32(std::string& s,std::uint32_t n) {for(unsigned i=0;i<4;++i)s+=static_cast<char>(n>>(8*i));}
std::string header(std::uint32_t version=2) {
    std::string s;u32(s,0x12345678);u32(s,version);s+="tjbo";return s;
}
void entry(std::string& s,std::uint32_t guid,const std::string& name,std::uint16_t type,
           std::uint32_t version=2) {
    u32(s,guid);if(!guid)return;
    u16(s,0xffff);u16(s,0x8000);u16(s,138);u16(s,0x7fff);u16(s,type);u16(s,0xabcd);
    s+=name;s+='\0';if(name.size()%2==0)s+=static_cast<char>(0xe7);
    if(version>2&&version<0x80000000U)u32(s,0xdeadbeef);
}
odh::Objt read(const std::string& s,const odh::ObjtLimits& limits={}) {
    std::istringstream input(s,std::ios::binary);return odh::read_objt(odh::View(input,s.size()),limits);
}
void structural() {
    const auto h=header();const auto empty=read(h);
    require(empty.entries.empty()&&empty.remaining_bytes==0,"header-only uncounted table is empty");
    require(empty.reserved==0x12345678&&empty.version==2&&empty.magic==std::array<std::uint8_t,4>{'t','j','b','o'},"raw header retained");
    auto s=h;entry(s,0,"ignored",33);const auto after_zero=s.size();
    entry(s,0xdeadbeef,"",7);const auto after_empty=s.size();
    entry(s,0xffffffffU,"odd",7);const auto after_odd=s.size();
    entry(s,0x80000000U,std::string("a\xff",2),0xffff);const auto after_even=s.size();
    entry(s,0,"ignored",0);
    const auto result=read(s);
    require(result.entries.size()==5&&result.remaining_bytes==0,"GUID0 continues instead of terminating");
    const auto& sentinel=result.entries[0];
    require(sentinel.guid==0&&!sentinel.metadata&&sentinel.name.empty()&&!sentinel.name_padding&&!sentinel.version_extra,"GUID0 has no serialized metadata or name");
    const auto& e=result.entries[1];
    require(e.guid==0xdeadbeef&&e.metadata&&e.name.empty()&&e.name_padding==0xe7&&!e.version_extra,"empty raw name retains padding and version2 has no extension");
    require(e.metadata->unknown1a==65535&&e.metadata->init_tree_version==32768&&e.metadata->unknown2a==138&&e.metadata->main_tree_version==32767&&e.metadata->type_id==7&&e.metadata->objd_type==0xabcd,"raw metadata widths including unknown OBJDtype");
    require(result.entries[2].name=="odd"&&!result.entries[2].name_padding,"odd raw name has no padding");
    require(result.entries[3].name==std::string("a\xff",2)&&result.entries[3].name_padding==0xe7,"high bytes retained without encoding conversion");
    require(result.entries[4].guid==0&&!result.entries[4].metadata,"final sentinel remains a serialized entry");
    require(!result.entry_for_type(0)&&!result.entry_for_type(6)&&!result.entry_for_type(65535),"type0 and out of range reject");
    require(result.entry_for_type(1)==&result.entries[0]&&result.entry_for_type(2)==&result.entries[1]&&result.entry_for_type(3)==&result.entries[2],"one-based indexed resolution including sentinel and duplicate raw IDs");
    require(result.entry_for_type(5)==&result.entries[4]&&!result.entry_for_type(7),"metadata TypeID is not a lookup key");
    for(std::size_t n=0;n<s.size();++n) {
        if(n==h.size()||n==after_zero||n==after_empty||n==after_odd||n==after_even)continue;
        rejects([&]{read(s.substr(0,n));},"every incomplete trailing entry rejects");
    }
    for(unsigned n=1;n<4;++n)rejects([&]{read(h+std::string(n,'\0'));},"partial zeroGUID candidate is not unknown tail");
    auto unusual=h;unusual[8]=static_cast<char>(0xff);unusual[9]='\0';
    require(read(unusual).magic[0]==255&&read(unusual).magic[1]==0,"magic is raw rather than whitelist");
    auto owned=read(s);s.assign(s.size(),'\0');
    require(owned.entries[3].guid==0x80000000U&&owned.entries[3].name==std::string("a\xff",2),"owned result survives source View and stream destruction");
}
void versions() {
    for(const auto version:{0U,1U,2U,3U,73U,0x7fffffffU,0x80000000U,0xffffffffU}) {
        auto s=header(version);entry(s,0,"ignored",0,version);entry(s,17,"even",65535,version);entry(s,18,"odd",0,version);
        const auto result=read(s);const bool extra=version>2&&version<0x80000000U;
        require(result.version==version&&result.entries.size()==3,"raw version retained and matching signed branch traverses all entries");
        require(!result.entries[0].version_extra&&result.entries[1].version_extra.has_value()==extra&&result.entries[2].version_extra.has_value()==extra,"version branch applies only to nonzeroGUID entries");
        if(extra)require(result.entries[1].version_extra==0xdeadbeef&&result.entries[2].version_extra==0xdeadbeef,"unknown version word preserves bits");
    }
    auto s=header(3);entry(s,1,"odd",1,3);
    for(unsigned n=1;n<=4;++n)rejects([&]{read(s.substr(0,s.size()-n));},"missing version extension rejected");
}
void limits() {
    const auto h=header();
    require(read(h,{12,0,0,0}).entries.empty(),"zero budgets permit header-only table");
    rejects([&]{read(h,{11,0,0,0});},"payload budget precedes parsing");
    auto sentinel=h;entry(sentinel,0,"",0);
    require(read(sentinel,{16,1,0,0}).entries.size()==1,"sentinel has no name budget");
    rejects([&]{read(sentinel,{16,0,0,0});},"sentinel counts against entry limit");
    auto s=h;entry(s,1,"abc",900);entry(s,2,"abcd",900);
    require(read(s,{s.size(),2,7,4}).entries.size()==2,"exact payload entry and cumulative names limits");
    rejects([&]{read(s,{s.size()-1,2,7,4});},"payload budget");
    rejects([&]{read(s,{s.size(),1,7,4});},"entry budget");
    rejects([&]{read(s,{s.size(),2,6,4});},"cumulative names budget");
    rejects([&]{read(s,{s.size(),2,7,3});},"individual name budget");
    auto empty=h;entry(empty,1,"",1);
    require(read(empty,{empty.size(),1,0,0}).entries[0].name.empty(),"empty name requires NUL and padding but no text budget");
    rejects([&]{read(empty.substr(0,empty.size()-1));},"missing padding rejects");
    auto unterminated=h;u32(unterminated,1);for(unsigned i=0;i<6;++i)u16(unterminated,1);unterminated+="AAAA";
    rejects([&]{read(unterminated);},"unterminated string rejected");
    rejects([&]{read(unterminated,{unterminated.size(),1,3,4096});},"unterminated string also bounded before allocating");
}
}
int main() {
    try {structural();versions();limits();std::cout<<"object_types: "<<checks<<" checks passed\n";return 0;}
    catch(const std::exception& e) {std::cerr<<"object_types: "<<e.what()<<'\n';return 1;}
}
