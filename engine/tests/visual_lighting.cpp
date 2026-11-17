// SPDX-License-Identifier: MPL-2.0
// Data-free checks through the real VM producer and DVD palette implementation.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "files/formats/iff/IffFile.h"
#include "world/model/Blueprint.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Entities;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Common::TS1;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
static void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}

static void Palette(){
    // Fixed packed16 vectors transcribed from the recovered palette contract;
    // these are derived static expectations, not captured DVD raster pixels.
    struct PaletteCase { st::Color Color; std::array<uint16_t,5> Expected; };
    const PaletteCase cases[]={
        {{123,201,87},{0x4326,0x5407,0x6509,0x7e2b,0x7e4a}},
        {{200,80,40},{0x6943,0x81a4,0xa205,0xca86,0xca85}},
        {{40,160,80},{0x1a86,0x1b27,0x2409,0x2ceb,0x2d0a}},
        {{7,3,7},{0x0001,0x0001,0x0001,0x0001,0x0000}},
        {{248,252,248},{0x8410,0xa514,0xce59,0xffdf,0xffff}}};
    const uint8_t tones[]={16,20,25,31,32};
    for(const auto& row:cases)for(int i=0;i<5;++i)
        Check(PackVisual565(row.Color,tones[i])==row.Expected[i],"fixed DVD palette transcription vector");
    Check(PackVisual565(st::Color::White(),32)==0xffff,"fullbright uses base palette");
    Check(PackVisual565(st::Color::White(),31)==0xffdf,"LUT31 is not base palette");
    Check(PackVisual565(st::Color::White(),16)==0x8410,"minimum ambient white");
    Check(PackVisual565(st::Color::Black(),16)==1,"original dim blue bias");
    Check(PackVisual565(st::Color::White(),VisualTone(1,2))==0xce59,"night visual cap");
    for(int period=0;period<4;++period)for(int byte=0;byte<256;++byte){
        const float f=std::clamp(float(byte*(1.0f/255.0f)),.5f,period==0?1.0f:.8f);
        const auto tone=VisualTone(float(byte*(1.0f/255.0f)),period);
        Check(tone==(std::abs(f-1)<=1e-6f?32:unsigned(double(f)*255+.5)>>3),"byte/period quantization");
        const auto c=PresentVisual565(st::Color(123,201,87,128),tone);
        Check(c.A==128,"palette must not transform coverage alpha");
    }
    Check(VisualTone(.999f,0)==31&&VisualTone(1,0)==32,"base epsilon independent of bucket");
    Check(DirectVisualFactor(0,true,false,1,0)==.8f,"pool direct factor");
    Check(DirectVisualFactor(0,false,true,0,1)==.8f,"occupied overhead factor");
    Check(DirectVisualFactor(0,false,true,1,1)==0,"zero-amount lamp suppresses overhead floor");
}

