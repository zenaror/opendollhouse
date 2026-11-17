/* SPDX-License-Identifier: MPL-2.0 */
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <new>
// Data-free fixture access to the actual cache/compositor, no alternate renderer.
#define private public
#include "world/World.h"
#include "world/components/ObjectComponent.h"
#undef private
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/SPR2.h"
#include "files/formats/iff/chunks/DGRP.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "world/utils/TriangleRaster.h"
#include "simantics/VM.h"
#include "simantics/entities/VMGameObject.h"

using namespace FSO::LotView;
using namespace FSO::LotView::Utils;
using namespace FSO::LotView::Components;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
// Controlled allocation failure only in this data-free executable. Warm all
// sprite/cache allocations, then fail the exact bit-plane allocation once.
static size_t failAllocationBytes = 0;
static bool failedAllocation = false;
void* operator new(size_t bytes) {
    if (failAllocationBytes && bytes == failAllocationBytes) {
        failAllocationBytes = 0; failedAllocation = true; throw std::bad_alloc();
    }
    if (auto* p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int Maximum(const std::vector<uint32_t>& a,const std::vector<uint32_t>& b){
    Check(a.size()==b.size(),"comparison dimensions");
    int maximum=0;
    for(size_t i=0;i<a.size();++i)for(int c=0;c<3;++c)
        maximum=std::max(maximum,std::abs(int((a[i]>>(8*c))&255)-int((b[i]>>(8*c))&255)));
    return maximum;
}
struct Fixture {
    IffFile iff;
    std::array<st::Color,2> colors{{{200,80,40},{40,160,80}}};
    SPR2 spr;
    DGRP group;
    ObjectComponent object{nullptr};
    Model::Blueprint blueprint{4,2};
    WorldState state{4,2,nullptr};
    _2DWorldBatch batch;
    World2D world;
    bool mixed;
    Fixture(bool palette,bool mix):mixed(mix){
        spr.ChunkID=100;spr.ChunkProcessed=true;iff.AddChunk(&spr);
        for(int i=0;i<2;++i){
            auto frame=std::make_unique<SPR2Frame>(&spr);
            frame->Width=frame->Height=1;frame->ZBufferData={128};
            const uint8_t alpha=i==0&&mixed?255:128;
            if(palette){frame->PalColors=colors.data();frame->PalData={uint8_t(i)};frame->AlphaData={alpha};}
            else{auto c=colors[i];c.A=alpha;frame->PixelData={c};}
            spr.Frames.push_back(std::move(frame));
        }
        group.ChunkParent=&iff;group.Images.emplace_back(&group);
        group.Images[0].Direction=1;group.Images[0].Zoom=3;
        for(int i=0;i<(mixed?2:1);++i){
            group.Images[0].Sprites.emplace_back(&group);
            auto& s=group.Images[0].Sprites.back();s.SpriteID=100;s.SpriteFrameIndex=i;
            s.Flags=i?DGRPSpriteFlags::Luminous:DGRPSpriteFlags(0);
        }
        object.DGRP=&group;object.Room=7;object.Level=1;object.TileX=object.TileY=0;
        object.blueprint=&blueprint;blueprint.Objects.push_back(&object);
        blueprint.Light.resize(8);
        std::fill(blueprint.RoomColors.begin(),blueprint.RoomColors.end(),st::Color::White());
        blueprint.RoomColors[7]={64,64,64};
        state._2D=&batch;state.WorldSpace.CadgeWidth=0;state.WorldSpace.CadgeBaseLine=1;
        // The production TopLeft anchor adds 32px at Near zoom. This one-pixel
        // fixture has a two-pixel viewport, so remove that presentation offset.
        state.WorldSpace.TilePxHeightHalf=0;
        batch.AmbientLight=blueprint.RoomColors.data();batch.OutputDepth=true;
        world.Init(&blueprint);world.SCROLL_BUFFER=0;world.Composite.Resize(4,2);
        auto offset=state.WorldSpace.GetScreenOffset();
        world.Origin=world.GetBufferOrigin({-offset.X,-offset.Y});
        world.AmbientLight=blueprint.RoomColors.data();world.OutsideLight=st::Color::White();
        for(auto* layer:{&world.StaticFloor,&world.StaticWall}){
            layer->Buffer.Resize(4,2);layer->Buffer.EnableRoom(true);layer->Buffer.Clear(0);layer->Valid=true;
        }
        world.RenderLayer(World2D::OBJECTS,state,nullptr);
        world.HaveOrigin=world.CompositeValid=true;
    }
    Framebuffer Direct(uint32_t background){
        Framebuffer f(4,2);f.Clear(background);batch.Target=&f;
        batch.SetScroll(world.Origin);batch.OffsetPixel({0,0});batch.OffsetTile({0,0,0});
        batch.Begin(state.Camera());object.Draw(state);batch.End();batch.Target=nullptr;return f;
    }
    Framebuffer Cached(uint32_t background){
        Framebuffer f(4,2);f.Clear(background);
        world.MergeLayer(world.StaticObjects.Buffer,f,{0,0,4,2});return f;
    }
};
void CacheCases(bool palette,bool mixed,uint32_t background){
    Fixture f(palette,mixed);
    auto direct=f.Direct(background),cached=f.Cached(background);
    Check(std::any_of(direct.Color.begin(),direct.Color.end(),[&](auto p){return p!=background;}),
          "fixture sprite did not cover its viewport");
    const int maximum=Maximum(direct.Color,cached.Color);
    // Same-domain deferred tint and source-over use different integer rounding
    // order. The measured single-domain nonblack case has a one-channel bound;
    // exact agreement is also valid. Other rows require exact agreement.
    const int expected=!mixed&&background!=0xff000000u?1:0;
    std::cout<<"cache palette="<<palette<<" mixed="<<mixed<<" background="<<background
             <<" maximum_channel_delta="<<maximum<<" expected="<<expected<<'\n';
    Check(maximum<=expected,"cache alpha/domain result");
    Check(f.world.StaticObjects.Buffer.HasPrelit==mixed,"mixed layer dependency");
    const auto replay=f.world.PrelitReplays;
    f.world.PreDraw(f.state);
    auto redraws=f.world.StaticRedraws;
    f.world.PreDraw(f.state);
    Check(f.world.StaticRedraws==redraws&&f.world.PrelitReplays==(mixed?replay+1:replay),
          "unchanged palette rerasterized cache");
    f.blueprint.RoomColors[7]={96,48,160}; // no damage: compare the actual palette
    f.world.PreDraw(f.state);
    Check(f.world.StaticRedraws-redraws==(mixed?1:0),"palette did not selectively refresh mixed layer");
    Check(Maximum(f.Direct(background).Color,f.Cached(background).Color)<=1,"changed palette cache output");
    f.state.RenderingThumbnail=true;f.world.PreDraw(f.state);
    Check(Maximum(f.Direct(background).Color,f.Cached(background).Color)<=1,"white thumbnail cache output");
    f.state.RenderingThumbnail=false;f.world.PreDraw(f.state);
    Check(Maximum(f.Direct(background).Color,f.Cached(background).Color)<=1,"thumbnail return cache output");
    if(mixed){
        f.group.Images[0].Sprites.erase(f.group.Images[0].Sprites.begin()+1,f.group.Images[0].Sprites.end());
        f.object.dgrp.InvalidateTextures();
        const st::Rectangle clip(-int(f.world.Origin.X),-int(f.world.Origin.Y),1,1);
        f.world.RenderLayer(World2D::OBJECTS,f.state,&clip);
        const auto p=size_t(clip.Y)*4+clip.X;
        Check(!f.world.StaticObjects.Buffer.IsPrelit(p),"partial redraw kept stale mixed bit");
        Check(f.world.StaticObjects.Buffer.HasPrelit,"partial redraw erased conservative layer dependency");
        Check(Maximum(f.Direct(background).Color,f.Cached(background).Color)<=1,"partial redraw cache output");
    }
}
void Metadata(){
    Framebuffer f(8,3);f.EnableRoom(true);f.Clear(0);f.MarkPrelit(10);
    World2D::ShiftBuffer(f,2,1);
    Check(f.IsPrelit(20),"scroll did not move prelit bit");
    f.ClearRect(0,0,2,1,0);Check(f.IsPrelit(20)&&f.HasPrelit,"clip erased outside dependency");
    f.ClearRect(4,2,5,3,0);Check(!f.IsPrelit(20)&&f.HasPrelit,"clip clear/dependency mismatch");
    f.Clear(0);Check(!f.HasPrelit&&f.Prelit.empty(),"full clear did not reset mask");
    f.MarkPrelit(0);f.Resize(9,3);Check(!f.HasPrelit&&f.Prelit.empty(),"resize kept old mask");
    f.MarkPrelit(0);f.ResolvePrelit=true;f.Clear(0);
    Check(f.IsPrelit(0),"replay erased discovered mask");f.ResolvePrelit=false;
    f.PrelitAll=f.HasPrelit=true;f.ClearRect(0,0,1,1,0);
    Check(f.IsPrelit(0),"allocation fallback lost its cached light dependency");
}
void Triangle(){
    Framebuffer f(4,4);f.EnableRoom(true);f.Clear(0);
    std::vector<st::Color> colors(65536,st::Color::White());colors[7]={64,64,64};
    f.CacheLight=colors.data();f.MarkPrelit(0);f.ResolvePrelit=true;f.Clear(0);
    RasterVertex a,b,c;a.x=a.y=0;b.x=4;b.y=0;c.x=0;c.y=4;
    DrawTriangle(f,a,b,c,nullptr,Framebuffer::Pack(200,80,40,255),0,nullptr,7);
    Check(f.Color[0]==Framebuffer::Pack(50,20,10,255)&&f.IsPrelit(0),"triangle replay light");
    f.ResolvePrelit=false;f.Clear(0);f.MarkPrelit(0);
    DrawTriangle(f,a,b,c,nullptr,Framebuffer::Pack(200,80,40,255),0,nullptr,7);
    Check(!f.IsPrelit(0),"opaque triangle did not supersede mixed pixel");
}
void SaturatedOutsideTint(){
    Framebuffer layer(4,4),direct(4,4),cached(4,4);
    layer.EnableRoom(true);layer.Clear(0);direct.Clear(0xff000000u);cached.Clear(0xff000000u);
    std::vector<st::Color> lights(65536,st::Color::White());
    direct.CacheLight=lights.data();direct.CacheOutsideLight={128,128,128};direct.DeferredOutsideTint=true;
    RasterVertex a,b,c;a.x=a.y=0;b.x=4;b.y=0;c.x=0;c.y=4;
    a.r=b.r=c.r=1.5f;a.g=b.g=c.g=1.7f;a.b=b.b=c.b=0.8f;
    const auto color=Framebuffer::Pack(200,200,200,255);
    DrawTriangle(layer,a,b,c,nullptr,color);
    DrawTriangle(direct,a,b,c,nullptr,color);
    World2D merger;merger.AmbientLight=lights.data();merger.OutsideLight={128,128,128};
    merger.MergeLayer(layer,cached,{0,0,4,4});
    Check(direct.Color==cached.Color,"saturated outside tint changed clamp order inside replay crop");
    Check(direct.Color[0]==Framebuffer::Pack(128,128,80,255),"saturation fixture did not exercise post-clamp tint");
}
void OutsideProducerOrder(){
    Model::Blueprint blueprint(4,4);WorldState state(256,256,nullptr);_2DWorldBatch batch;
    state._2D=&batch;state.CenterTile({1.5f,1.5f});state.OutsideColor={128,128,128};
    auto offset=state.WorldSpace.GetScreenOffset();batch.SetScroll({-offset.X,-offset.Y});
    TerrainComponent terrain({0,0,3,3});terrain.RegenTerrain(state,&blueprint);
    terrain.LightGreen=terrain.LightBrown=terrain.DarkGreen=terrain.DarkBrown={240,240,240};
    std::vector<float> heights;for(int y=0;y<4;++y)for(int x=0;x<4;++x)heights.push_back(x*0.3f);
    terrain.SetHeights(heights,4);
    RoofComponent roof;roof.blueprint=&blueprint;roof.ShapeDirty=roof.StyleDirty=false;
    roof.Mips[0].Width=roof.Mips[0].Height=1;roof.Mips[0].Pixels={Framebuffer::Pack(200,200,200,255)};
    RoofComponent::RoofQuad quad{};quad.Face=0;quad.Fascia=false;
    quad.V[0]={{0,0,0},0,0,2};quad.V[1]={{2,0,0},1,0,2};
    quad.V[2]={{2,2,0},1,1,2};quad.V[3]={{0,2,0},0,1,2};
    roof.Meshes.resize(1);roof.Meshes[0].push_back(quad);
    std::vector<st::Color> lights(65536,st::Color::White());
    for(bool isRoof:{false,true}){
        Framebuffer layer(256,256),direct(256,256),cached(256,256);
        layer.EnableRoom(true);layer.Clear(0);direct.Clear(0xff000000u);cached.Clear(0xff000000u);
        direct.CacheLight=lights.data();direct.CacheOutsideLight={128,128,128};direct.DeferredOutsideTint=true;
        if(isRoof){roof.DrawInto(state,layer);roof.DrawInto(state,direct);}
        else{terrain.DrawInto(state,layer);terrain.DrawInto(state,direct);}
        World2D merger;merger.AmbientLight=lights.data();merger.OutsideLight={128,128,128};
        merger.MergeLayer(layer,cached,{0,0,256,256});
        Check(std::any_of(layer.Color.begin(),layer.Color.end(),[](auto p){return (p&255)==255&&(p>>24)==255;}),
              "outside producer fixture did not saturate its white-light raster");
        Check(direct.Color==cached.Color,"terrain/roof crop changed clamp/tint order");
    }
}
Framebuffer Reference(Fixture& f,uint32_t background=0xff000000u){
    Framebuffer target(f.world.Composite.Width,f.world.Composite.Height);target.Clear(background);
    f.batch.Target=&target;f.batch.SetScroll(f.world.Origin);
    if(f.blueprint.Terrain)f.blueprint.Terrain->DrawInto(f.state,target);
    f.batch.Begin(f.state.Camera());
    for(auto* obj:f.blueprint.Objects){
        if(obj->Level>f.state.Level())continue;
        auto p=obj->DrawPosition();f.batch.OffsetPixel(f.state.WorldSpace.GetScreenFromTile(p));
        f.batch.OffsetTile(p);obj->Draw(f.state);
    }
    f.batch.End();f.batch.Target=nullptr;return target;
}
void Advance(Fixture& f){
    for(auto* obj:f.blueprint.Objects)obj->Update(f.state);
    f.world.PreDraw(f.state);
}
void CheckResolved(Fixture& f,const char* why){
    Check(f.world.Composite.Color==Reference(f).Color,why);
    Framebuffer shown(f.world.Composite.Width,f.world.Composite.Height);
    f.world.Draw(f.state,shown,true);
    Check(shown.Color==Reference(f).Color,"resolved group was drawn twice or lost coverage");
    const auto saved=shown.Color;
    Advance(f);f.world.Draw(f.state,shown,true);
    Framebuffer full(shown.Width,shown.Height);f.world.Draw(f.state,full,false);
    Check(shown.Color==saved&&shown.Color==full.Color,"resolved retained/full redraw mismatch");
}
void EnableOriginalAmbient(Fixture& f){
    auto& light=f.blueprint.VisualLight;light.Enabled=true;light.Width=4;light.Height=2;
    light.Generation=light.GeometryGeneration=1;light.Maps.emplace_back(8,uint32_t(1)|(uint32_t(1)<<16));
    light.Tones.assign(4,16);light.Grid.assign(8,0);
}
void OriginalObjectPosition(){
    Fixture f(false,false);
    auto& light=f.blueprint.VisualLight;light.Enabled=true;light.Width=light.Height=8;
    const auto whole=[](unsigned room){return room|(room<<16);};
    light.Maps.assign(1,std::vector<uint32_t>(64,whole(1)));light.Tones={16,16,20,20,16,16,25,25};
    light.Maps[0][3*8+3]=2|(3<<16);light.Maps[0][3*8+2]=whole(2);light.Maps[0][3*8+4]=whole(3);
    light.Maps[0][2*8+3]=whole(2);light.Maps[0][4*8+3]=whole(3);
    std::vector<std::vector<Model::WallTile>> walls(1,std::vector<Model::WallTile>(64));
    walls[0][3*8+3].Segments=Model::WallSegments(16);
    f.blueprint.Width=f.blueprint.Height=8;f.blueprint.Walls=&walls;
    OBJD definition;definition.ObjectType=OBJDType::Normal;FSO::Content::GameObject gameObject;gameObject.OBJ=&definition;
    // A real VMEntity needs a resource owner even with empty behavior tables.
    FSO::Content::GameObjectResource resource(&f.iff,nullptr,nullptr,"visual-object-position-fixture",true);
    gameObject.Resource=&resource;
    FSO::SimAntics::VM::UseWorld(true);
    FSO::SimAntics::VMGameObject entity(&gameObject,new ObjectComponent(&gameObject));
    auto& object=*static_cast<ObjectComponent*>(entity.WorldUI.get());object.DGRP=&f.group;object.Room=7;object.blueprint=&f.blueprint;
    f.batch.VisualLight=&light;
    const auto draw=[&](int rawX,int rawY,int expected){
        entity.Position(Model::LotTilePos(int16_t(rawX),int16_t(rawY),1));
        Framebuffer target(4,2);target.Clear(0xff000000u);f.batch.Target=&target;
        f.batch.SetScroll({0,0});f.batch.OffsetPixel({0,0});f.batch.OffsetTile({0,0,0});
        f.batch.Begin(f.state.Camera());object.Draw(f.state);f.batch.End();f.batch.Target=nullptr;
        Check(object.dgrp.Items[0].Sprite.VisualSample==expected,"object lighting did not sample the raw VM subtile/side");
        return target.Color;
    };
    const auto low=draw(52,52,FSO::Common::TS1::VisualLighting::FirstKey+5);
    const auto high=draw(56,56,FSO::Common::TS1::VisualLighting::FirstKey+7);
    Check(low!=high,"subtile fixture did not change actual raster output");
    draw(64,52,FSO::Common::TS1::VisualLighting::FirstKey+6);
    f.group.Images[0].Sprites[0].ObjectOffset.X=16;f.group.Images[0].Sprites[0].SpriteOffset.X=7;
    object.dgrp.InvalidateTextures();draw(52,52,FSO::Common::TS1::VisualLighting::FirstKey+5);
}
void OriginalAmbientCache(){
    for(bool palette:{false,true})for(bool mixed:{false,true})for(bool opaque:{false,true}){
        Fixture f(palette,mixed);
        if(opaque){auto& frame=*f.spr.Frames[0];if(palette)frame.AlphaData[0]=255;else frame.PixelData[0].A=255;}
        EnableOriginalAmbient(f);auto& light=f.blueprint.VisualLight;
        Advance(f);CheckResolved(f,"TS1 source palette versus retained composition");
        const auto initial=f.world.Composite.Color;
        Check(initial!=std::vector<uint32_t>(initial.size(),f.world.ClearColor),"TS1 fixture must draw visible pixels");
        const auto rasters=f.world.StaticRedraws;
        light.Tones[2]=light.Tones[3]=25;++light.Generation;
        Advance(f);CheckResolved(f,"TS1 actual light change without damage");
        Check(f.world.StaticRedraws==rasters,"ambient relight rerasterized an unlit static layer");
        Check(f.world.Composite.Color!=initial,"TS1 relight did not reach displayed pixels");
        const auto changed=f.world.Composite.Color;
        f.state.ObjectLighting=false;f.blueprint.Damage.emplace_back(Model::BlueprintDamageType::LIGHTING_CHANGED);
        Advance(f);CheckResolved(f,"F5 must preserve ordinary ambient");
        Check(f.world.Composite.Color==changed,"F5 changed an ambient-only source");
        f.state.RenderingThumbnail=true;Advance(f);CheckResolved(f,"TS1 to existing neutral thumbnail mode");
        f.state.RenderingThumbnail=false;Advance(f);CheckResolved(f,"thumbnail to TS1 ambient return");
        Check(f.world.Composite.Color==changed,"thumbnail roundtrip lost TS1 cached material");
        const auto stableRasters=f.world.StaticRedraws,stableReplays=f.world.CompositeReplays;
        Advance(f);Check(f.world.StaticRedraws==stableRasters&&f.world.CompositeReplays==stableReplays,
            "stable TS1 frame replayed retained geometry");
    }
    std::cout<<"TS1 ambient retained/full and static source-cache roundtrips passed\n";
}
void ResolvedGroups(bool original=false){
    Fixture f(true,true);
    if(original)EnableOriginalAmbient(f);
    ObjectComponent other(nullptr);other.DGRP=&f.group;other.Room=original?65535:6;other.Level=1;
    other.TileX=other.TileY=0;other.blueprint=&f.blueprint;
    other.renderInfo.Layer=WorldObjectRenderLayer::DYNAMIC;other.ForceDynamic=true;
    f.blueprint.RoomColors[6]={160,96,48};f.blueprint.Objects.push_back(&other);
    Advance(f);CheckResolved(f,"mixed static/dynamic group does not follow source order");
    Check(other.renderInfo.Composited&&f.object.renderInfo.Composited,"group bounds were not included");
    auto replays=f.world.CompositeReplays;
    for(int i=0;i<20;++i)Advance(f);
    Check(f.world.CompositeReplays==replays,"stable dynamic group was replayed every frame");

    f.blueprint.Damage.clear();other.CutawayHidden(true);
    Check(f.blueprint.Damage.empty(),"dynamic cutaway fixture unexpectedly emitted object damage");
    Advance(f);CheckResolved(f,"dynamic cutaway without damage retained stale pixels");
    other.CutawayHidden(false);Advance(f);CheckResolved(f,"dynamic cutaway return");
    other.Position={1.0f/64,0,0};Advance(f);CheckResolved(f,"moving member left old or omitted new pixels");
    other.Position={0,0,0};Advance(f);
    auto before=f.world.Composite.Color;
    std::reverse(f.blueprint.Objects.begin(),f.blueprint.Objects.end());
    Advance(f);CheckResolved(f,"object order change was not recomposed");
    Check(before!=f.world.Composite.Color,"order fixture did not change its translucent output");

    f.blueprint.Objects.erase(std::find(f.blueprint.Objects.begin(),f.blueprint.Objects.end(),&other));
    Advance(f);CheckResolved(f,"removed member retained pixels");
    f.blueprint.Objects.push_back(&other);Advance(f);CheckResolved(f,"new member omitted");
    other.Room=65533;f.blueprint.RoomColors[65533]=st::Color::Transparent();
    Advance(f);CheckResolved(f,"gray sentinel acquired dark unused-room tint in direct crop");
    Check(original||f.world.Composite.Color[2]==Framebuffer::Pack(113,113,113,255),
          "gray sentinel did not preserve its unlit grayscale sprites");
    Check(f.world.Composite.Color!=std::vector<uint32_t>(f.world.Composite.Color.size(),0xff000000u),
          "gray sentinel fixture drew nothing");
    other.Room=original?65535:6;Advance(f);
    f.state.RenderingThumbnail=true;f.state.ThumbnailGroundBounds={1,1,1,1};
    Advance(f);CheckResolved(f,"thumbnail bounds did not remove group");
    f.state.ThumbnailGroundBounds={0,0,1,1};Advance(f);CheckResolved(f,"thumbnail bounds return");
    f.state.RenderingThumbnail=false;Advance(f);CheckResolved(f,"thumbnail lighting return");

    f.state.SetDimensions({6,2});Advance(f);CheckResolved(f,"resized group coverage");
    f.state.CenterTile({0.25f,0});Advance(f);CheckResolved(f,"scroll omitted old/new group bounds");
    f.state.CenterTile({0,0});Advance(f);CheckResolved(f,"scroll return");
}
void InterleavedDepth(bool original=false){
    Fixture f(false,true);
    if(original)EnableOriginalAmbient(f);
    f.state.SetDimensions({64,64});f.state.WorldSpace.Invalidate();f.state.SimpleTerrain=true;
    std::fill(f.blueprint.RoomColors.begin(),f.blueprint.RoomColors.end(),st::Color::White());
    f.spr.Frames[0]->PixelData[0]={200,40,20,255};
    f.spr.Frames[1]->PixelData[0]={20,200,40,128};
    auto& far=f.group.Images[0].Sprites[0];far.ObjectOffset.Y=-48;far.SpriteOffset={-192,96};
    auto& near=f.group.Images[0].Sprites[1];near.ObjectOffset.Y=-16;near.SpriteOffset={-64,32};
    f.object.dgrp.InvalidateTextures();
    f.blueprint.Terrain.reset(new TerrainComponent({0,0,2,2}));
    auto& terrain=*f.blueprint.Terrain;
    terrain.LightGreen=terrain.LightBrown=terrain.DarkGreen=terrain.DarkBrown={30,60,90};
    terrain.RegenTerrain(f.state,&f.blueprint);
    f.world.Invalidate();Advance(f);
    const size_t pixel=size_t(63)*64+32;
    const auto external=f.world.StaticFloor.Buffer.Depth[pixel];
    const auto foreground=f.world.StaticObjects.Buffer.Depth[pixel];
    Framebuffer farOnly(64,64);farOnly.Clear(0);f.batch.Target=&farOnly;
    f.batch.SetScroll(f.world.Origin);f.batch.OffsetPixel({0,0});f.batch.OffsetTile({0,0,0});
    f.batch.Begin(f.state.Camera());f.batch.Draw(&f.object.dgrp.Items[0].Sprite);f.batch.End();f.batch.Target=nullptr;
    Check(std::isfinite(external)&&farOnly.Depth[pixel]>external&&external>foreground,
          "depth fixture did not place external terrain between two object fragments");
    Framebuffer flattened(64,64);flattened.Clear(0xff000000u);
    f.world.MergeLayer(f.world.StaticFloor.Buffer,flattened,{0,0,64,64});
    f.world.MergeLayer(f.world.StaticObjects.Buffer,flattened,{0,0,64,64});
    const auto reference=Reference(f);
    std::cout<<"depth interleave ts1="<<original<<" flattened_max="<<Maximum(flattened.Color,reference.Color)
             <<" resolved_max="<<Maximum(f.world.Composite.Color,reference.Color)<<'\n';
    Check(flattened.Color!=reference.Color,"flattened depth fixture did not demonstrate loss");
    Check(f.world.Composite.Color==reference.Color,"direct crop did not resolve interleaved depth");
    const auto before=f.world.Composite.Color;
    f.object.renderInfo.Layer=WorldObjectRenderLayer::DYNAMIC;f.object.ForceDynamic=true;
    f.world.Invalidate();Advance(f);
    Check(f.world.Composite.Color==before,"interleaved depth depends on static/dynamic membership");
}
void LocalMemberDamage(bool original=false){
    Fixture f(false,false);
    if(original)EnableOriginalAmbient(f);
    f.state.SetDimensions({128,64});
    auto background=std::make_unique<SPR2Frame>(&f.spr);
    background->Width=background->Height=1;background->ZBufferData={128};
    background->PixelData={{30,60,90,255}};f.spr.Frames.push_back(std::move(background));
    DGRP backGroup;backGroup.ChunkParent=&f.iff;backGroup.Images.emplace_back(&backGroup);
    backGroup.Images[0].Direction=1;backGroup.Images[0].Zoom=3;
    backGroup.Images[0].Sprites.emplace_back(&backGroup);
    auto& backSprite=backGroup.Images[0].Sprites[0];backSprite.SpriteID=100;backSprite.SpriteFrameIndex=2;
    // Same screen pixel, a genuinely farther source depth.
    backSprite.ObjectOffset.Y=-16;backSprite.SpriteOffset={-64,0};
    ObjectComponent back(nullptr);back.DGRP=&backGroup;back.Room=6;back.Level=1;
    back.TileX=back.TileY=0;back.blueprint=&f.blueprint;
    f.blueprint.Objects.insert(f.blueprint.Objects.begin(),&back);
    f.object.renderInfo.Layer=WorldObjectRenderLayer::DYNAMIC;f.object.ForceDynamic=true;
    f.world.Invalidate();Advance(f);CheckResolved(f,"local movement baseline");
    const auto alone = [&](ObjectComponent& obj) {
        Framebuffer target(128,64);target.Clear(0);f.batch.Target=&target;
        f.batch.SetScroll(f.world.Origin);auto p=obj.DrawPosition();
        f.batch.OffsetPixel(f.state.WorldSpace.GetScreenFromTile(p));f.batch.OffsetTile(p);
        f.batch.Begin(f.state.Camera());obj.Draw(f.state);f.batch.End();f.batch.Target=nullptr;
        return target;
    };
    const auto backOnly=alone(back),frontOnly=alone(f.object);
    const size_t covered=size_t(31)*128+64;
    Check((backOnly.Color[covered]>>24)==255 && (frontOnly.Color[covered]>>24)>2 &&
          (frontOnly.Color[covered]>>24)<255 && std::isfinite(backOnly.Depth[covered]) &&
          std::isfinite(frontOnly.Depth[covered]) && backOnly.Depth[covered]>frontOnly.Depth[covered],
          "local fixture did not overlap a translucent foreground and farther opaque source");
    int64_t fullArea=0;for(const auto& r:f.world.ResolveRegions)fullArea+=int64_t(r.Width)*r.Height;
    Check(fullArea>128,"local damage fixture has no larger retained group");
    auto pixels=f.world.CompositeReplayPixels;
    f.object.Position={1.0f/64,0,0};Advance(f);
    auto localPixels=f.world.CompositeReplayPixels-pixels;
    const auto moved=f.world.Composite.Color;
    CheckResolved(f,"local movement differs from complete source rendering");
    f.world.RenderRoomColors.clear();Advance(f);
    Check(f.world.Composite.Color==moved,"local movement differs from full resolved rebuild");
    Check(localPixels>0&&localPixels<fullArea/4,"movement replay did not remain local");

    DGRP animation;animation.ChunkParent=&f.iff;animation.Images.emplace_back(&animation);
    animation.Images[0].Direction=1;animation.Images[0].Zoom=3;
    animation.Images[0].Sprites.emplace_back(&animation);
    animation.Images[0].Sprites[0].SpriteID=100;animation.Images[0].Sprites[0].SpriteFrameIndex=1;
    pixels=f.world.CompositeReplayPixels;
    f.object.DGRP=&animation;Advance(f);
    const auto animationPixels=f.world.CompositeReplayPixels-pixels;
    const auto animated=f.world.Composite.Color;
    CheckResolved(f,"local graphic change differs from complete source rendering");
    f.world.RenderRoomColors.clear();Advance(f);
    Check(f.world.Composite.Color==animated,"local graphic change differs from full resolved rebuild");
    Check(animationPixels>0&&animationPixels<fullArea/4,"animation replay did not remain local");
    pixels=f.world.CompositeReplayPixels;const auto passes=f.world.CompositeReplays;
    back.Room=7;f.object.Position={2.0f/64,0,0};Advance(f);
    const auto simultaneousPixels=f.world.CompositeReplayPixels-pixels;
    Check(f.world.CompositeReplays-passes==1,"simultaneous local changes caused separate geometry passes");
    Check(simultaneousPixels>0&&simultaneousPixels<fullArea/4,"combined member damage expanded to full group");
    CheckResolved(f,"combined local changes differ from full source order");
    std::cout<<"resolved local movement_pixels="<<localPixels<<" animation_pixels="<<animationPixels
             <<" simultaneous_pixels="<<simultaneousPixels<<" full_group_pixels="<<fullArea<<'\n';
}
void GroupAllocationFallback(bool original=false){
    Fixture f(false,true);if(original)EnableOriginalAmbient(f);Advance(f);
    std::vector<World2D::ObjectView>().swap(f.world.FrameObjects);
    bool changed=false;
    failedAllocation=false;failAllocationBytes=sizeof(World2D::ObjectView)*f.blueprint.Objects.size();
    f.world.CaptureObjects(f.state,changed);failAllocationBytes=0;
    Check(failedAllocation&&f.world.ObjectViewFallback,"visual-key allocation fault not exercised");
    f.world.PrepareResolvedGroups(changed);f.world.BuildComposite({0,0,4,2},f.state);
    Check(f.world.ResolveAllObjects&&f.world.Composite.Color==Reference(f).Color,"visual-key fallback output");
    f.world.RenderRoomColors.clear();Advance(f);CheckResolved(f,"visual-key fallback recovery");

    f.world.CaptureObjects(f.state,changed);
    std::vector<st::Rectangle>().swap(f.world.ResolveRegions);
    failedAllocation=false;failAllocationBytes=sizeof(st::Rectangle);
    f.world.PrepareResolvedGroups(changed);failAllocationBytes=0;
    Check(failedAllocation&&f.world.ResolveAllObjects,"region allocation fallback not exercised");
    f.world.BuildComposite({0,0,4,2},f.state);
    Check(f.world.Composite.Color==Reference(f).Color,"region allocation fallback output");

    auto& layer=f.world.StaticObjects.Buffer;
    std::vector<uint64_t>().swap(layer.Transparency);layer.TransparencyAll=false;
    failedAllocation=false;failAllocationBytes=sizeof(uint64_t);
    layer.MarkTransparency(0);failAllocationBytes=0;
    Check(failedAllocation&&layer.TransparencyAll,"coverage allocation fallback not exercised");
    f.world.CaptureObjects(f.state,changed);f.world.PrepareResolvedGroups(changed);
    f.world.BuildComposite({0,0,4,2},f.state);
    Check(f.world.Composite.Color==Reference(f).Color,"coverage allocation fallback output");
}
void AllocationFallback(){
    Fixture f(false,true);
    // A second ordinary opaque pixel lies outside the partial update. It must
    // also be replayed when allocation failure promotes the entire light mask.
    auto& image=f.group.Images[0];
    image.Sprites.emplace_back(&f.group);
    image.Sprites.back().SpriteID=100;image.Sprites.back().SpriteOffset={1,0};
    f.object.dgrp.InvalidateTextures();
    f.world.RenderLayer(World2D::OBJECTS,f.state,nullptr);
    auto& layer=f.world.StaticObjects.Buffer;
    layer.ResetPrelit();std::vector<uint64_t>().swap(layer.Prelit);
    failAllocationBytes=((layer.Color.size()+63)/64)*sizeof(uint64_t);
    const st::Rectangle clip(-int(f.world.Origin.X),-int(f.world.Origin.Y),1,1);
    f.world.RenderLayer(World2D::OBJECTS,f.state,&clip);
    failAllocationBytes=0;
    Check(failedAllocation&&layer.PrelitAll,"mask allocation fault was not exercised");
    Check(f.world.FullCompositeDamage,"partial fallback did not request full composite");
    Check(f.Direct(0xff000000u).Color==f.Cached(0xff000000u).Color,
          "allocation fallback left outside-clip unlit pixels marked lit");
    f.world.PreDraw(f.state);
    Check(!f.world.FullCompositeDamage&&f.world.Composite.Color==f.Direct(0xff000000u).Color,
          "promoted fallback composite was not completely rebuilt");
}
}
int main(){try{
    for(bool palette:{false,true})for(bool mix:{false,true})
        for(uint32_t bg:{0xff000000u,0xff3c2814u})CacheCases(palette,mix,bg);
    Metadata();Triangle();SaturatedOutsideTint();OutsideProducerOrder();AllocationFallback();ResolvedGroups();ResolvedGroups(true);GroupAllocationFallback();GroupAllocationFallback(true);InterleavedDepth();InterleavedDepth(true);LocalMemberDamage();LocalMemberDamage(true);OriginalAmbientCache();OriginalObjectPosition();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
