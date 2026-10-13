#include "odh/animation.hpp"
#include <iostream>
#include <sstream>
namespace {
int checks=0;
void require(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t n,unsigned width=4){for(unsigned i=0;i<width;++i)b+=static_cast<char>((n>>(i*8))&255);}
void text(std::string& b,const std::string& s){put(b,static_cast<std::uint32_t>(s.size()),1);b+=s;}
template<class F>auto view(const std::string& b,F f){std::istringstream s(b);return f(odh::View(s,b.size()));}
auto bcf(const std::string& b,odh::AnimationLimits l={}){return view(b,[&](const auto& v){return odh::read_bcf_animations(v,l);});}
auto strings(const std::string& b,odh::AnimationLimits l={}){return view(b,[&](const auto& v){return odh::read_strings(v,l);});}
auto cfp(const std::string& b,unsigned count){return view(b,[&](const auto& v){return odh::read_cfp(v,count,count);});}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const odh::FormatError&){failed=true;}require(failed,"invalid/truncated resource rejected");}
std::string fixture(){std::string b;put(b,0);put(b,0);put(b,1);text(b,"clip");text(b,"xskill");
    for(auto n:{0x42C80000U,0U,0U,3U,3U,1U})put(b,n);text(b,"ROOT");
    for(auto n:{3U,0x42C80000U,1U,1U,0U,0U,1U,1U})put(b,n);text(b,"key");text(b,"value");
    put(b,1);put(b,1);put(b,0xFFFFFFFF);put(b,1);text(b,"xevt");text(b,"100");return b;}
}
int main(){try{
    const auto b=fixture();const auto t=bcf(b);const auto& a=t.animations.at(0);const auto& m=a.motions.at(0);
    require(a.name=="clip" && a.xskill=="xskill" && a.translations==3 && a.rotations==3,"BCF animation fields");
    require(m.bone=="ROOT" && m.frames==3 && m.first_rotation==0 && m.duration_bits==0x42C80000,"BCF motion fields");
    require(m.properties[0][0]==std::make_pair(std::string("key"),std::string("value")) &&
            m.time_properties[0][0].id==-1 && m.time_properties[0][0].pairs[0].second=="100","property pairs/events preserved");
    for(std::size_t n=0;n<b.size();++n)rejects([&]{bcf(b.substr(0,n));});
    require(bcf(b+"tail").remaining_bytes==4,"BCF tail reported");
    auto bad=b;bad[0]=1;rejects([&]{bcf(bad);});bad=b;bad[4]=1;rejects([&]{bcf(bad);});
    odh::AnimationLimits limits;limits.motions=0;rejects([&]{bcf(b,limits);});limits={};limits.properties=4;rejects([&]{bcf(b,limits);});
    limits={};limits.text_bytes=2;rejects([&]{bcf(b,limits);});
    std::string plain;put(plain,0,2);put(plain,1,2);text(plain,"abc");require(strings(plain).entries[0].value=="abc","Pascal strings");
    std::string terminated;put(terminated,65535,2);put(terminated,1,2);terminated+=std::string("abc\0",4);
    require(strings(terminated).entries[0].comment.empty(),"terminated single strings");
    std::string paired;put(paired,65534,2);put(paired,1,2);paired+=std::string("v\0c\0",4);
    require(strings(paired).entries[0].comment=="c","string pairs");
    std::string multi;put(multi,65533,2);put(multi,3,2);multi+=std::string("\x02" "UK\0\0\x00" "US\0\0\x01" "US2\0\0",16);
    auto st=strings(multi);require(odh::string_in_language(st,1,0)->value=="US" && odh::string_in_language(st,0,1)->value=="US2" &&
                                !odh::string_in_language(st,3,0),"language grouping/alias without fallback");
    for(std::size_t n=3;n<multi.size();++n)rejects([&]{strings(multi.substr(0,n));});
    require(strings("").state==odh::StringTableState::absent && strings(std::string(2,'\0')).state==odh::StringTableState::header_only,"missing/header-only distinction");
    require(strings(multi+"xxx").remaining_bytes==3,"string tail retained");
    std::string compressed;
    for(unsigned p=0;p<7;++p){put(compressed,255,1);put(compressed,0x3F800000);put(compressed,254,1);put(compressed,1,2);}
    const auto planes=cfp(compressed,3);
    for(const auto& p:planes.bits)require(p==std::vector<std::uint32_t>{0x3F800000,0x3F800000,0x3F800000},"literal and repeat-count-plus-one");
    for(std::size_t n=0;n<compressed.size();++n)rejects([&]{cfp(compressed.substr(0,n),3);});
    require(cfp(compressed+"x",3).remaining_bytes==1,"CFP tail reported");
    std::string delta(7,static_cast<char>(127));const auto d=cfp(delta,1);
    for(const auto& p:d.bits)require(p[0]==0x2FDA1F02,"independent float32 delta golden bits");
    std::string reserved(7,static_cast<char>(253));rejects([&]{cfp(reserved,1);});
    std::string overflow;for(int p=0;p<7;++p){put(overflow,254,1);put(overflow,1,2);}rejects([&]{cfp(overflow,1);});
    std::string nan;for(int p=0;p<7;++p){put(nan,255,1);put(nan,0x7FC00000);}rejects([&]{cfp(nan,1);});
    limits={};limits.samples=1;rejects([&]{view(compressed,[&](const auto& v){return odh::read_cfp(v,3,3,limits);});});
    std::cout<<checks<<" animation resource checks passed (synthetic fixtures only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Animation test failed: "<<e.what()<<'\n';return 1;}}