static void Producer(){
    VM::UseWorld(false);
    FSO::LotView::Model::Blueprint bp(7,7);
    VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.TS1=true;ctx.Blueprint=&bp;
    ctx.Architecture(std::make_unique<VMArchitecture>(7,7,&bp,&ctx));
    auto& arch=*ctx.Architecture();
    for(auto& story:arch.Walls)for(auto& wall:story){wall.TopLeftStyle=wall.TopRightStyle=1;wall.TopLeftPattern=wall.TopRightPattern=1;}
    for(auto& story:arch.Floors)for(auto& floor:story)floor.Pattern=1;
    for(int x=2;x<=4;++x){arch.Walls[0][2*7+x].Segments|=FSO::LotView::Model::WallSegments(2);arch.Walls[0][1*7+x].Segments|=FSO::LotView::Model::WallSegments(8);
        arch.Walls[0][4*7+x].Segments|=FSO::LotView::Model::WallSegments(8);arch.Walls[0][5*7+x].Segments|=FSO::LotView::Model::WallSegments(2);}
    for(int y=2;y<=4;++y){arch.Walls[0][y*7+2].Segments|=FSO::LotView::Model::WallSegments(1);arch.Walls[0][y*7+1].Segments|=FSO::LotView::Model::WallSegments(4);
        arch.Walls[0][y*7+4].Segments|=FSO::LotView::Model::WallSegments(4);arch.Walls[0][y*7+5].Segments|=FSO::LotView::Model::WallSegments(1);}
    ctx.RebuildEnvironmentGeometry();ctx.Clock->Hours=20;
    const int inside=ctx.GetEnvironmentRoomAt(LotTilePos(56,56,1));
    Check(inside>0&&ctx.EnvironmentRooms[inside].CanonicalID==1,"column-order original living room record");
    const auto upper=uint16_t(ctx.EnvironmentMaps[1][3*7+3]);
    Check(upper!=1024&&ctx.EnvironmentRooms[upper].CanonicalID==1024,"second story prefix independent of EnvGlobal index");
    OBJD humanDef,petDef,lampDef;humanDef.ObjectType=petDef.ObjectType=OBJDType::Person;lampDef.ObjectType=OBJDType::Normal;
    FSO::Content::GameObject humanObject,petObject,lampObject;humanObject.OBJ=&humanDef;petObject.OBJ=&petDef;lampObject.OBJ=&lampDef;
    // VMEntity reads slots/labels/interaction tables through Resource even
    // when a fixture has no authored behaviors or commercial content.
    FSO::Files::Formats::IFF::IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"visual-lighting-fixture",true);
    humanObject.Resource=petObject.Resource=lampObject.Resource=&resource;
    VMAvatar human(&humanObject),pet(&petObject);VMGameObject lamp(&lampObject,nullptr);
    human.Thread=std::make_unique<VMThread>(&ctx,&human,8);pet.Thread=std::make_unique<VMThread>(&ctx,&pet,8);lamp.Thread=std::make_unique<VMThread>(&ctx,&lamp,8);
    human.SetPersonData(VMPersonDataVariable::Gender,0);pet.SetPersonData(VMPersonDataVariable::Gender,2);
    human.Position(LotTilePos(56,56,1));pet.Position(LotTilePos(56,56,1));lamp.Position(LotTilePos(56,56,1));
    human.SetValue(VMStackObjectVariable::Room,3);pet.SetValue(VMStackObjectVariable::Room,3);lamp.SetValue(VMStackObjectVariable::Room,3);
    vm.Entities={&human,&pet};ctx.EnvironmentObjectsDirty=ctx.VisualLightingDirty=true;
    const auto seed=ctx.RandomSeed;const auto clock=ctx.Clock->MinuteFractions;
    ctx.PrepareVisualLighting();
    Check(ctx.VisualOverhead[1]&&bp.VisualLight.Tones[inside*2]==25,"human entry raises overhead in native graphical room 3");
    const auto unchanged=ctx.VisualLightingRefreshes;ctx.PrepareVisualLighting();
    Check(ctx.VisualLightingRefreshes==unchanged,"stable draw preparation must not rescan objects");
    Check(ctx.RandomSeed==seed&&ctx.Clock->MinuteFractions==clock,"visual preparation must not tick or consume RNG");
    human.Position(LotTilePos(24,24,1));ctx.ObserveVisualActor(&human);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    Check(ctx.VisualOverhead[1]&&bp.VisualLight.Tones[inside*2]==25,"pet retains overhead after human exit");
    lamp.SetValue(VMStackObjectVariable::FlagField2,0x400);lamp.SetValue(VMStackObjectVariable::LightingContribution,0);
    vm.Entities.push_back(&lamp);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    Check(bp.VisualLight.Tones[inside*2]==16,"admitted zero-contribution lamp counts");
    pet.Position(LotTilePos(24,24,1));ctx.ObserveVisualActor(&pet);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    Check(ctx.VisualOverhead[1],"pet departure does not execute human overhead transition");
    const auto previousMaps=bp.VisualLight.Maps;
    ctx.RebuildEnvironmentGeometry();ctx.PrepareVisualLighting();
    Check(ctx.VisualOverhead[1]&&bp.VisualLight.Maps==previousMaps,"record ID reuse preserves historical overhead");
    human.Position(LotTilePos(56,56,1));ctx.ObserveVisualActor(&human);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    human.Position(LotTilePos(24,24,1));ctx.ObserveVisualActor(&human);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    Check(!ctx.VisualOverhead[1],"last human leaves an otherwise empty room");
    vm.Entities.erase(std::find(vm.Entities.begin(),vm.Entities.end(),&lamp));
    pet.Position(LotTilePos(56,56,1));ctx.ObserveVisualActor(&pet);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    Check(!ctx.VisualOverhead[1]&&bp.VisualLight.Tones[inside*2]==16,"pet-only initial room stays dark");
    const auto publication=bp.VisualLight.Generation;
    human.Position(LotTilePos(56,56,1));ctx.ObserveVisualActor(&human);
    human.Position(LotTilePos(24,24,1));ctx.ObserveVisualActor(&human);
    Check(bp.VisualLight.Generation==publication,"actor ticks must not publish intermediate renderer snapshots");
    ctx.PrepareVisualLighting();
    Check(ctx.VisualOverhead[1]&&bp.VisualLight.Tones[inside*2]==25,"human enters and exits between draws while pet retains overhead");
    human.Position(LotTilePos::OUT_OF_WORLD);ctx.ObserveVisualActor(&human);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    std::vector<int> counts;for(const auto& room:ctx.EnvironmentRooms)counts.push_back(room.VisualPeople);
    for(auto invalid:{LotTilePos(-16,48,1),LotTilePos(112,48,1),LotTilePos(48,-16,1),LotTilePos(48,112,1)}){
        human.Position(invalid);ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
        for(size_t i=1;i<counts.size();++i)Check(ctx.EnvironmentRooms[i].VisualPeople==counts[i],"out-of-bounds actor aliased a linearized tile");
    }
    human.Position(LotTilePos(24,24,1));ctx.ObserveVisualActor(&human);
    const auto walls=arch.Walls[0];
    for(int diagonal:{16,32}){
        arch.Walls[0]=walls;const int x=4,y=diagonal==16?4:2;
        arch.Walls[0][y*7+x].Segments=FSO::LotView::Model::WallSegments(diagonal);
        arch.Walls[0][y*7+x+1].Segments&=~FSO::LotView::Model::WallSegments(1);
        arch.Walls[0][(y+(diagonal==16?1:-1))*7+x].Segments&=~FSO::LotView::Model::WallSegments(diagonal==16?2:8);
        ctx.RebuildEnvironmentGeometry();ctx.PrepareVisualLighting();
        const auto cell=ctx.EnvironmentMaps[0][y*7+x];const auto low=uint16_t(cell),high=uint16_t(cell>>16);
        Check(low!=high&&!ctx.EnvironmentRooms[low].Outside&&ctx.EnvironmentRooms[high].Outside,"split binding needs real interior/exterior halves");
        Model::VMRoomMap graphical;std::vector<VMRoom> graphicRooms(1);
        graphical.GenerateMap(arch.Walls[0],arch.Floors[0],7,7,graphicRooms);
        const auto graphicCell=graphical.Map[y*7+x];
        const auto interiorGraphic=uint16_t(graphical.Map[3*7+3]);
        for(int half=0;half<2;++half){
            const auto graphicID=uint16_t(graphicCell>>(half*16));
            const int logicalHalf=bp.LightingHalf(x,y,1,half);
            const auto logicalID=logicalHalf?high:low;
            std::printf("split binding diagonal=%d tile=%d,%d graphicalCell=%08x interior=%u half=%d graphRoom=%u logicalCell=%08x logicalHalf=%d canonical=%u outside=%d\n",
                diagonal,x,y,unsigned(graphicCell),unsigned(interiorGraphic),half,unsigned(graphicID),unsigned(cell),logicalHalf,
                unsigned(ctx.EnvironmentRooms[logicalID].CanonicalID),int(ctx.EnvironmentRooms[logicalID].Outside));
            Check((graphicID==interiorGraphic)==!ctx.EnvironmentRooms[logicalID].Outside,"graphical face maps to correct canonical logical half");
            const int sample=bp.VisualLight.Sample(x,y,8,8,1,uint8_t(diagonal),logicalHalf);
            Check(sample==VisualLighting::FirstKey+logicalID*2+1,"split face reads direct sample instead of overwritten byte");
        }
        for(const auto& room:ctx.EnvironmentRooms)if(room.Story){
            Check(!room.EnvironmentTileVisits.empty(),"flood emitted an empty canonical record");
            Check((room.CanonicalID>>10)==room.Story-1,"canonical ID story prefix");
        }
    }
    arch.Walls[0]=walls;arch.TS1PoolTiles.assign(49,0);arch.TS1PoolTiles[3*7+3]=1;
    ctx.RebuildEnvironmentGeometry();human.Position(LotTilePos(56,56,1));ctx.ObserveVisualActor(&human);ctx.PrepareVisualLighting();
    const int pool=ctx.GetEnvironmentRoomAt(human.Position());
    Check(pool>0&&ctx.EnvironmentRooms[pool].Pool&&bp.VisualLight.Tones[pool*2]==25,"pool producer forces visual floor independently of lamps");
    Check(!ctx.VisualOverhead[ctx.EnvironmentRooms[pool].CanonicalID],"pool rejects overhead setter");
    for(const auto& room:ctx.EnvironmentRooms)if(room.Story==2)Check(!room.Pool,"upper story cannot become a pool record");
    const auto ownedMaps=bp.VisualLight.Maps;
    ctx.EnvironmentMaps[0].clear();
    Check(bp.VisualLight.Maps==ownedMaps,"renderer map snapshot must survive producer map mutation");
    VM::UseWorld(true);lamp.WorldUI=std::make_unique<FSO::LotView::Components::ObjectComponent>(&lampObject);
    lamp.SetValue(VMStackObjectVariable::FlagField2,0);
    for(int amount:{-1,0,50}){
        lamp.SetValue(VMStackObjectVariable::LightingContribution,int16_t(amount));lamp.RefreshLight();
        Check(lamp.WorldUI->Room==(amount?65535:3),"actual emitter predicate is signed nonzero without GeneratesLight");
    }
    lampDef.ObjectType=OBJDType::Portal;lamp.RefreshLight();
    Check(lamp.WorldUI->Room==3,"portal contribution does not bypass ambient cap");
    VM::UseWorld(false);
    vm.Entities.clear();
}

int main(){try{Palette();Producer();std::puts("TS1 ambient palette, record binding and visual producer passed");return 0;}
catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
