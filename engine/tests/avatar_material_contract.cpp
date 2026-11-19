// SPDX-License-Identifier: MPL-2.0
// Source-derived material commands and real raster integration. No game assets;
// this is not a comparison with the original driver or a live player journey.
#include "common/TS1/TS1AvatarMaterial.h"
#include "vitaboy/AvatarRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace FSO::Common::TS1;
using namespace FSO::Vitaboy;
static void Need(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
static std::shared_ptr<AvatarBindingInstance> Part(Mesh* mesh=nullptr,Texture* texture=nullptr) {
    auto p=std::make_shared<AvatarBindingInstance>();p->Mesh=mesh;p->Texture=texture;return p;
}
static void SourceCommands() {
    AvatarPartMaterial m;m.Initialize(2);
    // Three actual typed original actors: UI2->0->1->2->same2,
    // effective mesh2->2->0->1->2. Ordinary flags remain000 throughout.
    for(const auto step:std::vector<std::array<int,3>>{{0,2,2},{1,0,0},{2,1,1},{2,2,2}}) {
        m.ApplyDetail(step[0],step[1],{});Need(m.Mode==step[2],"DVD old-global order/reclick");
    }
    for(int bits=0;bits<8;++bits) for(int old=0;old<=2;++old) for(int next=0;next<=2;++next) {
        AvatarMaterialFlags f{bool(bits&1),bool(bits&2),bool(bits&4)};
        m.Initialize(old);m.ApplyDetail(next,old,f);
        Need(m.Mode==(bits?std::max(1,next):old),"all source flag branches promote/bypass old-global");
        Need(m.SpecialBlend==(m.Mode==2 && bool(bits&3)),"source blend bit independently selects A/B");
        const std::array<float,4> white{{1,1,1,1}},zero{{0,0,0,0}};
        const auto expected=f.A?std::array<float,4>{{17.f/255.f,1,241.f/255.f,128.f/255.f}}:
            f.C?std::array<float,4>{{17.f/255.f,1,128.f/255.f,128.f/255.f}}:
            f.B?std::array<float,4>{{248.f/255.f,248.f/255.f,1,128.f/255.f}}:white;
        Need(m.Diffuse==expected && m.Ambient==expected,"source flag color precedence A/C/B");
        Need(m.Specular==(bits?expected:zero) && m.Emissive==(bits?expected:zero),"source self-lighting/material colors are distinct");
        m.ApplyFlags({},next);
        Need(m.Mode==next && !m.SpecialBlend && m.Emissive==zero,"clearing flags restores current global/material");
    }
    Need(AvatarFlagsFromPersonWords(1,2,0)==AvatarMaterialFlags{true,false,true},"source word614/620 mask");
    Need(AvatarFlagsFromPersonWords(2,4,1)==AvatarMaterialFlags{true,false,false},"source634 bit0 and no unrelated bits");
}
static void Lifetimes() {
    Mesh shared;Avatar one(nullptr),two(nullptr);
    one.Bindings={Part(&shared),Part(&shared)};two.Bindings={Part(&shared)};
    one.ApplyOriginalCharacterDetail(2,false);two.ApplyOriginalCharacterDetail(2,false);
    one.ApplyOriginalCharacterDetail(0,true);one.ApplyOriginalCharacterDetail(1,true);
    Need(one.Bindings[0]->OriginalMaterial.Mode==0 && one.Bindings[1]->OriginalMaterial.Mode==0,
        "commands reach every part without drawing or visibility");
    Need(two.Bindings[0]->OriginalMaterial.Mode==2,"shared content mesh does not share material history");
    one.ApplyOriginalCharacterDetail(1,false);
    Need(one.Bindings[0]->OriginalMaterial.Mode==0,"ordinary frame/global bind does not replay material commands");
    one.Bindings.push_back(Part(&shared));one.RebuildOriginalMaterials();
    for(const auto& b:one.Bindings) Need(b->OriginalMaterial.Mode==1,"appearance rebuild reapplies current global to every part");
    Avatar fresh(nullptr);fresh.Bindings={Part(&shared)};fresh.ApplyOriginalCharacterDetail(1,false);
    Need(fresh.Bindings[0]->OriginalMaterial.Mode==1,"fresh actor/world initializes from stored preference, not old history");
    Avatar preview(one);
    Need(preview.OriginalCharacterDetail<0 && preview.Bindings[0]->OriginalMaterial.Mode<0,
        "portrait copy does not import world material history");
    one.ApplyOriginalMaterialFlags({true,false,false});
    Need(one.Bindings[0]->OriginalMaterial.Flags.A,"live actor flag transition reaches existing bindings");
    one.ApplyOriginalMaterialFlags({});
    Need(one.Bindings[0]->OriginalMaterial.Mode==1,"flag-clear command uses current global");
}
static void Raster() {
    Skeleton skeleton;auto bone=std::make_unique<Bone>();bone->Name="ROOT";bone->ParentName="NULL";
    skeleton.Bones.push_back(std::move(bone));Avatar actor(&skeleton);
    Mesh mesh;
    for(auto p:std::vector<Vector3>{{-.8f,0,0},{.8f,0,0},{0,1.6f,0}}) {
        VitaboyVertex v;v.Position=p;v.TextureCoordinate={.5f,.5f};mesh.VertexBuffer.push_back(v);
    }
    mesh.IndexBuffer={0,1,2};Texture texture;texture.Width=texture.Height=1;texture.Paletted=false;
    texture.Pixels={0xff3264c8};actor.Bindings={Part(&mesh,&texture)};
    WorldCamera camera;camera.ViewDimensions={96,96};
    AvatarDrawOptions opt;opt.Cull=AvatarCullMode::None;opt.DepthTest=opt.DepthWrite=false;
    opt.OriginalMaterials=true;opt.Tint=st::Color(64,128,192,255);
    std::vector<uint8_t> color(96*96*4);Framebuffer frame(96,96,color.data());ZBuffer depth;
    actor.ApplyOriginalCharacterDetail(0,false);
    auto result=DrawAvatar(frame,depth,camera,actor,{0,0,0},float(M_PI),opt);
    Need(result.Pixels>0,"real avatar raster has covered pixels");
    size_t at=0;while(at<color.size() && color[at+3]==0)at+=4;
    Need(at<color.size() && color[at]==200 && color[at+1]==100 && color[at+2]==50,
        "source mode0 texture selection bypasses diffuse tint");
    const auto unlit=color;actor.Bindings[0]->OriginalMaterial.Mode=1;
    std::fill(color.begin(),color.end(),0);DrawAvatar(frame,depth,camera,actor,{0,0,0},float(M_PI),opt);
    Need(color[at]==50 && color[at+1]==50 && color[at+2]==37,"mode1 retains existing lit fallback");
    auto lit=color;actor.Bindings[0]->OriginalMaterial.Mode=2;
    std::fill(color.begin(),color.end(),0);DrawAvatar(frame,depth,camera,actor,{0,0,0},float(M_PI),opt);
    Need(color==lit && color!=unlit,"source1/2 share modulation selection, sampling stays separate");
    opt.OriginalMaterials=false;actor.Bindings[0]->OriginalMaterial.Mode=0;
    std::fill(color.begin(),color.end(),0);DrawAvatar(frame,depth,camera,actor,{0,0,0},float(M_PI),opt);
    Need(color==lit,"legacy/portable/preview path preserves existing shader");
}
int main() {try {SourceCommands();Lifetimes();Raster();std::puts("avatar material contract passed");return 0;}
catch(const std::exception& e){std::fprintf(stderr,"avatar material contract: %s\n",e.what());return 1;}}
