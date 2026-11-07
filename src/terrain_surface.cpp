#include "odh/terrain_surface.hpp"
#include "odh/ground_owner.hpp"
#include <cmath>
namespace odh {
namespace {
// Numeric color profile observed in FreeSO LotTypeGrassInfo at pinned revision
// 4c6b3e8f; provenance and limits are recorded in terrain-surface-validation.md.
constexpr std::array<std::array<int,3>,8> green{{{80,116,59},{181,171,149},{126,96,70},{240,245,250},{0,0,255},{74,89,66},{140,113,49},{240,245,250}}};
constexpr std::array<std::array<int,3>,8> brown{{{157,117,65},{196,185,162},{126,96,70},{240,245,250},{0,0,255},{90,69,41},{115,73,33},{15,20,140}}};
std::array<float,3> color(const SavedArchitecture& a,std::uint32_t x,std::uint32_t y,unsigned theme){
    const auto at=(std::int64_t(y)-1)*a.width+std::int64_t(x)-1;
    const float amount=at<0?1.0f:static_cast<float>(static_cast<std::uint8_t>(127-(*a.grass)[static_cast<std::size_t>(at)]))/255.0f;
    const auto& light=green[theme==1?0:theme];const auto& dark=theme==1?green[1]:brown[theme];
    std::array<float,3> out{};
    for(unsigned c=0;c<3;++c){const float lerp=static_cast<float>(light[c])+static_cast<float>(dark[c]-light[c])*amount;
        out[c]=static_cast<float>(static_cast<int>(lerp))/255.0f;}
    return out;
}
std::array<float,3> normal(const SavedArchitecture& a,std::uint32_t x,std::uint32_t y){
    auto h=[&](std::uint32_t cx,std::uint32_t cy){return terrain_corner_altitude(a,cx,cy)*3;};
    std::array<float,3> sum{};const auto limit=a.width-1;
    if(x<limit){sum[0]-=h(x+1,y)-h(x,y);sum[1]+=1;}
    if(x>1){sum[0]-=h(x,y)-h(x-1,y);sum[1]+=1;}
    if(y<limit){sum[2]-=h(x,y+1)-h(x,y);sum[1]+=1;}
    if(y>1){sum[2]-=h(x,y)-h(x,y-1);sum[1]+=1;}
    const float length=std::sqrt(sum[0]*sum[0]+sum[1]*sum[1]+sum[2]*sum[2]);
    if(length!=0)for(auto& v:sum)v/=length;
    return sum;
}
}
TerrainSurface terrain_surface(const SavedArchitecture& a,const WorldProjection& camera,TerrainTheme theme,std::int32_t base_alt,bool record_owners){
    project_tile(camera,camera.center_tile);
    const unsigned type=static_cast<unsigned>(theme);
    if(type>=8||a.width<2||a.width>64||a.height!=a.width)throw FormatError("terrain surface theme/extent outside profile");
    const auto count=std::size_t(a.width)*a.height;
    if(!a.grass||a.grass->size()!=count||!a.altitude||a.altitude->size()!=count||
       a.levels[0].floors.size()!=count||a.levels[0].walls.size()!=count)
        throw FormatError("terrain surface grid extent missing or mismatched");
    for(const auto* grid:{&a.pools,&a.water})if(*grid&&(*grid)->size()!=count)throw FormatError("terrain pool/water extent mismatch");
    TerrainSurface out;
    out.triangles.reserve(std::size_t(a.width-2)*(a.height-2)*2);
    for(std::uint32_t y=1;y+1<a.height;++y)for(std::uint32_t x=1;x+1<a.width;++x){
        const auto at=std::size_t(y)*a.width+x;const auto& wall=a.levels[0].walls[at];auto pattern=rendered_floor_pattern(a,0,at);
        const auto diag=wall.segments&48;
        if(diag==48)throw FormatError("terrain double diagonal unsupported");
        std::array<bool,2> draw{};
        if(diag){auto first=wall.top_left_pattern,second=wall.top_left_style;if(first==0&&second==0)first=second=pattern;draw={first==0||first==65534,second==0||second==65534};}
        else draw={pattern==0||pattern==65534,pattern==0||pattern==65534};
        if(!draw[0]&&!draw[1])continue;
        std::array<RasterVertex,4> vertices{};
        const std::array<std::array<std::uint32_t,2>,4> corners{{{x,y},{x+1,y},{x+1,y+1},{x,y+1}}};
        for(unsigned c=0;c<4;++c){const auto cx=corners[c][0],cy=corners[c][1];
            vertices[c]={project_tile(camera,{static_cast<float>(cx),static_cast<float>(cy),terrain_corner_altitude(a,cx,cy,base_alt)}),{0,0},normal(a,cx,cy),color(a,cx,cy,type)};}
        const auto owner=record_owners?ground_owner_id(x,y,1):std::uint16_t{0};
        if(record_owners&&!owner)throw FormatError("terrain owner tile outside supported lot");
        auto tri=[&](unsigned i,unsigned j,unsigned k){out.triangles.push_back({{vertices[i],vertices[j],vertices[k]},0,owner});
            out.room_samples.push_back({static_cast<std::int32_t>((corners[i][0]+corners[j][0]+corners[k][0])*16/3),
                static_cast<std::int32_t>((corners[i][1]+corners[j][1]+corners[k][1])*16/3),1});};
        if(!diag){tri(0,1,2);tri(2,3,0);++out.whole_tiles;}
        else if(diag==16){if(draw[0]){tri(1,2,3);++out.half_tiles;}if(draw[1]){tri(0,1,3);++out.half_tiles;}}
        else {if(draw[0]){tri(0,1,2);++out.half_tiles;}if(draw[1]){tri(2,3,0);++out.half_tiles;}}
    }
    return out;
}
}
