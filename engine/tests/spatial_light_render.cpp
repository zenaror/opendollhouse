// SPDX-License-Identifier: MPL-2.0
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define private public
#include "world/World2D.h"
#include "world/utils/_2DWorldBatch.h"
#undef private
using namespace FSO::LotView;
using namespace FSO::LotView::Utils;
using namespace FSO::Common::TS1;
static void Check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static uint8_t Byte(size_t offset){return uint8_t((offset*13+offset/251)%239);}
static void Loader(){
    TS1LightMap m;std::string error;
    const size_t floorOffsets[]{1744256,1769344,1869696};
    for(int z=1;z<=3;++z){
        std::vector<std::pair<size_t,size_t>> reads;
        Check(m.Load(z,TS1LightMap::FileBytes,[&](size_t off,size_t n,auto& out){
            Check(off+n<=TS1LightMap::FileBytes,"payload outside source");reads.push_back({off,n});
            out.resize(n);for(size_t i=0;i<n;++i)out[i]=Byte(off+i);return true;
        },error),"load selected zoom");
        const size_t expectedResident[]{96128,384512,1538048};
        Check(m.ResidentBytes()==expectedResident[z-1]&&reads.size()==3,"bounded selected-zoom residency");
        const int w=16<<(z-1),h=(58<<(z-1))+(8<<z),fw=224<<(z-1);
        const int radii[]{4,4,3,2};
        for(int family=0;family<2;++family){
            size_t base=252416;
            for(int f=0;f<=family;++f)for(int zoom=1;zoom<=3;++zoom){
                if(f==family&&zoom==z)goto found;
                base+=size_t(30)*(16<<(zoom-1))*((58<<(zoom-1))+(8<<zoom));
            }
            found:;
            size_t record=0;
            for(int d=0;d<4;++d)for(int lateral=-radii[d];lateral<=radii[d];++lateral,++record){
                const auto view=family==0?m.WallMask(1,-d,-lateral):m.WallMask(2,lateral,-d);
                Check(view.Data&&view.Width==w&&view.Height==h,"authored wall selector");
                uint8_t value=0;Check(view.Read(w-1,h-1,value),"wall final sample");
                Check(value==Byte(base+(record+1)*size_t(w)*h-1),"wall source offset");
            }
        }
        Check(!m.WallMask(1,1,0).Data&&!m.WallMask(2,0,1).Data&&!m.WallMask(10,0,0).Data&&
              !m.WallMask(1,std::numeric_limits<int>::min(),0).Data,"wall admission/bounds");
        for(int rotation=0;rotation<4;++rotation)for(int x=-3;x<=3;++x)for(int y=-3;y<=3;++y){
            int dx=x,dy=y;m.RotatePair(dx,dy,rotation);const int rx[]{x,-y,-x,y},ry[]{y,x,-y,-x};
            Check(dx==rx[rotation]&&dy==ry[rotation],"camera pair rotation");
            uint8_t value=0;Check(m.FloorSample(dx,dy,0,0,value),"floor projected sample");
            const int step=32<<(z-1);size_t index=size_t((6+dx+dy)*(step/4))*fw+(6+dx-dy)*(step/2);
            Check(value==Byte(floorOffsets[z-1]+index),"floor authored projected origin");
        }
        uint8_t byte=0;Check(!m.FloorSample(4,0,0,0,byte),"floor consumer radius differs from producer");
        const auto gen=m.Generation();const auto before=m.FloorMask().Data;
        Check(!m.Load(z,TS1LightMap::FileBytes,[](size_t,size_t,auto&){return false;},error),"read failure");
        Check(m.Generation()==gen&&m.FloorMask().Data==before,"failed transaction preserved old publication");
    }
    m.Reset();Check(!m.Ready()&&m.ResidentBytes()==0,"reset releases masks");
}
static void Fragments(){
    TS1LightMap masks;std::string error;
    Check(masks.Load(1,TS1LightMap::FileBytes,[](size_t,size_t n,auto& out){out.assign(n,8);return true;},error),"constant authored fixture");
    VisualLighting light;light.Enabled=true;light.Width=light.Height=1;light.Tones={16};light.Intensities={128};
    light.SpatialMaps.assign(1,std::vector<SpatialCell>(1));
    light.SpatialMaps[0][0].Slots[0]={0,0};light.SpatialMaps[0][0].Slots[1]={0,0};
    std::vector<st::Color> pixels(8,st::Color(200,120,80));Texture2D tex;tex.Width=4;tex.Height=2;tex.Data=pixels.data();
    std::vector<st::Color> ambient(65536,st::Color::White());
    _2DWorldBatch batch;batch.VisualLight=&light;batch.SpatialMasks=&masks;batch.SpatialEnabled=true;batch.AmbientLight=ambient.data();
    _2DSprite sprite;sprite.Pixel=&tex;sprite.SrcRect={0,0,4,2};sprite.AbsoluteDestRect={0,0,4,2};
    sprite.SpatialSurface=1;sprite.SpatialFrameExact=true;sprite.SpatialFrameScreen={0,0};sprite.SpatialStory=1;
    sprite.VisualSample=VisualLighting::FirstKey;
    auto draw=[&](bool cached){Framebuffer f(4,2);f.Clear(cached?0:0xff203040u);if(cached)f.EnableRoom(true);
        batch.Target=&f;batch.RenderSprite(&sprite);return f;};
    for(int alpha:{255,128})for(int q:{128,135,255}){
        for(auto& p:pixels)p.A=uint8_t(alpha);light.Intensities[0]=uint8_t(q);light.Tones[0]=uint8_t(q>>3);
        auto direct=draw(false),layer=draw(true);Framebuffer joined(4,2);joined.Clear(0xff203040u);
        World2D world;world.AmbientLight=ambient.data();world.VisualLight=&light;world.MergeLayer(layer,joined,{0,0,4,2});
        if(alpha==255)Check(direct.Color==joined.Color,"opaque spatial source transformed once before cache merge");
        else {
            // Partial cache coverage is deliberately reconstructed from source
            // fragments by World2D's existing replay; a raw MergeLayer has an
            // intermediate integer rounding and is not the final image.
            Check(layer.IsTransparentTile(0),"partial spatial cache requests fragment replay");
            Framebuffer replay(4,2);replay.Clear(0xff203040u);replay.ResolvePrelit=true;
            batch.Target=&replay;batch.RenderSprite(&sprite);
            Check(direct.Color==replay.Color,"partial spatial source replay");
        }
        const auto lit=PresentVisual565(pixels[0],uint8_t(std::min(q+16,255)>>3));
        if(alpha==255)Check(direct.Color[0]==lit.PackedValue(),"two source masks added before quantization/saturation");
        Check(layer.HasSpatialSurface&&batch.SpatialInvalidCoordinates==0,"potential spatial dependency and valid sample");
    }
    for(auto& p:pixels)p.A=255;light.Intensities[0]=255;light.Tones[0]=32;
    auto bypass=draw(false);Check(bypass.Color[0]==PresentVisual565(pixels[0],32).PackedValue(),"base palette is not saturated LUT");
    light.Tones[0]=31;auto saturated=draw(false);Check(saturated.Color!=bypass.Color,"same q, different bypass state");
    batch.SpatialEnabled=false;auto off=draw(true);Check(off.HasSpatialSurface,"F5 off keeps potential dependency");
    batch.SpatialEnabled=true;light.SpatialMaps[0][0]=SpatialCell{};auto empty=draw(true);Check(empty.HasSpatialSurface,"zero sources keeps potential dependency");
    light.SpatialMaps[0][0].Slots[0]={0,0};sprite.AbsoluteDestRect={-2,-1,4,2};sprite.SpatialFrameScreen={-2,-1};
    auto clipped=draw(false);Check(clipped.Color[0]==saturated.Color[0]&&batch.SpatialInvalidCoordinates==0,"viewport clip retains full-frame sample origin");
    std::cout<<"fragment/cache/clip checks passed; samples="<<batch.SpatialSamples<<'\n';
}
int main(){try{Loader();Fragments();std::cout<<"spatial authored render checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
