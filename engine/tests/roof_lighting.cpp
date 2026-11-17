// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1RoofLighting.h"
#include "world/components/RoofComponent.h"
#include "world/model/Blueprint.h"
#include <cstdint>
#include <cstring>
#include "world/utils/TriangleRaster.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace FSO::Common::TS1;
using namespace FSO::LotView;
using namespace FSO::LotView::Utils;
static void Check(bool passed,const char* message) {
    if(!passed) { std::fprintf(stderr,"Roof material conformance: %s\n",message);std::abort(); }
}
static void Near(float a,float b,const char* message) { Check(std::abs(a-b)<2e-6f,message); }
static void MaterialCases() {
    const RoofLightingInput lit{2,{{.07f,.23f,.41f}}};
    const RoofRGB texture{{.19f,.53f,.87f}};
    // Independent ideal fixed-function numbers, asymmetric in face and RGB.
    // They do not assert original Direct3D3 driver byte quantization.
    const RoofRGB expected[4]={
        {{.09140028919545555f,.33975870143995496f,.7143171136844543f}},
        {{.19f,.53f,.87f}},
        {{.15834339422013177f,.5264947312456307f,.87f}},
        {{.05792873668311747f,.24639068653711713f,.5610526363911168f}}};
    const float diffuse[4]={.41105415366029235f,.9395523512235254f,
        .7633862853691145f,.23488808780588136f};
    for(int face=0;face<4;++face) {
        const auto normal=RoofFaceNormal(face,1);
        const auto color=RoofMaterialRGB(texture,normal,lit);
        const auto base=RoofMaterialFactors(normal,{2,{{0,0,0}}});
        float length=0;
        for(int c=0;c<3;++c) {
            length+=normal[size_t(c)]*normal[size_t(c)];
            Near(color[size_t(c)],expected[face][size_t(c)],"asymmetric texture/ambient/face result");
            Near(base[size_t(c)],diffuse[face],"white owned directional diffuse");
        }
        Near(length,1,"flat face normal must be unit length");
        const auto medium=RoofMaterialRGB(texture,normal,{1,lit.Ambient});
        Check(medium==color,"modes1/2 have the same established material operation");
        Check(RoofMaterialRGB(texture,normal,{0,lit.Ambient})==texture,"mode0 selects texture alone");
    }
    const RoofRGB steep[4]={{{0,.447213595499958f,-.894427190999916f}},
        {{.894427190999916f,.447213595499958f,0}},{{0,.447213595499958f,.894427190999916f}},
        {{-.894427190999916f,.447213595499958f,0}}};
    for(int face=0;face<4;++face) for(int c=0;c<3;++c)
        Near(RoofFaceNormal(face,2)[size_t(c)],steep[face][size_t(c)],"independent steep face normal family");
    const auto away=RoofMaterialRGB(texture,{{0,-1,0}},lit);
    for(int c=0;c<3;++c) Near(away[size_t(c)],texture[size_t(c)]*lit.Ambient[size_t(c)],"backface diffuse clamps at zero");
    const auto flat=RoofMaterialFactors(RoofFaceNormal(0,0),{2,{{0,0,0}}});
    Near(flat[0],.8304547985373997f,"flat roof uses vertical world normal");
    Check((RoofMaterialFactors(RoofFaceNormal(2,1),{2,{{1,1,1}}})==RoofRGB{{1,1,1}}),"ambient plus diffuse clamps before modulation");
}
static RoofRGB TurnY(RoofRGB value,float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    return {{value[0]*c+value[2]*s,value[1],-value[0]*s+value[2]*c}};
}
static void CameraInvariant() {
    const auto normal=RoofFaceNormal(0,.5f);
    const auto sourceLight=RoofSourceLightToSurface();
    const RoofRGB toLight{{sourceLight[2],sourceLight[1],sourceLight[0]}};
    const auto factors=RoofMaterialFactors(normal,{2,{{0,0,0}}});
    for(int camera=0;camera<4;++camera) {
        const float yaw=(45-90*camera)*3.14159265358979323846f/180;
        const auto n=TurnY(normal,yaw),light=TurnY(toLight,yaw);
        const float dot=std::max(0.0f,n[0]*light[0]+n[1]*light[1]+n[2]*light[2]);
        Near(dot,factors[0],"view transforms both normal and light; same world face stays lit identically");
    }
}
static void ExplicitInput() {
    Components::RoofComponent roof;
    Check(!roof.GetLighting().Available(),"no unproved material input on construction");
    const RoofLightingInput known{2,{{0,0,0}}};
    Check(roof.SetLighting(known),"explicit source bind changes material consumer");
    Check(!roof.SetLighting(known),"identical input keeps cached wall valid");
    roof.SetCharacterDetail(2,false);
    Check(roof.GetRawCharacterDetail()==2 && !roof.HasMaterials(),"new source factory binds raw detail before drawing");
    roof.SetStylePitch(3,.5f);
    Check(roof.GetLighting().Mode==2,"style/pitch update must not guess a new construction mode");
    Check(roof.SetLighting({0,{{0,0,0}}}),"texture-only mode changes material operation");
    Check(!roof.SetLighting({0,{{.2f,.3f,.4f}}}),"mode0 does not depend on inherited ambient");
    Near(roof.GetLighting().Ambient[1],.3f,"latest explicit ambient snapshot is retained");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    Check((RoofMaterialFactors({{0,1,0}},{0,{{nan,nan,nan}}})==RoofRGB{{1,1,1}}),"mode0 needs no ambient observation");
    Check(roof.SetLighting({2,{{nan,0,0}}}),"unavailable lit ambient returns to fallback");
    Check(!roof.GetLighting().Available(),"nonfinite lit state cannot enter raster arithmetic");
    Check(!roof.SetLighting({}),"equivalent unbound consumer needs no new raster");
}
static void UniformHistory() {
    RoofUniformMaterialState state;
    state.SetCharacterDetail(1,false);
    Check(state.Mode==1 && !state.HasMaterials,"fresh Medium mesh inherits raw1 and is lit");
    state.HasMaterials=true;
    state.SetCharacterDetail(1,false);
    Check(state.Mode==1,"unrelated settings binding does not emulate a roof apply");
    state.SetCharacterDetail(1,true);
    Check(state.Mode==0 && state.RawDetail==1,"explicit Medium maps existing material to0 even raw value unchanged");
    state.SetCharacterDetail(2,true);
    Check(state.Mode==2 && state.HasMaterials,"explicit High maps existing material to2");
    state.SetCharacterDetail(1,false);
    Check(state.Mode==2 && state.RawDetail==1,"construction global stays distinct from existing mode");
    state.ResetFactory();
    Check(state.Mode==1 && !state.HasMaterials,"new style/pitch factory inherits current raw detail");
    state.HasMaterials=true;state.SetCharacterDetail(0,true);
    Check(state.Mode==0,"explicit Low selects texture-only material");
    Near(ClampRoofPitch(-1),0,"source pitch clamps negative to0");
    Near(ClampRoofPitch(std::numeric_limits<float>::quiet_NaN()),0,"source ordered pitch comparison maps NaN to0");
    Near(ClampRoofPitch(3),2,"source pitch clamps above2");
    const float epsilon=1.0e-6f;
    Check(!RoofPitchReplacesFactory(0,epsilon),"source factory epsilon equality does not reset");
    Check(RoofPitchReplacesFactory(0,std::nextafter(epsilon,1.0f)),"strictly greater than source epsilon resets");
    Check(RoofPitchReplacesFactory(std::numeric_limits<float>::quiet_NaN(),0),"unordered source pitch difference resets");
    Components::RoofComponent roof;
    roof.SetCharacterDetail(1,false);
    roof.SetLightingAmbient({{.07f,.23f,.41f}});
    Check(roof.GetRawCharacterDetail()==1 && roof.GetLighting().Mode==1,"ambient binding must preserve raw/event history");
    roof.SetStylePitch(3,.5f);
    Check(roof.GetRawCharacterDetail()==1 && roof.GetLighting().Mode==1 && !roof.HasMaterials(),"style/pitch replacement rebinds raw mode");
}

