// SPDX-License-Identifier: MPL-2.0
#include "odh/career.hpp"
#include "odh/ts1_variable.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
using namespace odh;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Career read(const std::string& data,const CareerLimits& limits={}){
    std::istringstream input(data,std::ios::binary);return read_carr(View(input,data.size()),limits);
}
template<class F> void fails(F f,const char* why){bool failed=false;try{f();}catch(const FormatError&){failed=true;}check(failed,why);}
void le32(std::string& bytes,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes.push_back(static_cast<char>(value>>(n*8)));}
std::string header(const std::string& name){std::string b;le32(b,17);le32(b,1000);le32(b,0x4f626a4d);b.push_back(1);
    b+=name;b.push_back(0);if(name.size()%2)b.push_back(static_cast<char>(0xa7));return b;}
// Independently authored fixed bytes: count1 = 100000001, followed by21 zero
// fields. The last two bits are discarded before the five string records.
std::string fixed(){auto b=header("Odd");b.append("\x80\x80\x00\x03",4);
    b.append("A\0m\0\0\xe1T\0u\0",10);return b;}
class Encode {
public:
    explicit Encode(std::string prefix):bytes(std::move(prefix)),bit(bytes.size()*8){}
    void bits(std::uint32_t value,unsigned count){for(unsigned n=count;n;--n){
        if(bit/8>=bytes.size())bytes.push_back(0);
        if((value>>(n-1))&1U)bytes[bit/8]=static_cast<char>(static_cast<unsigned char>(bytes[bit/8])|(1U<<(7-bit%8)));
        ++bit;if(bit%8==0)odd=!odd;}}
    void integer(std::int32_t value){if(!value){bits(0,1);return;}bits(1,1);
        const unsigned widths[]{6,11,21,32};unsigned code=0;
        while(code<3&&(value<-(std::int64_t{1}<<(widths[code]-1))||value>=(std::int64_t{1}<<(widths[code]-1))))++code;
        bits(code,2);bits(static_cast<std::uint32_t>(value),widths[code]);}
    void string(const std::string& text,bool next){
        if(!string_mode){if(bit%8==0)odd=!odd;else bit+=8-bit%8;}
        bytes.resize(bit/8);bytes+=text;bytes.push_back(0);
        if((text.size()%2==0)==!odd)bytes.push_back(static_cast<char>(0xc3));
        bit=bytes.size()*8;string_mode=!next;odd=next;
    }
    std::string bytes;std::size_t bit;bool odd=true,string_mode=false;
};
void parser(){
    const auto raw=fixed();const auto career=read(raw);
    check(career.padding==17&&career.version==1000&&career.magic==0x4f626a4d&&career.compression==1&&
        career.name=="Odd"&&career.name_padding==0xa7&&career.levels.size()==1,"fixed source header and odd-name padding");
    const auto& l=career.levels[0];
    check(l.job_name=="A"&&l.male_uniform_mesh=="m"&&l.female_uniform_mesh.empty()&&l.uniform_skin=="T"&&l.unknown=="u"&&
        !l.string_padding[0]&&!l.string_padding[1]&&l.string_padding[2]==0xe1&&!l.string_padding[3]&&!l.string_padding[4]&&
        career.consumed_bits==raw.size()*8&&career.remaining_bits==0,"fixed field/string interruption and consecutive false-string padding");
    const auto tail=read(raw+"\x5a");check(tail.remaining_bits==8&&tail.suffix==std::vector<std::uint8_t>{0x5a},"unknown suffix preserved after string reload");
    for(std::size_t n=0;n<raw.size();++n)fails([&]{read(raw.substr(0,n));},"every truncation of required fixed payload rejected");
    Encode encoded(header(std::string("\xe9Z",2)));encoded.integer(2);
    const std::int32_t numbers[]{INT32_MIN,INT32_MAX,-1048576,1048575,-1024,1023,-32,31,-1,0,1,999,-7,3,11,21,-35,777,9,17,4};
    for(unsigned row=0;row<2;++row){for(const auto n:numbers)encoded.integer(n);
        encoded.string(row?"XY":"ABC",false);encoded.string(row?"":"B$G$B_01",false);
        encoded.string("F$G$B_01",false);encoded.string("$C",false);encoded.string(row?"":"Q",true);}
    const auto multiple=read(encoded.bytes);
    check(multiple.name==std::string("\xe9Z",2)&&!multiple.name_padding&&multiple.levels.size()==2,"raw name bytes and even header alignment");
    for(const auto& level:multiple.levels){for(unsigned n=0;n<10;++n)check(level.minimum_required[n]==numbers[n],"all requirement widths/signs");
        for(unsigned n=0;n<7;++n)check(level.motive_delta[n]==numbers[10+n],"all motive values");
        check(level.salary==777&&level.start_hour==9&&level.end_hour==17&&level.car_type==4&&
            level.female_uniform_mesh=="F$G$B_01"&&level.uniform_skin=="$C","all scalar fields and literal uniform templates");}
    check(multiple.levels[1].male_uniform_mesh.empty()&&multiple.levels[1].job_name=="XY"&&multiple.levels[1].unknown.empty(),
        "male-empty/default-daywear fact is distinct from female mesh and final empty string");
    auto bad=raw;bad[12]=0;fails([&]{read(bad);},"unsupported compression rejected");
    Encode negative(header(""));negative.integer(-1);fails([&]{read(negative.bytes);},"negative level count rejected");
    Encode empty(header(""));empty.integer(0);check(read(empty.bytes).levels.empty(),"known zero-level career is valid");
    CareerLimits limits;limits.levels=0;fails([&]{read(raw,limits);},"level allocation budget");
    limits={};limits.decoded_fields=21;fails([&]{read(raw,limits);},"scalar/string decoded field budget");
    limits={};limits.string_length=2;fails([&]{read(raw,limits);},"name length budget");
    limits={};limits.string_bytes=4;fails([&]{read(raw,limits);},"cumulative strings budget");
    limits={};limits.payload_bytes=raw.size()-1;fails([&]{read(raw,limits);},"payload byte budget");
}
void lookup(){
    const auto c=read(fixed());CareerCatalog catalog({{0,c},{17,c},{65535,c}});
    CareerCatalog authored({{17,c},{3,c},{29,c}});
    check(authored.next(-1)==17&&authored.next(3)==17&&authored.next(17)==29&&authored.next(29)==-1,
        "career enumeration preserves authenticated CARR chunk order rather than sorting IDs");
    check(catalog.next(17)==-1,"career ID65535 source signed conversion is negative termination");
    check(catalog.lookup(17,0).status==CareerLookupStatus::found&&catalog.lookup(17,0).level->male_uniform_mesh=="m"&&
        catalog.lookup(-1,0).status==CareerLookupStatus::found,"exact signed-person-data to ushort ID lookup");
    check(catalog.lookup(0,0).status==CareerLookupStatus::found,"job zero is a real chunk ID, never automatically unemployed");
    check(catalog.lookup(18,0).status==CareerLookupStatus::missing_career&&!catalog.lookup(18,0).level,"missing career is explicit");
    for(const auto n:{-1,1,INT32_MAX})check(catalog.lookup(17,n).status==CareerLookupStatus::invalid_level&&!catalog.lookup(17,n).level,"level never clamped");
    fails([&]{CareerCatalog duplicate({{17,c},{17,c}});},"duplicate career IDs never silently override");
    CareerLimits limits;limits.careers=0;fails([&]{CareerCatalog limited({{17,c}},limits);},"catalog count budget");
    limits={};limits.levels=1;fails([&]{CareerCatalog limited({{17,c},{18,c}},limits);},"catalog cumulative levels budget");
    limits={};limits.string_bytes=3;fails([&]{CareerCatalog limited({{17,c}},limits);},"catalog cumulative text budget");
    check(catalog.lookup(17,0).level->female_uniform_mesh.empty(),"empty female field remains empty, never replaced with male");
}
void job_data(){
    CareerLevel first;
    for(std::size_t n=0;n<10;++n)first.minimum_required[n]=65536+100+static_cast<std::int32_t>(n);
    for(std::size_t n=0;n<7;++n)first.motive_delta[n]=-65537-static_cast<std::int32_t>(n);
    first.salary=0x18001;first.start_hour=7;first.end_hour=23;first.car_type=65535;
    auto second=first;second.start_hour=19;
    Career source;source.levels={first,second};Career empty;
    const CareerCatalog catalog({{0,source},{65535,source},{18,empty}});
    std::array<std::int16_t,23> expected{};expected[0]=2;expected[1]=-32767;
    for(std::size_t n=0;n<10;++n)expected[n+2]=static_cast<std::int16_t>(100+n);
    expected[12]=7;expected[13]=23;
    for(std::size_t n=0;n<7;++n)expected[n+14]=static_cast<std::int16_t>(-1-static_cast<int>(n));
    expected[21]=-1;expected[22]=0;
    for(std::int16_t field=0;field<23;++field)
        check(catalog.job_data(0,0,field)==expected[static_cast<std::size_t>(field)]&&
            catalog.job_data(65535,0,field)==expected[static_cast<std::size_t>(field)],
            "all23 JobData fields map exact source values with defined signed low16 narrowing");
    for(const auto level:{std::int16_t{-32768},std::int16_t{-1},std::int16_t{2},std::int16_t{32767}})
        for(const auto field:{std::int16_t{-1},std::int16_t{0},std::int16_t{12},std::int16_t{22},std::int16_t{32767}})
            check(catalog.job_data(99,level,field)==0&&!catalog.job_data(0,level,field),
                "missing career precedes validation but real career validates level even before count/constant fields");
    for(const auto field:{std::int16_t{-32768},std::int16_t{-1},std::int16_t{23},std::int16_t{32767}})
        check(!catalog.job_data(0,0,field),"invalid JobData field is unsupported, not a successful zero");
    check(!catalog.job_data(18,0,0)&&!catalog.job_data(18,0,22),"existing empty career has no valid level even for constant fields");
    odh::World world;odh::VmMemory memory;unsigned generic_reads=0;
    memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{++generic_reads;return 999;};
    memory.job_data=[&](const auto&,std::uint16_t job,std::int16_t level,std::int16_t field){return catalog.job_data(job,level,field);};
    odh::VmFrame frame;frame.context={11,22,33,44};odh::Ts1Temps query_temps{};query_temps[0]=-1;query_temps[1]=1;
    check(odh::read_ts1_variable(world,memory,frame,query_temps,33,12)==19&&generic_reads==0,
        "non-Expression variable consumer uses its supplied query temps and unsigned career ID");
    query_temps[1]=2;
    check(!odh::read_ts1_variable(world,memory,frame,query_temps,33,12)&&generic_reads==0,
        "unsupported JobData does not fall through a generic memory callback with the wrong register context");
    memory.job_data={};query_temps[1]=0;
    check(!odh::read_ts1_variable(world,memory,frame,query_temps,33,12)&&generic_reads==0,
        "missing job provider remains unavailable rather than zero or generic scope emulation");
}
}
int main(){try{parser();lookup();job_data();std::cout<<"CARR career reader and exact TS1 lookup passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
