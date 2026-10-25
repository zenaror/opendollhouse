#include "odh/raster.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
int checks=0;
using Pixel=std::array<std::uint8_t,4>;
const Pixel red{255,0,0,255},green{0,255,0,255},blue{0,0,255,255},white{255,255,255,255};
void require(bool ok,const char* why) {++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F fn) {
    bool failed=false;try{fn();}catch(const odh::FormatError&){failed=true;}
    require(failed,"invalid raster input or budget rejected");
}
odh::BitmapImage texture(std::uint32_t w,std::uint32_t h,std::vector<Pixel> pixels) {
    return {w,h,24,true,std::move(pixels),0};
}
odh::RasterVertex vertex(float x,float y,float z=0,float u=0,float v=0) {
    return {{x,y,z},{u,v},{0,1,0}};
}
odh::RasterTriangle triangle(std::size_t tex=0,float depth=0) {
    return {{{vertex(0,0,depth),vertex(2,0,depth),vertex(0,2,depth)}},tex};
}
std::vector<odh::RasterTriangle> square(std::size_t tex=0,float depth=0) {
    return {{{{vertex(0,0,depth,0,0),vertex(2,0,depth,1,0),vertex(2,2,depth,1,1)}},tex},
            {{{vertex(0,0,depth,0,0),vertex(2,2,depth,1,1),vertex(0,2,depth,0,1)}},tex}};
}
odh::RasterOptions nearest() {odh::RasterOptions o;o.filter=odh::TextureFilter::nearest;return o;}
Pixel at_uv(const odh::BitmapImage& image,float u,float v,odh::TextureFilter filter) {
    auto tri=triangle();for(auto& vertex:tri.vertices)vertex.texcoord={u,v};
    odh::RasterOptions options;options.filter=filter;
    return odh::rasterize(1,1,{tri},{image},options).pixels[0];
}
}

