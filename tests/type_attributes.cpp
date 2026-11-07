#include "odh/type_attributes.hpp"
#include "odh/world.hpp"
#include <iostream>
#include <sstream>
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error("malformed type attributes accepted");}
void u32(std::string& s,std::uint32_t v){for(unsigned i=0;i<4;++i)s.push_back(static_cast<char>(v>>(8*i)));}
std::string header(){std::string s(8,'\0');s+="TTAT";s.push_back(0);return s;}
odh::TypeAttributesSnapshot read(const std::string& s,odh::TypeAttributesLimits limits={}){std::istringstream in(s,std::ios::binary);return odh::read_tatt(odh::View(in,s.size()),limits);}
struct Bits {
    std::string bytes;unsigned used=0;
    void bits(std::uint32_t value,unsigned width){for(unsigned i=0;i<width;++i){if(used%8==0)bytes.push_back(0);bytes.back()=static_cast<char>(static_cast<unsigned char>(bytes.back())|(((value>>(width-i-1))&1U)<<(7-used%8)));++used;}}
    void field(std::int32_t value,unsigned width){if(!value){bits(0,1);return;}bits(1,1);bits(3,2);bits(static_cast<std::uint32_t>(value),width);}
};
}
int main()try{
    auto raw=header();u32(raw,2);u32(raw,0xf1234567);u32(raw,3);raw.append("\x00\x80\xff\xff\xff\x7f",6);u32(raw,700);u32(raw,0);
    const auto seed=read(raw);need(seed.by_guid.at(0xf1234567)==std::vector<std::int16_t>({-32768,-1,32767})&&seed.by_guid.at(700).empty(),"normal TATT lost signed words/empty row");
    auto compressed=header();compressed[12]=1;Bits fields;fields.field(2,32);fields.field(static_cast<std::int32_t>(0xf1234567U),32);fields.field(3,32);for(auto value:{-32768,-1,32767})fields.field(value,16);fields.field(700,32);fields.field(0,32);compressed+=fields.bytes;
    need(odh::encode_type_attributes_snapshot(read(compressed))==odh::encode_type_attributes_snapshot(seed),"compressed TATT differs from normal source rows");
    for(std::size_t size=0;size<raw.size();++size)rejects([&]{read(raw.substr(0,size));});
    auto malformed=raw;malformed.push_back(0);rejects([&]{read(malformed);});malformed=raw;malformed[13]=static_cast<char>(255);rejects([&]{read(malformed);});
    odh::TypeAttributesLimits tiny;tiny.words=2;rejects([&]{read(raw,tiny);});
    auto duplicate=header();u32(duplicate,2);u32(duplicate,8);u32(duplicate,1);duplicate.append("\1\0",2);u32(duplicate,8);u32(duplicate,1);duplicate.append("\2\0",2);
    need(read(duplicate).by_guid.at(8)==std::vector<std::int16_t>({2}),"duplicate TATT rows must retain final source row");
    odh::TypeAttributes state(seed);need(!state.changed()&&state.read(9,32767)==0&&state.read(0xf1234567,3)==0&&!state.read(0xf1234567,-1),"source missing/OOB/negative read semantics");
    need(state.write(9,15,88,false)&&!state.changed()&&!state.snapshot().by_guid.count(9),"unavailable resource write must not invent a row");
    need(state.write(9,32,88,true)&&state.snapshot().by_guid.at(9).size()==32&&state.read(9,31)==0&&state.read(9,32)==0,"new row has exactly32 words even for outside write");
    need(state.write(9,15,-123,true)&&state.read(9,15)==-123&&state.write(9,40,99,true)&&state.read(9,40)==0,"shared signed write/index no-op");
    const auto encoded=odh::encode_type_attributes_snapshot(state.snapshot());
    odh::TypeAttributes resumed(odh::decode_type_attributes_snapshot(encoded));need(resumed.read(9,15)==-123,"mutable shared row did not survive codec/reconstruction");
    auto corrupt=encoded;corrupt[0]=2;rejects([&]{resumed.restore(odh::decode_type_attributes_snapshot(corrupt));});need(resumed.read(9,15)==-123,"failed restore changed live state");
    corrupt=encoded;corrupt.push_back(0);rejects([&]{odh::decode_type_attributes_snapshot(corrupt);});
    odh::World world;need(world.create(1,odh::EntityKind::object,{})&&world.define_type(1,100,900)&&world.create(2,odh::EntityKind::object,{})&&world.define_type(2,200),"world fixtures");
    auto resolver=[](std::uint32_t guid)->std::optional<std::uint32_t>{if(guid==900||guid==200)return 9;if(guid==100)return 888;return {};};
    auto memory=odh::type_attribute_memory(world,resumed,resolver,[](auto guid){return guid==9;});
    odh::VmFrame frame{odh::routine_key(4096),0,{1,1,2},{},{}};need(world.bind_context_live(frame.context),"bind frame generations");
    need(memory.read(frame,36,15)==-123&&memory.read(frame,37,15)==-123,"scopes36/37 must alias shared TypeAttrGUID using caller master");
    need(memory.write(frame,36,15,456)&&memory.read(frame,37,15)==456&&!memory.read(frame,32,15)&&!memory.write(frame,32,15,42),"scope pair shared live write and unrelated scope rejection");
    const auto key=odh::routine_key(4096);
    auto bound=world.vm_memory();const auto prior_read=bound.read;const auto prior_write=bound.write;
    bound.read=[&](const auto& f,auto scope,auto index){return scope==36||scope==37?memory.read(f,scope,index):prior_read(f,scope,index);};
    bound.write=[&](const auto& f,auto scope,auto index,auto value){return scope==36||scope==37?memory.write(f,scope,index,value):prior_write(f,scope,index,value);};
    const odh::Instruction assign{2,1,255,{15,0,0x41,1,0,5,36,7}};
    const odh::Instruction observe{2,254,255,{0,0,15,0,0,5,8,37}};
    odh::Vm vm({{key,odh::Bhav{0x8002,{assign,observe},{},odh::Bhav::Metadata{0,4,0,0}}}},bound);vm.start(key,{1,1,2});
    need(vm.run(4)==odh::VmStatus::returned_true&&vm.temps()[0]==321,"actual Expression setter/getter consumers did not share persistent bank");
    std::cout<<"type attributes source, scoped VM, budgets and persistence checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
