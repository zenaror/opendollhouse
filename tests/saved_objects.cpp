#include "odh/saved_objects.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejected(F f,const char* why){try{f();}catch(const odh::FormatError&){require(true,why);return;}require(false,why);}
struct Encoder {
    std::vector<std::uint8_t> bytes;unsigned bit=0;
    void bits(std::uint32_t v,unsigned count){
        for(unsigned n=count;n;--n){if(bit==0)bytes.push_back(0);
            bytes.back()|=static_cast<std::uint8_t>(((v>>(n-1))&1U)<<(7-bit));bit=(bit+1)&7U;}
    }
    void field(std::int32_t v,unsigned width){if(v==0){bits(0,1);return;}bits(1,1);bits(3,2);bits(static_cast<std::uint32_t>(v),width);}
    void word(std::int16_t v){field(v,16);}
};
odh::ObjmBlock block(std::int16_t id,std::int16_t attributes=0){
    Encoder e;for(unsigned i=0;i<7;++i)e.field(i==6?1:0,32);
    e.word(1);e.word(attributes);for(int i=0;i<attributes;++i)e.word(42);
    for(unsigned i=0;i<8;++i)e.word(0);
    for(unsigned i=0;i<68;++i)e.word(i==11?id:0);
    for(unsigned i=0;i<5;++i)e.word(0);
    return {13,std::move(e.bytes)};
}
void run(){
    odh::Objt types{};
    odh::ObjtEntry one{};one.guid=0x11223344;one.metadata=odh::ObjtMetadata{0,0,0,0,99,4};
    odh::ObjtEntry two{};two.guid=0x55667788;two.metadata=odh::ObjtMetadata{0,0,0,0,1,7};
    types.entries={one,two};
    odh::Objm objects{};objects.id_to_objt={{1,1},{65535,2}};
    objects.blocks={block(-1,1),block(1)}; // Order deliberately differs from map order.
    auto rows=odh::read_saved_object_prefixes(objects,types);
    require(rows.size()==2&&rows[0].object_id==65535&&rows[0].type_id==2&&rows[0].guid==two.guid,
        "signed ObjectData ID cast and one-based serialized type mapping");
    require(rows[1].object_id==1&&rows[1].guid==one.guid,"block order retained instead of sorted-map zip");
    require(rows[0].prefix.attributes==std::vector<std::int16_t>{42}&&rows[0].prefix.odd_offset,"prefix and parity preserved");
    objects.blocks[0].bytes.assign(1,0);
    require(rows[0].prefix.object_data[11]==-1&&rows[0].prefix.attributes[0]==42,"result owns independent prefix banks");
    objects.blocks[0]=block(-1,1);
    odh::SavedPrefixLimits limits;
    limits.words=163;require(odh::read_saved_object_prefixes(objects,types,limits).size()==2,"exact cumulative word budget");
    limits.words=162;rejected([&]{odh::read_saved_object_prefixes(objects,types,limits);},"attribute exceeds cumulative budget");
    limits.words=161;rejected([&]{odh::read_saved_object_prefixes(objects,types,limits);},"fixed banks exceed cumulative budget before decode");
    limits={};limits.objects=1;rejected([&]{odh::read_saved_object_prefixes(objects,types,limits);},"object count bound");
    limits={};limits.attributes_per_object=0;rejected([&]{odh::read_saved_object_prefixes(objects,types,limits);},"per-object attribute bound");
    odh::Objm large{};odh::Objt large_types{};large_types.entries.push_back(one);
    constexpr std::uint16_t large_count=847;
    for(std::uint16_t id=1;id<=large_count;++id){large.id_to_objt.emplace(id,1);large.blocks.push_back(block(static_cast<std::int16_t>(id)));}
    require(odh::read_saved_object_prefixes(large,large_types).size()==large_count,
        "default word budget accommodates the largest inspected native lot");
    auto bad=objects;bad.blocks.pop_back();rejected([&]{odh::read_saved_object_prefixes(bad,types);},"block/map count mismatch");
    bad=objects;bad.blocks[0]=block(2);rejected([&]{odh::read_saved_object_prefixes(bad,types);},"instance ID absent from table");
    bad=objects;bad.blocks[0]=block(1);rejected([&]{odh::read_saved_object_prefixes(bad,types);},"duplicate instance ID");
    bad=objects;bad.blocks[0]=block(0);bad.id_to_objt.erase(65535);bad.id_to_objt[0]=2;
    rejected([&]{odh::read_saved_object_prefixes(bad,types);},"ID zero is not an object identity");
    for(auto type:{std::uint16_t{0},std::uint16_t{3},std::uint16_t{65535}}){
        bad=objects;bad.id_to_objt[1]=type;rejected([&]{odh::read_saved_object_prefixes(bad,types);},"missing type rejected");
    }
    auto sentinel=types;sentinel.entries[0]=odh::ObjtEntry{};
    rejected([&]{odh::read_saved_object_prefixes(objects,sentinel);},"GUID-zero sentinel not a resolved definition");
    bad=objects;bad.blocks[1].bytes.clear();rejected([&]{odh::read_saved_object_prefixes(bad,types);},"malformed later prefix cannot return partial snapshot");
    require(rows[0].guid==two.guid&&rows[1].prefix.object_data[11]==1,"previous result survives failed snapshot");
    odh::Objm empty{};limits={};limits.words=0;limits.objects=0;
    require(odh::read_saved_object_prefixes(empty,{},limits).empty(),"loaded empty container valid zero budget");
}
}
int main(){try{run();std::cout<<checks<<" saved object prefix checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
