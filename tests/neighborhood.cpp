#include "odh/neighborhood.hpp"
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
void u32(std::string& s,std::uint32_t n) { for(int i=0;i<4;++i) s+=static_cast<char>(n>>(i*8)); }
void u16(std::string& s,std::uint16_t n) { s+=static_cast<char>(n); s+=static_cast<char>(n>>8); }
void patch(std::string& s,std::size_t offset,std::uint32_t n) { for(int i=0;i<4;++i) s.at(offset+i)=static_cast<char>(n>>(i*8)); }
std::string header(std::uint32_t count=1,std::uint32_t version=62) {
    std::string s; u32(s,0x12345678);u32(s,version);s+="SRBN";u32(s,count);return s;
}
std::string record(std::int32_t version=10,std::int32_t mode=5,const std::string& name="Al",std::int16_t id=-32768,bool relation=true) {
    std::string s;u32(s,1);u32(s,version);if(version==10)u32(s,0x80000000);
    s+=name;s+='\0';if(name.size()%2==0)s+=static_cast<char>(0xa5);
    u32(s,0x7fffffff);u32(s,mode);
    if(mode>0)for(int i=0;i<(version==4?80:256);++i)u16(s,static_cast<std::uint16_t>(i==0?0x8000:i==1?0x7fff:i));
    u16(s,static_cast<std::uint16_t>(id));u32(s,0xffffffff);u32(s,0x80000000);
    u32(s,relation?2:0);
    if(relation) {
        u32(s,1);u32(s,0x80000000);u32(s,3);u32(s,0x80000000);u32(s,0x7fffffff);u32(s,0x12345678);
        u32(s,1);u32(s,0x7fffffff);u32(s,0);
    }
    return s;
}
odh::Nbrs read(const std::string& s,const odh::NbrsLimits& limits={}) {
    std::istringstream input(s,std::ios::binary);return odh::read_nbrs(odh::View(input,s.size()),limits);
}
std::string encoded(const odh::Nbrs& value, const odh::NbrsLimits& limits={}) {
    const auto bytes=odh::encode_nbrs(value,limits);
    return std::string(bytes.begin(),bytes.end());
}
void prefixes(const std::string& s) {
    for(std::size_t n=0;n<s.size();++n)rejects([&]{read(s.substr(0,n));},"every record prefix must reject");
}
}
int main() {
 try {
    for(auto version:{62U,63U,73U}) {
        const auto n=read(header(0,version)); require(n.version==version&&n.entries.empty()&&n.remaining_bytes==0,"supported container zero entries");
    }
    for(auto version:{0U,4U,61U,64U,72U,74U,0xffffffffU})rejects([&]{read(header(0,version));},"unsupported container");
    auto bad=header(0);bad[8]='x';rejects([&]{read(bad);},"signature");
    for(auto count:{0xffffffffU,0x80000000U,0x7fffffffU})rejects([&]{read(header(count));},"negative or impossible entry count");
    auto placeholders=header(3);u32(placeholders,0);u32(placeholders,0);u32(placeholders,0);
    const auto p=read(placeholders+"tail");require(p.entries.size()==3&&p.remaining_bytes==4,"placeholder extent and tail");
    for(const auto& e:p.entries)require(e.marker==0&&!e.record,"placeholder has no invented record or ID");
    prefixes(placeholders);
    for(auto marker:{2U,0xffffffffU,0x80000000U}) {auto s=header();u32(s,marker);rejects([&]{read(s);},"unsupported marker");}
    for(auto version:{0U,3U,5U,9U,11U,0xffffffffU}) {auto s=header();u32(s,1);u32(s,version);rejects([&]{read(s);},"unsupported record version");}
    for(auto version:{4,10})for(auto mode:{-1,0,1,5,9}) {
        const auto s=header()+record(version,mode);
        const auto n=read(s);const auto& r=*n.entries.at(0).record;
        require(n.reserved==0x12345678&&n.remaining_bytes==0,"raw header and full extent");
        require(r.version==version&&r.name=="Al"&&r.name_padding==0xa5,"version and name byte padding");
        require(r.unknown3.has_value()==(version==10),"optional version extension");
        if(r.unknown3)require(*r.unknown3==std::numeric_limits<std::int32_t>::min(),"raw unknown3 signed");
        require(r.mystery_zero==std::numeric_limits<std::int32_t>::max()&&r.person_mode==mode,"unknown fields and raw mode");
        require(r.person_data.has_value()==(mode>0),"optional person data");
        if(r.person_data) {
            require(r.person_data->size()==static_cast<std::size_t>(version==4?80:256),"complete person data words");
            require(r.person_data->at(0)==-32768&&r.person_data->at(1)==32767,"signed person words");
            require(r.person_data->back()==(version==4?79:255),"person tail retained beyond88");
        }
        require(r.neighbor_id==-32768&&r.guid==0xffffffffU&&r.unknown_neg_one==std::numeric_limits<std::int32_t>::min(),"raw identity and unknown fields");
        require(r.relationships.size()==2&&r.relationships[0].key_count==1&&r.relationships[0].key==std::numeric_limits<std::int32_t>::min()&&r.relationships[1].key==std::numeric_limits<std::int32_t>::max(),"raw relationship keys ordered");
        require(r.relationships[0].values==std::vector<std::int32_t>({std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max(),0x12345678})&&r.relationships[1].values.empty(),"int32 values never narrowed and empty values");
        prefixes(s);
    }
    for(const auto& name:{std::string(),std::string("A"),std::string("Odd"),std::string("\x80\xff",2)}) {
        const auto r=*read(header()+record(4,0,name)).entries[0].record;
        require(r.name==name&&r.name_padding.has_value()==(name.size()%2==0),"raw onebyte names and parity");
    }
    auto ordered=header(4);u32(ordered,0);ordered+=record(4,0,"a",32767);ordered+=record(10,0,"b",-1);u32(ordered,0);
    const auto o=read(ordered);require(!o.entries[0].record&&o.entries[1].record->neighbor_id==32767&&o.entries[2].record->neighbor_id==-1&&!o.entries[3].record,"serialized order retained");
    auto base=header()+record(4,0,"a");
    // Header16 + marker/version8 + name2 + mystery/mode8 + ID/GUID/unknown10.
    constexpr std::size_t rel_count=44,key_count=48,key=52,value_count=56,second_key=76;
    for(auto n:{0xffffffffU,0x80000000U,0x7fffffffU}) {
        auto s=base;patch(s,rel_count,n);rejects([&]{read(s);},"relationship negative/impossible count");
        s=base;patch(s,value_count,n);rejects([&]{read(s);},"value negative/impossible count");
    }
    for(auto n:{0U,2U,0xffffffffU}) {auto s=base;patch(s,key_count,n);rejects([&]{read(s);},"unknown key layout rejects");}
    {auto s=base;patch(s,second_key,0x80000000);rejects([&]{read(s);},"duplicate keys ambiguous");}
    (void)key;
    const auto exact=header()+record(4,1,"Al");
    odh::NbrsLimits limits;limits.payload_bytes=exact.size();limits.decoded_bytes=2+160+12;limits.entries=1;limits.name_bytes=2;limits.total_name_bytes=2;limits.relationships=2;limits.relationship_values=3;
    require(read(exact,limits).remaining_bytes==0,"all exact limits accepted");
    for(int which=0;which<7;++which) {
        auto l=limits;switch(which){case 0:--l.payload_bytes;break;case 1:--l.decoded_bytes;break;case 2:--l.entries;break;case 3:--l.name_bytes;break;case 4:--l.total_name_bytes;break;case 5:--l.relationships;break;case 6:--l.relationship_values;break;}
        rejects([&]{read(exact,l);},"one below each limit rejects");
    }
    auto twice=header(2)+record(4,1,"Al")+record(4,1,"Al",3);
    odh::NbrsLimits cumulative; cumulative.total_name_bytes=3;rejects([&]{read(twice,cumulative);},"cumulative text limit");
    cumulative={};cumulative.decoded_bytes=347;rejects([&]{read(twice,cumulative);},"cumulative decoded bytes limit");
    cumulative={};cumulative.relationships=3;rejects([&]{read(twice,cumulative);},"cumulative relations limit");
    cumulative={};cumulative.relationship_values=5;rejects([&]{read(twice,cumulative);},"cumulative values limit");
    odh::NbrsLimits zero;zero.entries=0;zero.name_bytes=0;zero.total_name_bytes=0;zero.decoded_bytes=0;zero.relationships=0;zero.relationship_values=0;
    require(read(header(0),zero).entries.empty(),"zero budgets accept no records");
    zero.entries=1;require(read(header()+record(4,0,"",0,false),zero).entries[0].record->name.empty(),"zero text/data/relation budgets accept empty record");
    require(read(exact+"tail").remaining_bytes==4,"unknown tail extent preserved");
    {std::istringstream input("prefix"+exact+"tail");odh::View v(input,6+exact.size()+4);require(odh::read_nbrs(v.subview(6,exact.size())).remaining_bytes==0,"bounded subview");rejects([&]{odh::read_nbrs(v.subview(6,exact.size()-1));},"outside byte unavailable");}
    {std::istringstream input(exact.substr(0,exact.size()-1));odh::View v(input,exact.size());rejects([&]{odh::read_nbrs(v);},"physical stream truncation");}
    {odh::Nbrs created{};created.version=73;odh::NeighborRecord sim{};sim.version=10;sim.unknown3=9;sim.name="Ada";sim.person_mode=9;sim.person_data=std::vector<std::int16_t>(256);sim.person_data->at(58)=27;sim.person_data->at(61)=1;sim.neighbor_id=1;sim.guid=0x12345678;sim.unknown_neg_one=-1;created.entries.push_back({0,std::nullopt});created.entries.push_back({1,sim});
     const auto result=read(encoded(created));require(result.entries.size()==2&&!result.entries[0].record&&result.entries[1].record->name=="Ada"&&result.entries[1].record->person_data->at(58)==27&&result.entries[1].record->person_data->at(61)==1&&result.entries[1].record->guid==sim.guid,"new synthetic neighbor and placeholder encode structurally");}
    require(encoded(read(ordered))==ordered,"mixed serialized placeholder order roundtrips exactly");
    require(encoded(read(placeholders))==placeholders,"all placeholders roundtrip");
    for(auto version:{62U,63U,73U})require(encoded(read(header(0,version)))==header(0,version),"empty supported containers roundtrip");
    for(auto version:{4,10})for(auto mode:{-1,0,1,5,9})for(const auto& name:{std::string(),std::string("Odd"),std::string("\x80\xff",2)}){
        const auto bytes=header()+record(version,mode,name);require(encoded(read(bytes))==bytes,"NBRS all profiles and raw names byte-roundtrip");
    }
    {auto n=read(exact+"tail");rejects([&]{encoded(n);},"NBRS unpreserved tail rejects");}
    {auto n=read(exact);n.version=64;rejects([&]{encoded(n);},"NBRS unsupported encoding version");}
    {auto n=read(exact);n.entries[0].marker=0;rejects([&]{encoded(n);},"placeholder cannot discard record");n.entries[0].marker=2;rejects([&]{encoded(n);},"unsupported encoding marker");n.entries[0].marker=1;n.entries[0].record.reset();rejects([&]{encoded(n);},"active marker requires record");}
    for(int field=0;field<10;++field){
        auto n=read(exact);auto& r=*n.entries[0].record;
        switch(field){
        case 0:r.version=5;break;case 1:r.unknown3=9;break;
        case 2:r.name.push_back('\0');break;case 3:r.name_padding.reset();break;
        case 4:r.person_data.reset();break;case 5:r.person_data->pop_back();break;
        case 6:r.person_mode=0;break;case 7:r.relationships[0].key_count=2;break;
        case 8:r.relationships[1].key=r.relationships[0].key;break;
        case 9:r.name="Odd";break;
        }
        rejects([&]{encoded(n);},"inconsistent encoding field rejects without discarding data");
    }
    {auto n=read(header()+record());n.entries[0].record->unknown3.reset();rejects([&]{encoded(n);},"v10 requires preserved extension");}
    require(encoded(read(exact),limits)==exact,"encoder accepts exact resource budgets");
    for(int which=0;which<7;++which){
        auto l=limits;switch(which){case 0:--l.payload_bytes;break;case 1:--l.decoded_bytes;break;case 2:--l.entries;break;case 3:--l.name_bytes;break;case 4:--l.total_name_bytes;break;case 5:--l.relationships;break;case 6:--l.relationship_values;break;}
        rejects([&]{encoded(read(exact),l);},"encoder one below every resource budget rejects");
    }
    {auto n=read(twice);auto l=odh::NbrsLimits{};l.total_name_bytes=3;rejects([&]{encoded(n,l);},"encoder cumulative names budget");l={};l.decoded_bytes=347;rejects([&]{encoded(n,l);},"encoder cumulative decoded budget");l={};l.relationships=3;rejects([&]{encoded(n,l);},"encoder cumulative relationships budget");l={};l.relationship_values=5;rejects([&]{encoded(n,l);},"encoder cumulative values budget");}
    std::cout<<checks<<" neighborhood checks passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
