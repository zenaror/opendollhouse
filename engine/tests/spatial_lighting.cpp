// SPDX-License-Identifier: MPL-2.0
// Data-free producer/event checks. Fixed expectations transcribe the DVD
// routines; running this is not an original-runtime or renderer parity claim.
#include "common/TS1/TS1SpatialLighting.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "files/formats/iff/IffFile.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::Common::TS1;
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Entities;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
static void Check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static bool Empty(const SpatialCell& cell){return cell.Slots[0].Dx==127&&cell.Slots[1].Dx==127;}

static void OrderedCells(){
    SpatialCell c;c.Insert({4,0});c.Insert({3,0});c.Insert({1,0});
    Check(c.Slots[0]==SpatialOffset{1,0}&&c.Slots[1]==SpatialOffset{3,0},"third overwrites first without promotion");
    c.Erase({1,0});Check(c.Slots[0]==SpatialOffset{3,0}&&c.Slots[1].Dx==127,"lost third is not refilled");
    c={};c.Insert({2,0});c.Insert({0,2});c.Insert({-2,0});
    Check(c.Slots[0]==SpatialOffset{2,0}&&c.Slots[1]==SpatialOffset{0,2},"equal distance retains old order");
    c={};c.Insert({1,1});c.Insert({1,1});c.Erase({1,1});
    Check(c.Slots[0]==SpatialOffset{1,1}&&c.Slots[1].Dx==127,"one unregister removes only one duplicate");
}

static void Admission(){
    SpatialPublisher p;p.Initialize(9,9);
    std::vector<std::vector<uint32_t>> maps(2,std::vector<uint32_t>(81,1u|(1u<<16)));
    std::vector<int16_t> ids{-1,0,1,2};SpatialRoomMapView view{9,9,&maps,&ids};
    SpatialEmitter e{{4,4,1},0,1,50,0,0x400};
    p.CollectVisit(e,view);Check(p.Maps[0][4*9+5].Slots[0]==SpatialOffset{1,0},"target minus source offset");
    const auto active=p.Maps;
    e.Hidden=1;p.CollectVisit(e,view);Check(p.Maps==active,"hidden collector visit skips without cleanup");
    e.Hidden=0;e.Flags2=0;p.CollectVisit(e,view);Check(p.Maps==active,"flag-disabled visit skips without cleanup");
    e.Flags2=0x400;e.SourceMode=0;p.CollectVisit(e,view);Check(p.Maps==active,"nonzero other source mode does not erase");
    e.Contribution=0;p.CollectVisit(e,view);Check(Empty(p.Maps[0][4*9+5]),"admitted zero erases even mode zero");
    e.SourceMode=1;e.Contribution=-50;p.CollectVisit(e,view);Check(p.Maps==active,"negative nonzero source inserts");
    e.Hidden=1;e.Flags2=0;e.Contribution=0;p.Unregister(e);Check(Empty(p.Maps[0][4*9+5]),"unregister mode1 ignores other admission fields");
    e.Hidden=0;e.Flags2=0x400;e.Contribution=50;p.CollectVisit(e,view);
    e.SourceMode=0;p.Unregister(e);Check(p.Maps==active,"unregister mode0 leaves footprint");
    Check(Empty(p.Maps[1][4*9+5])&&Empty(p.Maps[0][0]),"story isolation and exterior border untouched");
    p.Initialize(9,9);e.SourceMode=1;
    maps[0][4*9+5]=2u|(2u<<16);maps[0][5*9+4]=1u|(2u<<16);
    p.CollectVisit(e,view);
    Check(Empty(p.Maps[0][4*9+5])&&!Empty(p.Maps[0][5*9+4]),"whole source admits matching split side but not another whole room");
    p.Initialize(9,9);maps[0][4*9+4]=1u|(2u<<16);maps[0][4*9+5]=2u|(3u<<16);
    p.CollectVisit(e,view);
    Check(!Empty(p.Maps[0][4*9+5])&&Empty(p.Maps[0][3*9+4]),"raw split source -5 admits unrelated split, not whole source side");
    e.Position={0,4,1};const auto border=p.Maps;p.Unregister(e);p.CollectVisit(e,view);
    Check(p.Maps==border,"invalid source border neither erases nor inserts");
}

