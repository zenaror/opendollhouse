// SPDX-License-Identifier: MPL-2.0
#include "world/utils/TriangleRaster.h"
#include "world/components/RoofComponent.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace FSO::LotView;
using namespace FSO::LotView::Utils;
static void Check(bool passed,const char* message) {
    if(!passed) {std::fprintf(stderr,"Roof sampling: %s\n",message);std::abort();}
}
static uint32_t RGB(int r,int g,int b,int a=255) {return Framebuffer::Pack(uint8_t(r),uint8_t(g),uint8_t(b),uint8_t(a));}
static RasterTexture AsymmetricTexture() {
    RasterTexture texture;texture.Width=texture.Height=2;
    texture.Pixels={RGB(0,20,40),RGB(100,40,60),RGB(20,120,80),RGB(200,200,240)};
    return texture;
}
static void TextureCases() {
    const RasterSampling point{},linear{RasterFilter::Linear};
    const auto texture=AsymmetricTexture();
    Check(texture.Sample(.5f,.5f,linear)==RGB(0,20,40),"linear texel center");
    Check(texture.Sample(1.5f,1.5f,linear)==RGB(200,200,240),"opposite linear texel center");
    Check(texture.Sample(1,1,linear)==RGB(80,95,105),"independent four-corner average");
    Check(texture.Sample(.75f,1.25f,linear)==RGB(55,111,101),"asymmetric subtexel interpolation");
    Check(texture.Sample(0,0,linear)==RGB(80,95,105),"both wrap seams blend last and first texels");
    Check(texture.Sample(0,.5f,linear)==RGB(50,30,50),"horizontal seam");
    Check(texture.Sample(-.25f,.5f,linear)==RGB(75,35,55),"negative linear coordinate uses floor");
    Check(texture.Sample(-.01f,-.01f,point)==RGB(200,200,240),"negative point coordinate uses floor");
    Check(texture.Sample(2,2,point)==RGB(0,20,40),"point wrap at texture extent");
    Check(texture.Sample(1,1,point)==RGB(200,200,240),"point baseline does not interpolate");
    RasterTexture alpha=texture;alpha.Pixels={RGB(0,0,0,0),RGB(0,0,0,64),RGB(0,0,0,128),RGB(0,0,0,255)};
    Check(alpha.Sample(1,1,linear)==RGB(0,0,0,112),"RGBA interpolation rounds the independent alpha mean");
    RasterTexture nonpot;nonpot.Width=3;nonpot.Height=2;
    for(int gray:{10,30,90,110,150,210})nonpot.Pixels.push_back(RGB(gray,gray,gray));
    Check(nonpot.Sample(3.2f,2.6f,point)==RGB(10,10,10),"non-POT positive wrap");
    Check(nonpot.Sample(-.01f,-.01f,point)==RGB(210,210,210),"non-POT negative point wrap");
    Check(nonpot.Sample(0,.5f,linear)==RGB(50,50,50),"non-POT linear horizontal seam");
    Check(nonpot.Sample(0,0,linear)==RGB(105,105,105),"non-POT simultaneous wrap seams");
    Check(nonpot.Sample(-.25f,.5f,linear)==RGB(70,70,70),"non-POT negative linear coordinate");
    RasterTexture one;one.Width=one.Height=1;one.Pixels={RGB(17,39,81)};
    Check(one.Sample(-7.25f,9.75f,linear)==one.Pixels[0],"single-texel texture wraps safely");
    Check(RasterTexture{}.Sample(0,0,linear)==0,"invalid texture never fetches or divides");
}
static void RasterCases() {
    auto texture=AsymmetricTexture();const RasterSampling point{},linear{RasterFilter::Linear};
    RasterVertex a,b,c;a.x=0;a.y=0;b.x=4;b.y=0;c.x=0;c.y=4;
    for(auto* v:{&a,&b,&c}){v->u=v->v=1;v->z=.75f;v->r=.5f;v->g=.75f;v->b=1;}
    Framebuffer defaultPoint(5,5),explicitPoint(5,5);defaultPoint.EnableRoom(true);explicitPoint.EnableRoom(true);
    RasterStats originalStats,pointStats;
    DrawTriangle(defaultPoint,a,b,c,&texture,0,-.05f,&originalStats,65535);
    DrawTriangle(explicitPoint,a,b,c,&texture,0,-.05f,&pointStats,65535,&point);
    Check(defaultPoint.Color==explicitPoint.Color&&defaultPoint.Depth==explicitPoint.Depth&&defaultPoint.Room==explicitPoint.Room&&originalStats.Pixels==pointStats.Pixels,"default supported point path remains byte/depth/room identical");
    Check(defaultPoint.Color[6]==RGB(100,150,240),"point baseline material modulation");
    st::Color light[1]={st::Color(7,19,31)};
    for(int path=0;path<3;++path){
        Framebuffer fb(5,5);if(path!=1)fb.EnableRoom(true);
        fb.CacheLight=light;fb.CacheOutsideLight=st::Color(29,137,61);fb.DeferredOutsideTint=path==1;
        if(path!=1)fb.MarkPrelit(6);fb.ResolvePrelit=path==2;
        DrawTriangle(fb,a,b,c,&texture,0,-.05f,nullptr,65535,&linear);
        Check(fb.Color[6]==RGB(40,71,105),"filter before material modulation, no cached/direct/prelit second tint");
        Check(fb.Depth==defaultPoint.Depth,"filtering does not alter triangle coverage or depth");
        if(path!=1)Check(fb.Room[6]==65535,"source sampling preserves the explicit fully lit room");
        if(path==0)Check(!fb.IsPrelit(6),"opaque filtered roof clears stale prelit coverage");
        const auto color=fb.Color;const auto depth=fb.Depth;
        auto aa=a,bb=b,cc=c;aa.z=bb.z=cc.z=2;
        DrawTriangle(fb,aa,bb,cc,&texture,0,0,nullptr,65535,&point);
        Check(fb.Color==color&&fb.Depth==depth,"far fragments cannot overwrite filtered source depth");
    }
    Framebuffer clipped(5,5);clipped.EnableRoom(true);clipped.SetClip(1,1,2,2);RasterStats clippedStats;
    DrawTriangle(clipped,a,b,c,&texture,0,-.05f,&clippedStats,65535,&linear);
    Check(clippedStats.Pixels==1&&clipped.Color[6]==RGB(40,71,105),"dirty clipping preserves filtered sample location");
}
static void IndependentContext() {
    FSO::LotView::Components::RoofComponent roof;
    Check(roof.GetSampling().Filter==RasterFilter::Point,"observed source context default stays point");
    roof.SetCharacterDetail(2,false);
    const auto raw=roof.GetRawCharacterDetail();const auto light=roof.GetLighting();
    const auto generation=roof.GetRenderGeneration();const auto mesh=roof.GetMeshBuildCount();
    const auto classifications=roof.GetClassificationBuildCount();
    Check(roof.SetSampling({RasterFilter::Linear}),"explicit context can select linear");
    Check(roof.GetRenderGeneration()==generation+1&&roof.GetMeshBuildCount()==mesh&&
          roof.GetClassificationBuildCount()==classifications,"sampler change only invalidates raster output");
    Check(roof.GetRawCharacterDetail()==raw&&roof.GetLighting().SameMaterialAs(light),"sampler is independent of material detail");
    const auto settled=roof.GetRenderGeneration();
    Check(!roof.SetSampling({RasterFilter::Linear})&&roof.GetRenderGeneration()==settled,"identical sampler preserves warm cache");
    roof.SetCharacterDetail(0,true);
    Check(roof.GetSampling().Filter==RasterFilter::Linear,"explicit character detail does not select the sampler");
    roof.SetStylePitch(roof.Style,.5f);
    Check(roof.GetSampling().Filter==RasterFilter::Linear,"factory replacement preserves render context sampling");
}
int main(){TextureCases();RasterCases();IndependentContext();std::puts("Roof sampling checks passed");}