int main() {try {
    {
        auto tri=triangle();tri.vertices[0].color={1,0,0};tri.vertices[1].color={0,1,0};tri.vertices[2].color={0,0,1};
        odh::RasterOptions options;options.vertex_colors=true;
        const std::vector<odh::BitmapImage> tex{texture(1,1,{white})};
        require(odh::rasterize(1,1,{tri},tex,options).pixels[0]==Pixel{128,64,64,255},"barycentric vertex RGB modulation");
        require(odh::rasterize(1,1,{tri},tex).pixels[0]==white,"default profile ignores valid vertex colors");
        for(auto& v:tri.vertices)v.normal={0,-1,0};options.lighting=true;options.directional_up_light=true;
        require(odh::rasterize(1,1,{tri},tex,options).pixels[0]==Pixel{0,0,0,255},"directional up lighting lower hemisphere");
        for(float bad:{-0.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){tri.vertices[0].color[0]=bad;rejects([&]{odh::rasterize(1,1,{tri},tex,options);});}
    }
    const auto palette=texture(2,2,{red,green,blue,white});
    const std::vector<odh::BitmapImage> solids{texture(1,1,{red}),texture(1,1,{green}),
                                               texture(1,1,{white})};
    const auto empty=odh::rasterize(2,2,{},{});
    require(empty.width==2 && empty.height==2 && empty.pixels==std::vector<Pixel>(4,{245,245,242,255}),
            "empty frame uses opaque clear color");
    require(std::all_of(empty.depth.begin(),empty.depth.end(),[](float z){return std::isinf(z) && z>0;}) &&
            empty.tested_samples==0 && empty.shaded_samples==0,"empty depth and work counters initialized");
    const auto quad=square();
    const auto frame=odh::rasterize(2,2,quad,{palette},nearest());
    require(frame.pixels==palette.pixels,"affine UVs use top-down rows without V flip");
    require(frame.shaded_samples==4 && frame.tested_samples==8,"quad covers four pixels with eight bbox candidates");
    const auto a=odh::rasterize(2,2,{quad[0]},solids,nearest());
    const auto b=odh::rasterize(2,2,{quad[1]},solids,nearest());
    require(a.shaded_samples+b.shaded_samples==4,"shared diagonal has exactly one owner");
    for(unsigned i=0;i<4;++i)
        require(std::isfinite(a.depth[i])!=std::isfinite(b.depth[i]),"top-left shared-edge masks are disjoint and complete");
    auto reversed=quad;
    for(auto& tri:reversed)std::swap(tri.vertices[1],tri.vertices[2]);
    const auto reverse_frame=odh::rasterize(2,2,reversed,{palette},nearest());
    require(reverse_frame.pixels==frame.pixels && reverse_frame.depth==frame.depth &&
            reverse_frame.shaded_samples==4,"both windings preserve coverage and interpolation");
    const auto n=odh::TextureFilter::nearest,l=odh::TextureFilter::bilinear;
    require(at_uv(palette,.25F,.25F,n)==red && at_uv(palette,.75F,.25F,n)==green &&
            at_uv(palette,.25F,.75F,n)==blue && at_uv(palette,.75F,.75F,n)==white,
            "nearest picks each analytic texel");
    require(at_uv(palette,0,0,n)==red && at_uv(palette,1,1,n)==red &&
            at_uv(palette,-.25F,-.25F,n)==white,"nearest wraps exact and negative boundaries");
    require(at_uv(palette,1e30F,-1e30F,n)==red,"huge finite UVs reduced before integer conversion");
    require(at_uv(palette,.25F,.25F,l)==red && at_uv(palette,.75F,.75F,l)==white,
            "bilinear texel centers retain exact colors");
    require(at_uv(palette,.5F,.5F,l)==Pixel{128,128,128,255} &&
            at_uv(palette,0,0,l)==Pixel{128,128,128,255},"bilinear blends four neighbors and wraps seam");
    require(at_uv(palette,-.25F,-.25F,l)==white && at_uv(palette,1.25F,1.25F,l)==red,
            "bilinear negative and positive periods wrap each neighbor");
    require(at_uv(palette,1e30F,-1e30F,l)==Pixel{128,128,128,255},"huge bilinear UV stays finite at wrap seam");
    const auto stripe=texture(2,1,{red,green});
    require(at_uv(stripe,.375F,.9F,l)==Pixel{191,64,0,255},"bilinear quarter weight rounds RGB and wraps one-row height");
    const auto far=square(0,2),near=square(1,1);
    auto far_first=far;far_first.insert(far_first.end(),near.begin(),near.end());
    auto near_first=near;near_first.insert(near_first.end(),far.begin(),far.end());
    const auto visible=odh::rasterize(2,2,far_first,solids);
    const auto permuted=odh::rasterize(2,2,near_first,solids);
    require(visible.pixels==std::vector<Pixel>(4,green) && visible.pixels==permuted.pixels &&
            visible.depth==permuted.depth,"smaller depth wins independently of submission order");
    require(visible.shaded_samples==8 && permuted.shaded_samples==4,"shaded counter measures depth-passing writes");
    auto tagged_far=square(0,2),tagged_near=square(1,1);
    for(auto& tri:tagged_far)tri.owner_id=17;
    for(auto& tri:tagged_near)tri.owner_id=29;
    auto tagged_triangles=tagged_far;
    tagged_triangles.insert(tagged_triangles.end(),tagged_near.begin(),tagged_near.end());
    odh::RasterOptions record_owners;record_owners.record_owner_ids=true;
    const auto tagged=odh::rasterize(2,2,tagged_triangles,solids,record_owners);
    require(tagged.owner_ids==std::vector<std::uint16_t>(4,29),
            "optional pixel owner follows the same nearer depth winner as color");
    require(visible.owner_ids.empty(),"owner storage remains absent when the caller does not request it");
    auto equal=square(1,2);auto ties=far;ties.insert(ties.end(),equal.begin(),equal.end());
    require(odh::rasterize(2,2,ties,solids).pixels==std::vector<Pixel>(4,red),"depth ties retain first color");
    auto equal_tag=equal;for(auto& tri:tagged_far)tri.owner_id=17;for(auto& tri:equal_tag)tri.owner_id=29;
    auto tie_tags=tagged_far;tie_tags.insert(tie_tags.end(),equal_tag.begin(),equal_tag.end());
    require(odh::rasterize(2,2,tie_tags,solids,record_owners).owner_ids==
            std::vector<std::uint16_t>(4,17),"equal-depth ownership retains the first surface");
    auto slope=square();for(auto& tri:slope)for(auto& v:tri.vertices)v.position[2]=v.position[0];
    require(odh::rasterize(2,2,slope,solids).depth==std::vector<float>{.5F,1.5F,.5F,1.5F},
            "depth interpolates affinely at pixel centers");
    {
        auto perspective_triangle=triangle();
        perspective_triangle.vertices={vertex(0,0,1,0,0),vertex(2,0,2,1,0),vertex(0,2,4,0,0)};
        const std::vector<odh::BitmapImage> gradient{texture(4,1,{red,green,blue,white})};
        auto options=nearest();
        const auto affine=odh::rasterize(1,1,{perspective_triangle},gradient,options);
        options.perspective_correct=true;
        const auto corrected=odh::rasterize(1,1,{perspective_triangle},gradient,options);
        require(affine.pixels[0]==green&&corrected.pixels[0]==red,
                "perspective-correct UVs differ from the affine profile at analytic weights");
        require(std::abs(corrected.depth[0]-1.0F/0.6875F)<1e-6F&&corrected.depth[0]<affine.depth[0],
                "perspective depth uses reciprocal interpolation and keeps nearer surfaces first");
        for(auto& v:perspective_triangle.vertices)v.position[2]=0;
        rejects([&]{odh::rasterize(1,1,{perspective_triangle},gradient,options);});
    }
    auto outside=triangle();for(auto& v:outside.vertices){v.position[0]-=10;v.position[1]-=10;}
    require(odh::rasterize(2,2,{outside},solids).tested_samples==0,"fully clipped triangle costs zero candidates");
    auto clipped=triangle();clipped.vertices={vertex(-10,-10),vertex(10,-10),vertex(0,10)};
    require(odh::rasterize(2,2,{clipped},solids).pixels==std::vector<Pixel>(4,red),"negative bounds clip to viewport");
    const auto maximum=std::numeric_limits<float>::max();
    auto giant=triangle();giant.vertices={vertex(-maximum,-maximum),vertex(maximum,-maximum),vertex(0,maximum)};
    const auto giant_frame=odh::rasterize(2,2,{giant},solids);
    require(giant_frame.tested_samples==4 && giant_frame.pixels==std::vector<Pixel>(4,red),
            "giant finite coordinates clip safely before integer conversion");
    auto degenerate=triangle();degenerate.vertices[2]=degenerate.vertices[1];
    require(odh::rasterize(2,2,{degenerate},solids).tested_samples==0,"zero-area triangle skipped");
    odh::RasterOptions lit;lit.lighting=true;
    auto normal_triangle=triangle(2);
    for(const auto [normal,color]:std::vector<std::pair<std::array<float,3>,Pixel>>{
        {{0,3,0},white},{{0,-3,0},{128,128,128,255}},{{1,0,0},{218,218,218,255}},{{0,0,0},white},
        {{maximum,maximum,0},{245,245,245,255}}}) {
        for(auto& v:normal_triangle.vertices)v.normal=normal;
        require(odh::rasterize(1,1,{normal_triangle},solids,lit).pixels[0]==color,
                "optional lighting normalizes normals and applies analytic vertical shade");
    }
    normal_triangle.vertices[0].normal={0,1,0};
    normal_triangle.vertices[1].normal=normal_triangle.vertices[2].normal={0,-1,0};
    require(odh::rasterize(1,1,{normal_triangle},solids,lit).pixels[0]==white,
            "zero interpolated normal follows explicit up policy");
    for(auto& v:normal_triangle.vertices)v.normal={0,-1,0};
    require(odh::rasterize(1,1,{normal_triangle},solids).pixels[0]==white,"unlit is default");
    rejects([&]{odh::rasterize(0,2,{},{});});rejects([&]{odh::rasterize(2,0,{},{});});
    auto limits=odh::RasterLimits{};
    for(unsigned which=0;which<7;++which) {
        auto small=limits;
        if(which==0)small.width=1;
        if(which==1)small.height=1;
        if(which==2)small.pixels=3;
        if(which==3)small.triangles=1;
        if(which==4)small.textures=0;
        if(which==5)small.texture_pixels=3;
        if(which==6)small.samples=7;
        rejects([&]{odh::rasterize(2,2,quad,{palette},{},small);});
    }
    limits.width=2;limits.height=2;limits.pixels=4;limits.triangles=2;limits.textures=1;
    limits.texture_pixels=4;limits.samples=8;
    require(odh::rasterize(2,2,quad,{palette},nearest(),limits).pixels==palette.pixels,
            "exact frame, texture, triangle and sample budgets accepted");
    require(odh::rasterize(2,2,{degenerate},solids,{},
        odh::RasterLimits{2,2,4,1,3,3,0}).tested_samples==0,"degenerate allowed with zero sample budget");
    odh::RasterLimits huge;huge.width=huge.height=huge.pixels=std::numeric_limits<std::uint32_t>::max();
    rejects([&]{odh::rasterize(huge.width,huge.height,{}, {},{},huge);});
    for(unsigned type=0;type<4;++type) {
        auto malformed=palette;
        if(type==0)malformed.width=0;
        if(type==1)malformed.pixels.pop_back();
        if(type==2)malformed.pixels[0][3]=0;
        if(type==3)malformed.width=malformed.height=std::numeric_limits<std::uint32_t>::max();
        rejects([&]{odh::rasterize(1,1,{}, {malformed});});
    }
    auto sum_limits=odh::RasterLimits{};sum_limits.texture_pixels=7;
    rejects([&]{odh::rasterize(1,1,{}, {palette,palette},{},sum_limits);});
    auto bad_index=triangle(solids.size());rejects([&]{odh::rasterize(1,1,{bad_index},solids);});
    rejects([&]{odh::rasterize(1,1,{triangle()},{});});
    for(const auto bad:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()})
        for(unsigned component=0;component<8;++component) {
            auto nonfinite=triangle();
            if(component<3)nonfinite.vertices[0].position[component]=bad;
            else if(component<5)nonfinite.vertices[0].texcoord[component-3]=bad;
            else nonfinite.vertices[0].normal[component-5]=bad;
            rejects([&]{odh::rasterize(1,1,{nonfinite},solids);});
        }
    auto transparent=nearest();transparent.clear[3]=0;
    rejects([&]{odh::rasterize(1,1,{}, {},transparent);});
    auto invalid_filter=nearest();invalid_filter.filter=static_cast<odh::TextureFilter>(99);
    rejects([&]{odh::rasterize(1,1,{}, {},invalid_filter);});
    const auto saved_pixels=palette.pixels;const auto saved_position=quad[0].vertices[0].position;
    odh::RasterLimits late;late.samples=4;
    rejects([&]{odh::rasterize(2,2,quad,{palette},{},late);});
    require(palette.pixels==saved_pixels && quad[0].vertices[0].position==saved_position,
            "late sample-budget rejection leaves inputs unchanged");
    std::cout<<checks<<" raster checks passed (synthetic fixtures only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"raster test failed: "<<e.what()<<'\n';return 1;}}
