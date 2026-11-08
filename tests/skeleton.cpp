#include "odh/skeleton.hpp"
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t value,unsigned width=4){for(unsigned i=0;i<width;++i)b+=static_cast<char>((value>>(i*8))&255);}
void replace(std::string& b,std::size_t at,std::uint32_t value,unsigned width=4){
    for(unsigned i=0;i<width;++i)b.at(at+i)=static_cast<char>((value>>(i*8))&255);}
void text(std::string& b,const std::string& value){put(b,static_cast<std::uint32_t>(value.size()),1);b+=value;}
auto read(const std::string& bytes,odh::SkeletonLimits limits={}){
    std::istringstream stream(bytes);return odh::read_bcf_skeletons(odh::View(stream,bytes.size()),limits);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const odh::FormatError&){failed=true;}
    require(failed,"invalid or truncated skeleton rejected");}
struct Fixture {
    std::string bytes;
    std::size_t bones,groups,pairs,appearance,animation;
};
void transforms(std::string& b){
    for(auto bits:{0x80000000U,0x7FC12345U,0x7F800000U,0x00000001U,0xFF800000U,0x3F800000U,0xBF800000U})put(b,bits);
    for(auto flag:{0xFFFFFFFFU,2U,0x80000000U})put(b,flag);
    put(b,0x7FA12345U);put(b,0x80000000U);
}
Fixture fixture(){Fixture f{};auto& b=f.bytes;put(b,1);text(b,"adult");f.bones=b.size();put(b,1,2);
    text(b,"");text(b,"discarded-parent"); // No other fields belong to an empty-name record.
    text(b,"ROOT");text(b,"NULL");f.groups=b.size();put(b,2);f.pairs=b.size();put(b,2);
    text(b,"key");text(b,"first");text(b,"key");text(b,"second");put(b,0);transforms(b);
    f.appearance=b.size();put(b,0);f.animation=b.size();put(b,0);return f;}