static void DirtyProtocol(){
    SpatialRoomSchedule s;s.Mark(0);s.Mark(1);s.Mark(1024);
    std::vector<uint16_t> seen;
    s.Flush({0,1,2,1024},[&](uint16_t id){seen.push_back(id);if(id==0)s.Mark(0);});
    Check(seen==std::vector<uint16_t>({0,1,1024})&&s.StoryMask==0&&s.Dirty[0],"synchronous F1 leaves dormant record after mask clear");
    seen.clear();s.Flush({0,1,2,1024},[&](uint16_t id){seen.push_back(id);});Check(seen.empty(),"dormant dirty record does not collect every draw");
    s.Mark(2);s.Flush({0,1,2,1024},[&](uint16_t id){seen.push_back(id);});
    Check(seen==std::vector<uint16_t>({0,2}),"later story event revives dormant lower record in original numeric order");
}

static void VMEvents(){
    VM::UseWorld(false);FSO::LotView::Model::Blueprint bp(7,7);
    VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.TS1=true;ctx.Blueprint=&bp;
    ctx.Architecture(std::make_unique<VMArchitecture>(7,7,&bp,&ctx));ctx.RebuildEnvironmentGeometry();
    ctx.Clock->Hours=12; // explicit day baseline: the default midnight is already period2
    OBJD def;def.ObjectType=OBJDType::Normal;FSO::Content::GameObject object;object.OBJ=&def;
    FSO::Files::Formats::IFF::IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"spatial-data-free",true);object.Resource=&resource;
    VMGameObject a(&object,nullptr),b(&object,nullptr),child(&object,nullptr);
    a.Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&ctx,&a,8);
    b.Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&ctx,&b,8);
    child.Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&ctx,&child,8);
    vm.AddEntity(&a);vm.AddEntity(&b);vm.AddEntity(&child);
    vm.Entities={&b,&child,&a}; // deliberate traversal-order adversary after real admission
    for(auto* e:vm.Entities){e->SetValue(VMStackObjectVariable::FlagField2,0x400);e->SetValue(VMStackObjectVariable::LightSource,1);e->SetValue(VMStackObjectVariable::LightingContribution,50);}
    a.Position(LotTilePos(40,56,1));b.Position(LotTilePos(72,56,1));child.Position(a.Position());
    ctx.RegisterObjectPos(&a);ctx.RegisterObjectPos(&b);
    const int env=ctx.GetEnvironmentRoomAt(a.Position());Check(env>0,"fixture source has valid original logical room");
    for(auto& r:ctx.EnvironmentRooms)r.SpatialTileOrder.clear();
    ctx.EnvironmentRooms[env].SpatialTileOrder={3*7+2,3*7+4,3*7+2};
    ctx.PrepareVisualLighting();
    const auto target=3*7+3;
    Check(bp.VisualLight.SpatialMaps[0][target].Slots[0]==SpatialOffset{-1,0}&&bp.VisualLight.SpatialMaps[0][target].Slots[1]==SpatialOffset{1,0},"ordered repeated room visits override shuffled VM entity order");
    const auto revision=bp.VisualLight.SpatialGeneration;const auto passes=ctx.SpatialCollectorPasses;
    ctx.PrepareVisualLighting();Check(bp.VisualLight.SpatialGeneration==revision&&ctx.SpatialCollectorPasses==passes,"stable preparation neither recollects nor republishes");
    ctx.RefreshSpatialObject(&a);ctx.PrepareVisualLighting();Check(bp.VisualLight.SpatialGeneration==revision,"net equal erase/insert stream preserves published generation");
    // Remove old A, move to B and back without any draw/collector in between.
    ctx.UnregisterObjectPos(&a);a.Position(LotTilePos(40,72,1));ctx.RegisterObjectPos(&a);
    ctx.UnregisterObjectPos(&a);a.Position(LotTilePos(40,56,1));ctx.RegisterObjectPos(&a);
    Check(ctx.SpatialLights.Maps[0][target].Slots[0]==SpatialOffset{-1,0}&&ctx.SpatialLights.Maps[0][target].Slots[1].Dx==127,"intermediate unregister A-B-A survives before publication");
    ctx.PrepareVisualLighting();Check(bp.VisualLight.SpatialMaps[0][target].Slots[1]==SpatialOffset{1,0},"collector restores source only at actual refresh boundary");
    a.Contained.assign(1,&child);child.Container=&a;child.ContainerSlot=0;
    a.SetValue(VMStackObjectVariable::LightingContribution,0);ctx.RegisterObjectPos(&child);
    ctx.EnvironmentRooms[env].SpatialTileOrder={3*7+2};ctx.RefreshSpatialObject(&a);ctx.PrepareVisualLighting();
    Check(bp.VisualLight.SpatialMaps[0][target].Slots[1]==SpatialOffset{1,0},"head-root zero then child source follows ascending-slot DFS");
    const auto childVisits=ctx.SpatialObjectVisits;ctx.RefreshSpatialObject(&child);ctx.PrepareVisualLighting();
    Check(ctx.SpatialObjectVisits==childVisits+2,"child is visited through root once, not as another independent tile head");
    Check(ctx.SpatialPeriod==0&&ctx.Clock->TimeOfDay()==0,"phase fixture starts in day period0");
    const auto beforePeriod=ctx.SpatialCollectorPasses;ctx.Clock->Hours=20;
    Check(ctx.Clock->TimeOfDay()==2,"first transition targets night period2");ctx.ObserveSpatialPeriod();
    Check(ctx.SpatialPeriod==2&&ctx.SpatialCollectorPasses==beforePeriod+1,"night transition collects immediately before any draw");
    ctx.Clock->Hours=12;Check(ctx.Clock->TimeOfDay()==0,"second transition returns to day period0");ctx.ObserveSpatialPeriod();
    Check(ctx.SpatialCollectorPasses==beforePeriod+2,"phase transitions collect before a later draw");
    Check(VisualIntensity(128.f/255,0)==128&&VisualIntensity(129.f/255,0)==129&&VisualTone(128.f/255,0)==VisualTone(129.f/255,0),"q is distinct before same tone bucket");
    ctx.EnvironmentRooms[env].LightFactor=128.f/255;ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    const auto toneGeneration=bp.VisualLight.Generation,intensityGeneration=bp.VisualLight.IntensityGeneration;
    ctx.EnvironmentRooms[env].LightFactor=129.f/255;ctx.VisualLightingDirty=true;ctx.PrepareVisualLighting();
    Check(bp.VisualLight.Generation==toneGeneration&&bp.VisualLight.IntensityGeneration==intensityGeneration+1&&bp.VisualLight.Intensities[size_t(env)*2]==129,"q-only publication invalidates spatial consumers while retaining old tone generation");
    // Isolated manager-state fixture: pre-collection counters are evaluated
    // with the already changed Overhead flag, rather than a cached old result.
    def.ObjectType=OBJDType::Person;
    a.SetValue(VMStackObjectVariable::FlagField2,0);child.SetValue(VMStackObjectVariable::FlagField2,0);
    auto& record=ctx.EnvironmentRooms[env];record.LightFactor=record.SpatialLightFactor=0;
    record.SpatialPeople=1;record.SpatialLamps=0;record.SpatialPool=record.Pool=false;
    ctx.VisualOverhead[record.CanonicalID]=1;ctx.SpatialSchedule.Dirty.fill(0);ctx.SpatialSchedule.StoryMask=0;
    ctx.RefreshSpatialObject(&a);ctx.FlushSpatialLighting();
    Check(!ctx.SpatialSchedule.Dirty[record.CanonicalID],"current Overhead with old counters avoids spurious changed-factor F1");
    a.Contained.clear();child.Container=nullptr;
    // Real F0 consumer, not the low-level geometry helper: two topology
    // boundaries before a draw must each publish source operations.
    def.ObjectType=OBJDType::Normal;a.SetValue(VMStackObjectVariable::FlagField2,0x400);
    a.SetValue(VMStackObjectVariable::LightingContribution,50);a.SetValue(VMStackObjectVariable::RoomImpact,1);
    ctx.RegisterObjectPos(&a);ctx.Architecture()->RegenRoomMap();
    const auto topologyPasses=ctx.SpatialCollectorPasses;
    ctx.RegeneratePortalInfo();
    const auto containsA=[&](){const auto& c=ctx.SpatialLights.Maps[0][target];return c.Slots[0]==SpatialOffset{1,0}||c.Slots[1]==SpatialOffset{1,0};};
    Check(ctx.SpatialCollectorPasses==topologyPasses+1&&containsA(),"first F0 collects source before draw");
    const int oldRecord=ctx.SpatialPlacements.at(&a).Room;Check(oldRecord==0,"topology fixture begins in original exterior record0");
    auto& arch=*ctx.Architecture();
    for(auto& story:arch.Walls)for(auto& wall:story){wall.TopLeftStyle=wall.TopRightStyle=1;wall.TopLeftPattern=wall.TopRightPattern=1;}
    for(auto& story:arch.Floors)for(auto& floor:story)floor.Pattern=1;
    for(int x=2;x<=4;++x){arch.Walls[0][2*7+x].Segments|=FSO::LotView::Model::WallSegments(2);arch.Walls[0][1*7+x].Segments|=FSO::LotView::Model::WallSegments(8);
        arch.Walls[0][4*7+x].Segments|=FSO::LotView::Model::WallSegments(8);arch.Walls[0][5*7+x].Segments|=FSO::LotView::Model::WallSegments(2);}
    for(int y=2;y<=4;++y){arch.Walls[0][y*7+2].Segments|=FSO::LotView::Model::WallSegments(1);arch.Walls[0][y*7+1].Segments|=FSO::LotView::Model::WallSegments(4);
        arch.Walls[0][y*7+4].Segments|=FSO::LotView::Model::WallSegments(4);arch.Walls[0][y*7+5].Segments|=FSO::LotView::Model::WallSegments(1);}
    a.SetValue(VMStackObjectVariable::LightingContribution,0);arch.RegenRoomMap();ctx.RegeneratePortalInfo();
    const int newRecord=ctx.SpatialPlacements.at(&a).Room;
    Check(ctx.SpatialCollectorPasses==topologyPasses+2&&!containsA(),"second F0 erases zero source without coalescing with first");
    Check(newRecord==1&&newRecord!=oldRecord&&ctx.SpatialPlacements.at(&a).Registered,"split synchronizes canonical placement identity while retaining placement lifetime");
    ctx.SpatialSchedule.Dirty.fill(0);ctx.SpatialSchedule.StoryMask=0;
    ctx.UnregisterObjectPos(&a);vm.RemoveEntity(&a);
    Check(ctx.SpatialSchedule.Dirty[size_t(newRecord)]&&!ctx.SpatialSchedule.Dirty[size_t(oldRecord)],"post-split unregister and removal dirty new canonical room, not obsolete exterior ID");
    vm.RemoveEntity(&b);vm.RemoveEntity(&child);
}
int main(){try{OrderedCells();Admission();DirtyProtocol();VMEvents();std::puts("TS1 spatial producer checks passed");return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
