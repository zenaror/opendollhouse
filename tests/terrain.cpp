#include "odh/terrain.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
void near(float actual,float expected,const char* message) {require(std::abs(actual-expected)<0.00001f,message);}
void bad(const std::function<void()>& f) {bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}require(rejected,"invalid terrain accepted");}
odh::SavedArchitecture plane() {
    odh::SavedArchitecture a;a.width=5;a.height=4;
    a.altitude.emplace(20);
    for(std::uint32_t y=0;y<a.height;++y)for(std::uint32_t x=0;x<a.width;++x)
        (*a.altitude)[y*a.width+x]={255,static_cast<std::uint8_t>(10+x*2+y*3),127,99};
    return a;
}
}
int main() {try {
    auto a=plane();const auto original=*a.altitude;
    for(std::uint32_t y=0;y<a.height;++y)for(std::uint32_t x=0;x<a.width;++x)
        near(odh::terrain_corner_altitude(a,x,y),static_cast<float>((10+x*2+y*3)*10)*(3.0f/160.0f),"corner byte1, scale and row-major index");
    const auto n=odh::terrain_interp_normal(a,1.25f,2.75f);const float length=std::sqrt(.375f*.375f+1.f+.5625f*.5625f);
    near(n[0],-.375f/length,"source live interpolated terrain normal X slope");
    near(n[1],1/length,"source terrain normal normalized up");near(n[2],-.5625f/length,"source terrain normal Y slope");
    const auto integer_normal=odh::terrain_interp_normal(a,2,2);
    near(integer_normal[0],0,"source ceil equals floor at integer normal coordinate");near(integer_normal[1],1,"source integer ground normal source profile");
    near(odh::terrain_interp_altitude(a,1.25f,2.75f),20.75f*10.0f*(3.0f/160.0f),"bilinear plane");
    near(odh::terrain_interp_altitude(a,2.0f,2.0f),odh::terrain_corner_altitude(a,2,2),"integer sample");
    near(odh::terrain_interp_altitude(a,0,0),odh::terrain_corner_altitude(a,1,1),"lower clamp excludes corner0");
    near(odh::terrain_interp_altitude(a,-0.25f,-1.75f),odh::terrain_corner_altitude(a,1,1),"negative remainder and lower clamp");
    near(odh::terrain_interp_altitude(a,32767,32767),odh::terrain_corner_altitude(a,4,3),"upper profile clamp");
    near(odh::terrain_interp_altitude(a,-32768,-32768),odh::terrain_corner_altitude(a,1,1),"lower profile boundary");
    near(odh::terrain_interp_altitude(a,4.75f,3.5f),odh::terrain_corner_altitude(a,4,3),"upper fractional clamp");
    near(odh::terrain_interp_altitude(a,2.25f,-0.75f),17.5f*10.0f*(3.0f/160.0f),"negative y clamp keeps x interpolation");
    near(odh::terrain_corner_altitude(a,2,2,200),0,"explicit base altitude");
    near(odh::terrain_interp_altitude(a,1.25f,2.75f,200),7.5f*(3.0f/160.0f),"interpolated explicit base altitude");
    require(*a.altitude==original,"sampling must not modify input");
    for(auto& cell:*a.altitude)cell[1]=20;
    for(auto pair:{std::pair<float,float>{1.2f,2.8f},{0.2f,-0.1f},{10.2f,10.3f}})
        near(odh::terrain_interp_altitude(a,pair.first,pair.second),3.75f,"constant altitude");
    require(std::isfinite(odh::terrain_corner_altitude(a,0,0,std::numeric_limits<std::int32_t>::min())),"extreme signed base finite");
    require(std::isfinite(odh::terrain_interp_altitude(a,2,2,std::numeric_limits<std::int32_t>::max())),"extreme signed base finite");
    bad([&]{odh::terrain_corner_altitude(a,5,0);});bad([&]{odh::terrain_corner_altitude(a,0,4);});
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),32768.0f,-32769.0f}) {
        bad([&]{odh::terrain_interp_altitude(a,invalid,2);});bad([&]{odh::terrain_interp_altitude(a,2,invalid);});
    }
    auto missing=a;missing.altitude.reset();
    bad([&]{odh::terrain_corner_altitude(missing,0,0);});bad([&]{odh::terrain_interp_altitude(missing,2,2);});
    for(int change:{-1,1}) {
        auto malformed=a;malformed.altitude->resize(static_cast<std::size_t>(20+change));
        bad([&]{odh::terrain_corner_altitude(malformed,0,0);});bad([&]{odh::terrain_interp_altitude(malformed,2,2);});
    }
    for(bool width:{false,true})for(std::uint32_t dimension:{0u,1u,std::numeric_limits<std::uint32_t>::max()}) {
        auto malformed=a;if(width)malformed.width=dimension;else malformed.height=dimension;
        bad([&]{odh::terrain_corner_altitude(malformed,0,0);});bad([&]{odh::terrain_interp_altitude(malformed,2,2);});
    }
    std::cout<<checks<<" terrain checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