static void SourceAnchorsAndPitchNoOp() {
    const auto light=RoofSourceLightToSurface();
    const uint32_t bits[3]={0x3e7f1da0u,0x3f5498afu,0x3eff1da0u};
    for(int i=0;i<3;++i) {
        uint32_t actual;std::memcpy(&actual,&light[size_t(i)],sizeof(actual));
        Check(actual==bits[i],"captured source directional float field changed");
    }
    Check((RoofNormalToSource({{.6f,.8f,0}})==RoofRGB{{0,.8f,.6f}}),"architectural tile axes must swap into source D3D frame");
    Model::Blueprint bp(2,2);auto& roof=bp.RoofComp;
    roof.SetStylePitch(roof.Style,0);bp.Damage.clear();
    roof.SetCharacterDetail(1,false);roof.SetLighting({0,{{0,0,0}}});
    const float epsilon=1.0e-6f;
    for(float pitch : {0.0f,epsilon*.5f,epsilon}) {
        roof.SetStylePitch(roof.Style,pitch);
        Check(roof.Pitch==0 && bp.Damage.empty() && roof.GetLighting().Mode==0,
            "same/subepsilon pitch must preserve old pitch, material and damage list");
    }
    const float changed=std::nextafter(epsilon,1.0f);
    roof.SetStylePitch(roof.Style,changed);
    Check(roof.Pitch==changed && bp.Damage.size()==1 && roof.GetLighting().Mode==1,
        "above-epsilon pitch must update and replace the factory once");
    Components::RoofComponent styleOnly;
    styleOnly.SetStylePitch(styleOnly.Style,0);
    styleOnly.SetStylePitch(styleOnly.Style+1,epsilon*.5f);
    Check(styleOnly.Pitch==0,"style replacement must not apply independently rejected small pitch");
}

static void NoOutsideSecondTint() {
    st::Color ambient[1]={st::Color(7,19,31)};
    RasterVertex a,b,c;
    a.x=0;a.y=0;b.x=4;b.y=0;c.x=0;c.y=4;
    for(auto* v : {&a,&b,&c}) {v->r=.25f;v->g=.5f;v->b=.75f;v->z=1;}
    const uint32_t texture=Framebuffer::Pack(40,80,120,255);
    const uint32_t expected=Framebuffer::Pack(10,40,90,255);
    for(int path=0;path<3;++path) {
        Framebuffer fb(5,5);
        if(path!=1) fb.EnableRoom(true);
        fb.CacheLight=ambient;fb.CacheOutsideLight=st::Color(29,137,61);
        fb.DeferredOutsideTint=path==1;
        const size_t pixel=6;
        if(path!=1) fb.MarkPrelit(pixel);
        fb.ResolvePrelit=path==2;
        DrawTriangle(fb,a,b,c,nullptr,texture,0,nullptr,65535);
        Check(fb.Color[pixel]==expected,"cached/direct/prelit source pixel must not inherit outside RGB");
        if(path!=1) Check(fb.Room[pixel]==65535,"source-lit roof keeps an explicit fully lit room key");
        if(path==0) Check(!fb.IsPrelit(pixel),"opaque source roof clears stale prelit coverage");
    }
}
int main() { MaterialCases();CameraInvariant();ExplicitInput();UniformHistory();SourceAnchorsAndPitchNoOp();NoOutsideSecondTint(); }
