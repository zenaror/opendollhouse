// SPDX-License-Identifier: MPL-2.0
#include "selected_sim_crystal.hpp"
#include "odh/compositor.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace odh_host;
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> void reject(F action){bool failed=false;try{action();}catch(const odh::FormatError&){failed=true;}need(failed,"invalid crystal input admitted");}
void put(std::string& s,std::uint32_t v,unsigned n=4){for(unsigned i=0;i<n;++i)s.push_back(static_cast<char>(v>>(8*i)));}
void text(std::string& s,const std::string& v){put(s,static_cast<unsigned>(v.size()),1);s+=v;}
void number(std::string& s,float v){std::uint32_t raw;std::memcpy(&raw,&v,4);put(s,raw);}
void at(std::string& s,unsigned offset,std::uint32_t v,unsigned n=4){for(unsigned i=0;i<n;++i)s.at(offset+i)=static_cast<char>(v>>(8*i));}
std::string appearance(const std::string& bone="ROOT"){
    std::string s;put(s,0);put(s,1);text(s,"head-arrow");put(s,0);put(s,0);put(s,1);
    text(s,bone);text(s,"xskin-head-arrow-ROOT-ARROW");put(s,0);put(s,0);put(s,0);return s;
}
std::string mesh(){
    std::string s;text(s,"xskin-head-arrow-ROOT-ARROW");text(s,"arrow");put(s,1);text(s,"ROOT");
    put(s,4);for(const auto index:{0u,1u,2u,0u,3u,1u,0u,2u,3u,1u,3u,2u})put(s,index);
    put(s,1);for(const auto value:{0u,0u,4u,0u,0u})put(s,value);
    put(s,4);for(unsigned i=0;i<4;++i){number(s,.5f);number(s,.5f);}
    put(s,0);put(s,4);
    for(const auto& p:std::array<std::array<float,3>,4>{{{-.3f,-.2f,0},{.3f,-.2f,0},{0,.3f,0},{0,0,2.1f}}}){
        for(auto v:p)number(s,v);
        for(const auto v:{0.f,1.f,0.f})number(s,v);
    }
    return s;
}
std::string bitmap(){
    std::string s(58,'\0');at(s,0,0x4d42,2);at(s,2,58);at(s,10,54);at(s,14,40);at(s,18,1);at(s,22,1);
    at(s,26,1,2);at(s,28,24,2);s[54]=s[55]=s[56]=static_cast<char>(128);return s;
}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string s="FAR!byAZ";put(s,1);put(s,0);std::vector<unsigned> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<unsigned>(s.size()));s+=entry.second;}
    at(s,12,static_cast<unsigned>(s.size()));put(s,static_cast<unsigned>(entries.size()));
    for(unsigned i=0;i<entries.size();++i){const auto& entry=entries[i];
        put(s,static_cast<unsigned>(entry.second.size()));put(s,static_cast<unsigned>(entry.second.size()));put(s,offsets[i]);
        put(s,static_cast<unsigned>(entry.first.size()));s+=entry.first;
    }return s;
}
void write(const std::filesystem::path& path,const std::string& bytes){
    std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));need(bool(f),"fixture write");
}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("odh-crystal-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){need(std::filesystem::create_directory(path),"create fixture directory");}
    ~Temp(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
constexpr float half_pi=1.5707963267948966f;
void cases(){
    struct ColorCase {std::int16_t mood;std::array<float,3> rgb;};
    for(const auto& item:std::vector<ColorCase>{{-32768,{1,0,0}},{-84,{1,0,0}},
        {-25,{1,.91f,.81f}},{0,{1,1,.6f}},{20,{.76f,1,.6f}},{50,{.4f,1,.4f}},
        {84,{0,1,0}},{32767,{0,1,0}}}){
        const auto actual=selected_sim_crystal_color(item.mood);
        for(unsigned i=0;i<3;++i)need(std::abs(actual[i]-item.rgb[i])<.00001f,"original piecewise mood color/clamp");
    }
    const auto shadow=selected_sim_crystal_light(100,{0,-1,0},{0,1,0});
    const auto facing=selected_sim_crystal_light(100,{0,2,0},{0,3,0});
    need(std::abs(shadow[1]-76.f/255)<.00001f&&
         std::abs(shadow[0]-12.f/255)<.00001f,"independent packed ambient on unlit facets");
    need(facing[1]==1&&facing[0]==shadow[0],"Lambert normalization and saturation");
    need(selected_sim_crystal_light(100,{1,0,0},{0,1,0})==shadow,
         "perpendicular facets have ambient only, not half-Lambert");
    need(selected_sim_crystal_light(100,{0,0,0},{0,1,0})==shadow,
         "zero normal retains ambient");
    reject([]{selected_sim_crystal_light(0,{0,1,0},{0,0,0});});
    reject([]{selected_sim_crystal_light(0,{INFINITY,1,0},{0,1,0});});
    reject([]{selected_sim_crystal_light(0,{0,1,0},{0,NAN,0});});
    Temp temp;const auto animation=temp.path/"animation.far",textures=temp.path/"textures.far";
    const auto valid_animation=far({{"head-arrow.cmx.bcf",appearance()},{"xskin-head-arrow-ROOT-ARROW.bmf",mesh()}});
    write(animation,valid_animation);write(textures,far({{"arrow.BMP",bitmap()}}));
    SelectedSimCrystal crystal(animation.string(),textures.string());
    const auto bounds=crystal.asset_bounds();
    need(std::abs(bounds[0][0]+.3f)<.00001f&&std::abs(bounds[1][2]-2.1f)<.00001f,"authored mesh bounds retained");
    odh::WorldProjection camera;camera.width=camera.height=256;camera.center_tile={0,0,1.5f};
    for(unsigned rotation=0;rotation<4;++rotation)for(unsigned zoom=1;zoom<=3;++zoom)
        for(float angle:{0.f,.7f,1.9f})for(std::int16_t mood:{std::int16_t(-100),std::int16_t(0),std::int16_t(100)}){
            camera.rotation=static_cast<std::uint8_t>(rotation);camera.zoom=static_cast<std::uint8_t>(zoom);
            const auto frame=crystal.render(camera,{0,0,.5f},angle,mood,1,{0,0,1},{half_pi,0,0});
            need(frame.shaded_samples>0&&frame.owner_ids.empty(),"all views/rotations render original unowned crystal");
            for(unsigned i=0;i<frame.depth.size();++i)if(std::isfinite(frame.depth[i])){
                const auto& pixel=frame.pixels[i];
                if(mood<0)need(pixel[0]>0&&pixel[0]<=128&&pixel[1]>=5&&pixel[1]<=7&&pixel[2]==pixel[1]&&pixel[3]==255,
                    "lit original gray texture retains red mood tint");
                if(mood>0)need(pixel[1]>0&&pixel[1]<=128&&pixel[0]>=5&&pixel[0]<=7&&pixel[2]==pixel[0]&&pixel[3]==255,
                    "lit original gray texture retains green mood tint");
            }
        }
    camera.rotation=0;camera.zoom=2;
    auto first=crystal.render(camera,{0,0,.5f},.7f,100,1,{0,0,1},{half_pi,0,0});
    auto second=crystal.render(camera,{0,0,1.f},.7f,100,1,{0,0,1},{half_pi,0,0});
    const auto centroid=[](const odh::RasterFrame& frame){double sum=0,count=0;
        for(unsigned i=0;i<frame.depth.size();++i)if(std::isfinite(frame.depth[i])){sum+=i/frame.width;++count;}
        need(count>0,"visible layer centroid");return sum/count;
    };
    need(centroid(second)<centroid(first),"real head height moves the crystal upward");
    odh::RasterFrame base{};base.width=base.height=256;base.pixels.assign(256*256,{11,22,33,255});
    base.depth.assign(256*256,std::numeric_limits<float>::infinity());base.owner_ids.assign(256*256,77);
    const auto merged=overlay_selected_sim_crystal(base,first);
    need(merged.owner_ids==base.owner_ids&&merged.pixels!=base.pixels,"crystal merge preserves scene picking");
    base.depth.assign(256*256,-100000.f);
    const auto occluded=overlay_selected_sim_crystal(base,first);
    need(occluded.pixels==base.pixels&&occluded.depth==base.depth,"scene depth occludes layer");
    const auto draw=[&](float angle,float scale,std::array<float,3> head,std::array<float,3> basis){
        return crystal.render(camera,head,angle,0,scale,{0,0,1},basis);
    };
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    reject([&]{draw(nan,1,{0,0,0},{0,0,0});});reject([&]{draw(0,0,{0,0,0},{0,0,0});});
    reject([&]{draw(0,1025,{0,0,0},{0,0,0});});reject([&]{draw(0,1,{nan,0,0},{0,0,0});});
    reject([&]{draw(0,1,{0,0,0},{nan,0,0});});
    auto bad_camera=camera;bad_camera.rotation=4;reject([&]{crystal.render(bad_camera,{0,0,0},0,0,1,{0,0,0});});
    odh::RasterLimits raster_limits;raster_limits.triangles=1;
    reject([&]{crystal.render(camera,{0,0,0},0,0,1,{0,0,0},{half_pi,0,0},raster_limits);});
    // Construction validates exact selected resources, not substitute geometry.
    const auto construct=[&](SelectedSimCrystalLimits limits={}){return SelectedSimCrystal(animation.string(),textures.string(),limits);};
    write(animation,far({}));reject([&]{construct();});
    write(animation,far({{"head-arrow.cmx.bcf",appearance("HEAD")},{"xskin-head-arrow-ROOT-ARROW.bmf",mesh()}}));reject([&]{construct();});
    write(animation,far({{"head-arrow.cmx.bcf",appearance()},{"HEAD-ARROW.CMX.BCF",appearance()},{"xskin-head-arrow-ROOT-ARROW.bmf",mesh()}}));reject([&]{construct();});
    write(animation,far({{"head-arrow.cmx.bcf",appearance()},{"xskin-head-arrow-ROOT-ARROW.bmf","bad"}}));reject([&]{construct();});
    write(animation,valid_animation);write(textures,far({{"arrow.BMP","bad"}}));reject([&]{construct();});
    write(textures,far({{"arrow.BMP",bitmap()},{"ARROW.bmp",bitmap()}}));reject([&]{construct();});
    write(textures,far({{"arrow.BMP",bitmap()}}));
    SelectedSimCrystalLimits limits;limits.asset_bytes=1;reject([&]{construct(limits);});
    limits={};limits.archive_bytes=1;reject([&]{construct(limits);});
    limits={};limits.vertices=3;reject([&]{construct(limits);});
    limits={};limits.triangles=3;reject([&]{construct(limits);});
    limits={};limits.texture_pixels=0;reject([&]{construct(limits);});
    // Draw remains independent of subsequent archive changes.
    write(animation,"not FAR anymore");write(textures,"not FAR anymore");
    need(crystal.render(camera,{0,0,.5f},.7f,100,1,{0,0,1},{half_pi,0,0}).pixels==first.pixels,"render owns all decoded data");
}
void commercial(const char* animations,const char* textures,const char* prefix){
    SelectedSimCrystal crystal(animations,textures);const auto bounds=crystal.asset_bounds();
    std::cout<<"skinned original bounds: ["<<bounds[0][0]<<','<<bounds[0][1]<<','<<bounds[0][2]<<"] ["
        <<bounds[1][0]<<','<<bounds[1][1]<<','<<bounds[1][2]<<"]\n";
    odh::WorldProjection camera;camera.width=camera.height=256;camera.center_tile={0,0,1.5f};
    for(std::int16_t mood:{std::int16_t(-100),std::int16_t(0),std::int16_t(100)}){
        const auto frame=crystal.render(camera,{0,0,.5f},.7f,mood,1,{0,0,1},{half_pi,0,0});
        std::ofstream file(std::string(prefix)+"-"+std::to_string(mood)+".ppm",std::ios::binary);
        file<<"P6\n"<<frame.width<<' '<<frame.height<<"\n255\n";
        for(const auto& color:frame.pixels)for(unsigned i=0;i<3;++i)file.put(static_cast<char>(color[i]));
        need(bool(file),"commercial preview write");
    }
}
}
int main(int argc,char** argv){try{
    if(argc==4)commercial(argv[1],argv[2],argv[3]);
    else if(argc==1)cases();else throw std::runtime_error("usage: selected_sim_crystal [Animation.far Textures.far output-prefix]");
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
