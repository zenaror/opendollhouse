#include "odh/wall_sprite.hpp"

#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using Pixel = std::array<std::uint8_t,4>;
const Pixel red{255,0,0,255}, green{0,255,0,255}, blue{0,0,255,255};
int checks = 0;
void require(bool ok, const char* why) { ++checks; if(!ok) throw std::runtime_error(why); }
template<class F> void rejects(F f) {
    bool caught=false; try { f(); } catch(const odh::FormatError&) { caught=true; }
    require(caught,"invalid wall input rejected");
}
odh::RasterFrame base(std::uint32_t w=1,std::uint32_t h=1,float z=INFINITY) {
    return {w,h,std::vector<Pixel>(std::size_t(w)*h,blue),
            std::vector<float>(std::size_t(w)*h,z),19,7};
}
odh::SprFrame spr(std::uint16_t w=1,std::uint16_t h=1,Pixel p=red) {
    odh::SprFrame f{}; f.width=w; f.height=h; f.pixels.assign(std::size_t(w)*h,p); return f;
}
odh::WallDepth dep(std::uint32_t w=1,std::uint32_t h=1,std::uint8_t z=255) {
    return {w,h,std::vector<std::uint8_t>(std::size_t(w)*h,z)};
}
odh::WallSpriteLayer layer(const odh::SprFrame& p,const odh::WallDepth& d,
                         const odh::SprFrame* m=nullptr) {
    return {&p,m,&d,0,0,p.width,p.height,0,0};
}
}
int main() { try {
    const auto b=base(); auto p=spr(); auto d=dep(); auto l=layer(p,d);
    const auto empty=odh::composite_wall_sprites(b,{});
    require(empty.image.pixels==b.pixels && empty.image.depth==b.depth &&
            empty.tested_samples==0 && empty.shaded_samples==0,"empty composition");
    auto r=odh::composite_wall_sprites(b,{l});
    require(r.image.pixels[0]==red && r.image.depth[0]==0 && r.tested_samples==1 &&
            r.shaded_samples==1 && r.transparent_samples==0 && r.occluded_samples==0,
            "opaque wall and accounting");
    auto ground=b;ground.owner_ids={77};
    require(odh::composite_wall_sprites(ground,{l}).image.owner_ids==
            std::vector<std::uint16_t>{0},"visible wall invalidates ground identity");
    auto malformed_base=ground;malformed_base.owner_ids.clear();malformed_base.owner_ids={77,78};
    rejects([&]{odh::composite_wall_sprites(malformed_base,{l});});
    require(r.image.tested_samples==19 && r.image.shaded_samples==7,"base counters retained");
    for(auto z : {0,51,102,153,204,255}) {
        d.pixels[0]=static_cast<std::uint8_t>(z); l.back_depth=2; l.front_depth_delta=4;
        r=odh::composite_wall_sprites(b,{l});
        require(std::abs(r.image.depth[0]-(12.f-float(z)/25.5f)) < .000002f,
                "analytic byte depth ramp");
    }
    d.pixels[0]=255; l.back_depth=1; l.front_depth_delta=0;
    r=odh::composite_wall_sprites(base(1,1,1),{l});
    require(r.image.pixels[0]==blue && r.occluded_samples==1,"base wins depth ties");
    l.back_depth=2; r=odh::composite_wall_sprites(base(1,1,1),{l});
    require(r.occluded_samples==1,"far wall occluded");
    auto p2=spr(1,1,green); auto l2=layer(p2,d);
    l.back_depth=0; r=odh::composite_wall_sprites(b,{l,l2});
    require(r.image.pixels[0]==red && r.occluded_samples==1,"first layer wins ties");
    auto m=spr(1,1,Pixel{11,22,33,255}); p.pixels[0]={231,43,17,0};
    l=layer(p,d,&m); r=odh::composite_wall_sprites(b,{l});
    require(r.image.pixels[0]==Pixel{231,43,17,255},"mask overrides transparent pattern alpha and RGB retained");
    m.pixels[0][3]=0; l.back_depth=-100; l2.back_depth=5;
    r=odh::composite_wall_sprites(b,{l,l2});
    require(r.image.pixels[0]==green && r.image.depth[0]==5 && r.transparent_samples==1 &&
            r.shaded_samples==1,"mask hole never writes depth");
    l.mask=nullptr; r=odh::composite_wall_sprites(b,{l});
    require(r.image.pixels[0]==blue && r.transparent_samples==1,"null mask uses pattern alpha");

    auto pattern=spr(3,2); pattern.pixels={red,green,blue,blue,red,green};
    auto mask=spr(2,3); mask.pixels[1][3]=0; mask.pixels[4][3]=0;
    auto depth=dep(4,1); depth.pixels={255,204,153,102};
    auto scaled=layer(pattern,depth,&mask); scaled.width=5; scaled.height=4;
    scaled.back_depth=2; scaled.front_depth_delta=4;
    r=odh::composite_wall_sprites(base(5,4),{scaled});
    const unsigned px[]={0,0,1,2,2}, py[]={0,0,1,1}, mx[]={0,0,1,1,1}, my[]={0,1,1,2}, dx[]={0,1,2,2,3};
    std::uint64_t holes=0;
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<5;++x) {
        const auto index=y*5+x; const bool hole=mask.pixels[my[y]*2+mx[x]][3]==0;
        holes+=hole;
        require(r.image.pixels[index]==(hole ? blue : pattern.pixels[py[y]*3+px[x]]),"mixed extents pixel-center color/mask");
        require(hole ? std::isinf(r.image.depth[index]) :
                std::abs(r.image.depth[index]-(2.f+float(dx[x])*2.f))<.000002f,
                "mixed extents independent depth lookup");
    }
    require(r.tested_samples==20 && r.transparent_samples==holes && r.shaded_samples==20-holes,
            "scaled candidate counters partition");
    scaled.x=-2; scaled.y=-1;
    auto unclipped=scaled; unclipped.x=0; unclipped.y=0;
    const auto large=odh::composite_wall_sprites(base(5,4),{unclipped});
    const auto clipped=odh::composite_wall_sprites(base(2,2),{scaled});
    for(unsigned y=0;y<2;++y) for(unsigned x=0;x<2;++x)
        require(clipped.image.pixels[y*2+x]==large.image.pixels[(y+1)*5+x+2] &&
                clipped.image.depth[y*2+x]==large.image.depth[(y+1)*5+x+2],"negative clip retains source coordinates");
    require(clipped.tested_samples==4,"clipped candidate count");

    // Exact sums: two layers each have 6 pattern + 6 mask + 4 Z visits,
    // 20 destination pixels and 4 clipped candidates; aliases count again.
    odh::WallSpriteLimits lim; lim.source_pixel_visits=32; lim.destination_pixels=40; lim.samples=8;
    require(odh::composite_wall_sprites(base(2,2),{scaled,scaled},lim).tested_samples==8,
            "independent budgets accept exact repeated-source sums");
    auto badlim=lim; --badlim.source_pixel_visits;
    rejects([&]{odh::composite_wall_sprites(base(2,2),{scaled,scaled},badlim);});
    badlim=lim; --badlim.destination_pixels;
    rejects([&]{odh::composite_wall_sprites(base(2,2),{scaled,scaled},badlim);});
    badlim=lim; --badlim.samples;
    rejects([&]{odh::composite_wall_sprites(base(2,2),{scaled,scaled},badlim);});
    auto giant=layer(p,d); giant.width=UINT32_MAX; giant.height=UINT32_MAX;
    giant.x=INT32_MAX; giant.y=INT32_MIN;
    lim.width=UINT32_MAX; lim.height=UINT32_MAX;
    rejects([&]{odh::composite_wall_sprites(b,{giant},lim);});
    lim.destination_pixels=UINT64_MAX; lim.source_pixel_visits=UINT64_MAX; lim.samples=UINT64_MAX;
    require(odh::composite_wall_sprites(b,{giant},lim).tested_samples==0,"giant clipped rectangle safely accounted");
    rejects([&]{odh::composite_wall_sprites(b,{giant,giant},lim);});
    auto alias=layer(p,d); lim={}; lim.source_pixel_visits=3;
    require(odh::composite_wall_sprites(b,{alias},lim).tested_samples==1,"effective alias mask counted independently");
    lim.source_pixel_visits=2; rejects([&]{odh::composite_wall_sprites(b,{alias},lim);});

    // Fully clipped invalid layers still reject; a preceding valid layer never
    // changes inputs. Metadata and indices have no bearing on decoded pixels.
    p=spr(); l=layer(p,d); auto invalid=l; invalid.x=INT32_MAX;
    const auto before=p.pixels;
    invalid.pattern=nullptr; rejects([&]{odh::composite_wall_sprites(b,{l,invalid});});
    invalid=l; invalid.depth=nullptr; rejects([&]{odh::composite_wall_sprites(b,{l,invalid});});
    invalid=l; invalid.width=0; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    invalid=l; invalid.height=0; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    auto malformed=p; malformed.pixels.clear(); invalid=l; invalid.pattern=&malformed;
    rejects([&]{odh::composite_wall_sprites(b,{l,invalid});});
    malformed=p; malformed.width=0; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    malformed=p; malformed.pixels[0][3]=128; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    invalid=l; invalid.mask=&malformed; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    auto malformed_depth=d; malformed_depth.pixels.clear(); invalid=l; invalid.depth=&malformed_depth;
    rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    malformed_depth=d; malformed_depth.width=0; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    for(auto v : {INFINITY,-INFINITY,std::numeric_limits<float>::quiet_NaN()}) {
        invalid=l; invalid.back_depth=v; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
        invalid=l; invalid.front_depth_delta=v; rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    }
    invalid=l; invalid.front_depth_delta=std::numeric_limits<float>::max();
    rejects([&]{odh::composite_wall_sprites(b,{invalid});});
    auto badbase=b; badbase.pixels[0][3]=0; rejects([&]{odh::composite_wall_sprites(badbase,{});});
    badbase=b; badbase.depth.clear(); rejects([&]{odh::composite_wall_sprites(badbase,{});});
    for(auto v : {-INFINITY,std::numeric_limits<float>::quiet_NaN()}) {
        badbase=b; badbase.depth[0]=v; rejects([&]{odh::composite_wall_sprites(badbase,{});});
    }
    badbase=b; badbase.width=0; rejects([&]{odh::composite_wall_sprites(badbase,{});});
    for(unsigned field=0;field<4;++field) {
        lim={};
        if(field==0) lim.layers=0;
        if(field==1) lim.width=0;
        if(field==2) lim.height=0;
        if(field==3) lim.pixels=0;
        rejects([&]{odh::composite_wall_sprites(b,{l},lim);});
    }
    require(p.pixels==before && b.pixels[0]==blue && std::isinf(b.depth[0]),"all input data preserved on failure");
    p.header.version=UINT32_MAX; p.indices={1,2,3}; p.reserved=UINT32_MAX;
    require(odh::composite_wall_sprites(b,{l}).image.pixels[0]==red,"SPR metadata and indices ignored");

    // Independent closed-form expected values for exact half/integer increments.
    const unsigned widths[3][4]={{16,16,32,4},{32,32,64,8},{64,64,128,16}};
    const unsigned heights[3][4]={{67,67,60,58},{135,135,120,116},{271,271,240,232}};
    const double starts[]={74,135,89.5,45};
    const int saved_round=std::fegetround();
    for(unsigned zoom=1;zoom<=3;++zoom) for(unsigned o=0;o<4;++o) {
        const auto depthmap=odh::wall_sprite_depth(zoom,o);
        require(depthmap.width==widths[zoom-1][o] && depthmap.height==heights[zoom-1][o],"wall depth config dimensions");
        const double step=double(1U << (3-zoom));
        for(unsigned y=0;y<depthmap.height;++y) for(unsigned x=0;x<depthmap.width;++x) {
            const double value=std::min(255.,starts[o]+y*step*.5+(o<2 ? x*step*(o==0 ? 1 : -1) : 0));
            const unsigned floor=static_cast<unsigned>(value);
            const auto expected=floor+(value-floor>.5 || (value-floor==.5 && floor%2) ? 1U : 0U);
            require(depthmap.pixels[y*depthmap.width+x]==expected,"analytic wall depth values, ties-even and clamp");
        }
        std::fesetround(FE_UPWARD); require(odh::wall_sprite_depth(zoom,o).pixels==depthmap.pixels,"host upward mode independent");
        std::fesetround(FE_DOWNWARD); require(odh::wall_sprite_depth(zoom,o).pixels==depthmap.pixels,"host downward mode independent");
        std::fesetround(saved_round);
    }
    rejects([]{odh::wall_sprite_depth(0,0);}); rejects([]{odh::wall_sprite_depth(4,0);});
    rejects([]{odh::wall_sprite_depth(1,4);});
    std::cout << checks << " wall sprite checks passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
