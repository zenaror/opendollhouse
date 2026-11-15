/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/OriginalCrystalProjection.h"
#include "vitaboy/AvatarRenderer.h"
#include "world/utils/WorldCamera.h"
#include <iostream>
#include <stdexcept>
int main(){try{
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
