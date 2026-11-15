/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/OriginalCrystalProjection.h"
#include "app/ui/OriginalCrystalOverlay.h"
#include "vitaboy/AvatarRenderer.h"
#include "world/utils/WorldCamera.h"
#include <iostream>
#include <stdexcept>
namespace {
void composite(){
    const float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
    odh::RasterFrame region{};region.width=3;region.height=2;
    region.pixels={{10,20,80,255},odh::RasterOptions{}.clear,{15,70,90,255},{4,40,60,255},{6,60,90,255},{8,80,120,255}};
    region.depth={3.f,inf,6.f,-3.f,0.f,9.f};
    const std::array<uint32_t,2> origin{2,3};
    const std::array<float,6> scene{inf,-inf,nan,6.f,0.f,-3.f};
    for(auto ox:{-7,-3,0,8,17})for(auto oy:{-7,-2,2,9})for(float bias:{-2.f,0.f,3.f,inf,-inf,nan}){
        simapp::Canvas expected(15,12),actual(15,12);
        std::vector<float> depth(expected.Px.size());
        for(size_t i=0;i<depth.size();++i){depth[i]=scene[i%scene.size()];expected.Px[i]=actual.Px[i]=uint32_t(i)*1325437u;}
        for(int y=0;y<8;++y)for(int x=0;x<8;++x){
            const int sx=ox+x,sy=oy+y;
            if(sx<0||sy<0||sx>=expected.Width||sy>=expected.Height)continue;
            auto pixel=odh::RasterOptions{}.clear;float z=inf;
            if(x>=2&&x<5&&y>=3&&y<5){const auto i=size_t(y-3)*3+(x-2);pixel=region.pixels[i];z=region.depth[i];}
            if(z+bias<=depth[size_t(sy)*expected.Width+sx])expected.Blend(sx,sy,simapp::RGBA(pixel[0],pixel[1],pixel[2],pixel[3]));
        }
        simapp::ComposeCrystal(actual,depth,{ox,oy,8,8},region,origin,bias);
        if(actual.Px!=expected.Px)throw std::runtime_error("compact crystal changed overlap, depth tie, background or clipped canvas edge");
    }
}
}
int main(){try{
    composite();
    size_t cases=0;float maximum=0;
    for(int rotation=0;rotation<4;++rotation)for(int zoom=1;zoom<=3;++zoom)
    for(const auto head: {std::array<float,3>{26.0353f,24.0484f,2.80321f},
        std::array<float,3>{26.0353f,24.0484f,2.396326f},std::array<float,3>{20.25f,30.75f,4.8f}}){
        FSO::Vitaboy::WorldCamera engine;
        engine.Rotation=FSO::LotView::WorldRotation(rotation);engine.Zoom=FSO::Vitaboy::WorldZoom(zoom);
        engine.CenterTile={25.125f,25.625f};engine.ViewDimensions={1280,720};engine.PixelOffset={1.25f,-.375f};
        FSO::LotView::Utils::WorldCamera depth;depth.Rotation=engine.Rotation;
        const auto anchor=engine.ProjectWorld({head[0]*3,head[2]*3,head[1]*3});
        const auto local=simapp::CrystalViewport(head,anchor.X,anchor.Y,uint8_t(zoom),uint8_t(rotation));
        const auto& center=local.Camera.center_tile;
        const auto bias=depth.Depth({center[0]*3,center[2]*3,center[1]*3});
        // Noncoplanar points cover shape/height and screen/depth agreement,
        // independently comparing the preserved projector with engine matrices.
        for(auto offset:{std::array<float,3>{0,0,0},std::array<float,3>{.4f,3.2f,-.3f},
            std::array<float,3>{-.6f,4.f,.2f},std::array<float,3>{.2f,2.f,.6f}}){
            const std::array<float,3> world{head[0]*3+offset[0],head[2]*3+offset[1],head[1]*3+offset[2]};
            const auto a=odh::project_world(local.Camera,world);
            const auto b=engine.ProjectWorld({world[0],world[1],world[2]});
            const float error=std::max({std::abs(a[0]+local.X-b.X),std::abs(a[1]+local.Y-b.Y),
                std::abs(a[2]+bias-depth.Depth({world[0],world[1],world[2]}))});
            maximum=std::max(maximum,error);if(error>.002f)throw std::runtime_error("original crystal projection/depth differs from avatar camera");
            ++cases;
        }
    }
    std::cout<<cases<<" camera/head/shape cases passed; max pixel/depth error "<<maximum<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