std::string multiple(bool blanks,bool props){std::string b;put(b,2);
    for(int s=0;s<2;++s){text(b,"s");put(b,1,2);
        if(blanks){text(b,"");text(b,"p");}
        text(b,"bone");text(b,"bone");put(b,props?1U:0U);
        if(props){put(b,1);text(b,"");text(b,"");}transforms(b);}
    put(b,0);put(b,0);return b;}
}
int main(){try{
    const auto f=fixture();const auto collection=read(f.bytes);const auto& skeleton=collection.skeletons.at(0);
    const auto& bone=skeleton.bones.at(0);
    require(skeleton.name=="adult" && skeleton.bones.size()==1 && bone.name=="ROOT" && bone.parent=="NULL","names and skipped blank record");
    require(bone.properties.size()==2 && bone.properties[0].size()==2 && bone.properties[1].empty(),"property groups including empty group");
    require(bone.properties[0][0]==std::make_pair(std::string("key"),std::string("first")) &&
            bone.properties[0][1]==std::make_pair(std::string("key"),std::string("second")),"ordered duplicate properties preserved");
    require(bone.translation_bits==std::array<std::uint32_t,3>{0x80000000U,0x7FC12345U,0x7F800000U} &&
            bone.rotation_bits==std::array<std::uint32_t,4>{1U,0xFF800000U,0x3F800000U,0xBF800000U},"raw signed zero, NaN, infinity and subnormal bits preserved");
    require(bone.can_translate==-1 && bone.can_rotate==2 && bone.can_blend==(-2147483647-1) &&
            bone.wiggle_value_bits==0x7FA12345U && bone.wiggle_power_bits==0x80000000U,"raw flags and wiggle bits preserved");
    require(collection.remaining_bytes==0 && read(f.bytes+"tail").remaining_bytes==4,"profile trailers consumed and tail reported");
    for(std::size_t cut=0;cut<f.bytes.size();++cut)rejects([&]{read(f.bytes.substr(0,cut));});
    for(const auto at:{std::size_t{0},f.groups,f.pairs}){auto bad=f.bytes;replace(bad,at,0xFFFFFFFFU);rejects([&]{read(bad);});}
    for(const auto raw:{0x8000U,0xFFFFU}){auto bad=f.bytes;replace(bad,f.bones,raw,2);rejects([&]{read(bad);});}
    for(const auto at:{f.appearance,f.animation})for(const auto count:{1U,0xFFFFFFFFU}){
        auto bad=f.bytes;replace(bad,at,count);rejects([&]{read(bad);});}
    for(const auto at:{std::size_t{0},f.groups,f.pairs}){auto bad=f.bytes;replace(bad,at,0x7FFFFFFFU);rejects([&]{read(bad);});}
    {auto bad=f.bytes;replace(bad,f.bones,0x7FFFU,2);rejects([&]{read(bad);});}
    odh::SkeletonLimits limits;limits.skeletons=0;rejects([&]{read(f.bytes,limits);});
    limits={};limits.bones=0;rejects([&]{read(f.bytes,limits);});
    limits={};limits.skipped_records=0;rejects([&]{read(f.bytes,limits);});
    auto more_blanks=f.bytes;more_blanks.insert(f.bones+2,std::string(2,'\0'));
    limits={};limits.skipped_records=1;rejects([&]{read(more_blanks,limits);});
    limits.skipped_records=2;
    require(read(more_blanks,limits).skeletons[0].bones.size()==1,"multiple skipped records do not consume declared decoded bone count");
    limits={};limits.properties=3;rejects([&]{read(f.bytes,limits);});
    limits={};limits.properties=4;require(read(f.bytes,limits).skeletons[0].bones[0].properties.size()==2,"exact property budget counts groups and pairs");
    const std::size_t text_bytes=5+16+4+4+3+5+3+6;
    limits={};limits.text_bytes=text_bytes;require(read(f.bytes,limits).skeletons.size()==1,"exact cumulative text budget including skipped parent");
    --limits.text_bytes;rejects([&]{read(f.bytes,limits);});
    const auto many=multiple(true,true);
    limits={};limits.bones=1;rejects([&]{read(many,limits);});
    limits={};limits.skipped_records=1;rejects([&]{read(many,limits);});
    limits={};limits.properties=3;rejects([&]{read(many,limits);});
    limits={};limits.bones=2;limits.skipped_records=2;limits.properties=4;
    require(read(many,limits).skeletons.size()==2,"budgets cumulative across skeletons at exact boundary");
    limits={};limits.text_bytes=13;rejects([&]{read(many,limits);});
    require(read(multiple(false,false)).skeletons[0].bones[0].parent=="bone","structural reader accepts self-parent for later pose validation");
    auto mixed=f.bytes.substr(0,f.appearance);
    put(mixed,1);text(mixed,"skin");put(mixed,0);put(mixed,0);put(mixed,1);
    text(mixed,"ROOT");text(mixed,"body");put(mixed,0);put(mixed,0);put(mixed,0);
    require(read(mixed).skeletons[0].bones[0].properties[0][1].second=="second","skeleton reader accepts and validates appearance suffix");
    limits={};limits.text_bytes=text_bytes+12;
    require(read(mixed,limits).remaining_bytes==0,"skeleton text cap covers mixed appearance names");
    --limits.text_bytes;rejects([&]{read(mixed,limits);});
    std::string empty;put(empty,0);put(empty,0);put(empty,0);
    limits={};limits.skeletons=0;limits.bones=0;limits.skipped_records=0;limits.properties=0;limits.text_bytes=0;
    require(read(empty,limits).skeletons.empty(),"empty collection accepted with zero budgets");
    std::string zero;put(zero,1);text(zero,"");put(zero,0,2);put(zero,0);put(zero,0);
    limits.skeletons=1;
    require(read(zero,limits).skeletons[0].bones.empty(),"zero declared bones accepted with zero bone and skipped budgets");
    std::string blank;put(blank,1);text(blank,"");put(blank,0,2);text(blank,"");text(blank,"p");put(blank,0);put(blank,0);
    rejects([&]{read(blank);}); // A zero bone count goes directly to the section trailers.
    std::cout<<checks<<" skeleton resource checks passed (synthetic fixtures only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Skeleton test failed: "<<e.what()<<'\n';return 1;}}
