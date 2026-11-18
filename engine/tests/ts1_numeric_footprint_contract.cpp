// SPDX-License-Identifier: MPL-2.0
// Numeric source rectangle and native occupancy have different lifetimes.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/model/VMPersonDataVariable.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
using FSO::LotView::Model::Direction;
namespace {
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Fixture {
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"numeric-footprint",true};
 OBJD Definition;FSO::Content::GameObject Object;
 Entities::VMMultitileGroup ActorGroup,SeatGroup;
 std::unique_ptr<VMAvatar> Actor;std::unique_ptr<VMGameObject> Seat;
 Fixture(){
  Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
  Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);
  Context.RebuildEnvironmentGeometry();
  Definition.GUID=0xf093a522;Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;
  Actor=std::make_unique<VMAvatar>(&Object);Seat=std::make_unique<VMGameObject>(&Object,nullptr);
  Actor->MultitileGroup=&ActorGroup;ActorGroup.AddObject(Actor.get());Seat->MultitileGroup=&SeatGroup;SeatGroup.AddObject(Seat.get());
  Machine.AddEntity(Actor.get());Machine.AddEntity(Seat.get());
  Actor->Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&Context,Actor.get(),16);
  Actor->Position(LotTilePos(40,56,1));Actor->Direction(Direction::NORTH);
 }
 ~Fixture(){Actor->Container=nullptr;Machine.Entities.clear();}
};
void NumericLifetime(){
 Fixture f;auto& actor=*f.Actor;
 Check(!actor.TS1NumericFootprintKnown,"new avatar invented retained numeric bounds");
 actor.SetPersonData(VMPersonDataVariable::FootprintExtension,8);
 auto hypothetical=actor.GetObstacle(actor.Position(),actor.Direction());
 Check(hypothetical&&!actor.TS1NumericFootprintKnown,"placement query published numeric bounds");
 actor.RestoreTS1Footprint(10,20,30,40);
 Check(actor.TS1NumericFootprintKnown&&actor.TS1NumericFootprint==std::array<int32_t,4>{10,20,30,40}&&bool(actor.Footprint),"classic numeric restore did not bind registered bounds");
 actor.PrePositionChange(&f.Context);
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{10,20,30,40},"unregistration destroyed numeric source state");
 actor.Container=f.Seat.get();actor.RestoreTS1Footprint(-5,17,-9,17);
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{-5,17,-9,17},"contained restore lost or normalized numeric bounds");
 const auto retained=actor.TS1NumericFootprint;
 actor.SetPersonData(VMPersonDataVariable::FootprintExtension,0);
 hypothetical=actor.GetObstacle(LotTilePos(72,72,1),Direction::EAST);
 actor.SetPersonData(VMPersonDataVariable::FootprintExtension,8);
 hypothetical=actor.GetObstacle(actor.Position(),actor.Direction());
 Check(!actor.Footprint&&actor.TS1NumericFootprint==retained,"temporary heading/PD73 placement queries republished cached numeric bounds");
 actor.RefreshTS1Footprint();
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{37,45,43,59},"explicit source rebuild omitted contained numeric bounds or registered occupancy");
 actor.SetPersonData(VMPersonDataVariable::RouteFootprintMask,8);actor.SetPersonData(VMPersonDataVariable::NonInterruptable,1);
 actor.ClearTS1InteractionFootprint();
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{37,53,43,59}&&
       actor.GetPersonData(VMPersonDataVariable::FootprintExtension)==0&&actor.GetPersonData(VMPersonDataVariable::RouteFootprintMask)==0&&actor.GetPersonData(VMPersonDataVariable::NonInterruptable)==0,
       "explicit interaction release did not rebuild numeric state independently of occupancy");
 actor.Position(LotTilePos(72,72,1));actor.PositionChange(&f.Context,true);
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{69,69,75,75},
       "committed contained placement retained stale numeric bounds instead of rebuilding at parent coordinates");
 actor.Position(LotTilePos::OUT_OF_WORLD);actor.RestoreTS1Footprint(101,102,103,104);
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{101,102,103,104},"out-of-world restore discarded literal numeric state");
 actor.Container=nullptr;actor.PositionChange(&f.Context,true);
 Check(!actor.Footprint&&actor.TS1NumericFootprint==std::array<int32_t,4>{-32771,-32771,-32765,-32765},
       "out-of-world commit skipped the unconditional numeric rebuild");
 actor.Position(LotTilePos(40,56,1));actor.PositionChange(&f.Context,true);
 Check(actor.Footprint&&actor.TS1NumericFootprintKnown&&actor.TS1NumericFootprint==std::array<int32_t,4>{actor.Footprint->x1,actor.Footprint->y1,actor.Footprint->x2,actor.Footprint->y2},"normal committed placement did not remember numeric bounds");
}
void GenericNumericLifetime(){
 Fixture f;auto& object=*f.Seat;VMEntity& base=object;
 object.Thread=std::make_unique<FSO::SimAntics::Engine::VMThread>(&f.Context,&object,16);
 object.Position(LotTilePos(40,56,1));object.Direction(Direction::NORTH);
 f.Definition.TileWidth=1;
 auto hypothetical=object.GetTS1NumericFootprint(object.Position(),object.Direction());
 Check(hypothetical==std::array<int32_t,4>{40,56,40,56}&&!base.TS1NumericFootprintKnown,
       "generic width1 query used physical fallback or published state");
 object.PositionChange(&f.Context,true);
 Check(base.TS1NumericFootprintKnown&&base.TS1NumericFootprint==hypothetical&&object.Footprint,
       "generic commit did not retain independent source geometry");
 Check(object.Footprint->x1!=base.TS1NumericFootprint[0],"width1 control failed to distinguish physical footprint");
 object.RestoreTS1Footprint(7,9,4,2);
 Check(base.TS1NumericFootprint==std::array<int32_t,4>{7,9,4,2}&&object.Footprint&&
       object.Footprint->x1==32&&object.Footprint->y1==48&&
       object.Footprint->x2==48&&object.Footprint->y2==64,
       "literal generic restore overwrote independent native placement geometry");
 object.RefreshTS1Footprint();
 object.PrePositionChange(&f.Context);
 Check(!object.Footprint&&base.TS1NumericFootprint==hypothetical,"pre-position overwrote numeric bounds with physical approximation");
 f.Definition.TileWidth=uint16_t(int16_t(-3));
 Check(object.GetTS1NumericFootprint(object.Position(),object.Direction())==std::array<int32_t,4>{41,57,39,55},
       "generic signed negative width was clamped or normalized");
 f.Definition.TileWidth=0;f.Definition.FootprintMask=0x4321;object.Direction(static_cast<Direction>(2));
 Check(object.GetTS1NumericFootprint(object.Position(),object.Direction())==std::array<int32_t,4>{35,52,47,62},
       "generic odd heading did not rotate source mask nibbles");
 object.SetFlag(VMEntityFlags::HasZeroExtent,true);
 Check(!object.Footprint&&base.TS1NumericFootprint==std::array<int32_t,4>{35,52,47,62},
       "zero extent erased source numeric rectangle or kept physical occupancy");
 // A false check callback may move/contain its target. Its committed numeric
 // rectangle survives independently; a reader must not derive it from flags.
 object.Container=f.Actor.get();object.Position(LotTilePos(72,72,1));object.PositionChange(&f.Context,true);
 Check(!object.Footprint&&base.TS1NumericFootprint==std::array<int32_t,4>{67,68,79,78},
       "contained generic commit skipped source geometry");
 const auto callbackSnapshot=base.TS1NumericFootprint;
 object.GetObstacle(LotTilePos(104,104,1),Direction::WEST);
 Check(base.TS1NumericFootprint==callbackSnapshot,"hypothetical query mutated post-callback source snapshot");
 object.RestoreTS1Footprint(7,9,4,2);
 Check(!object.Footprint&&base.TS1NumericFootprint==std::array<int32_t,4>{7,9,4,2},
       "base restore normalized contained/zero-extent literal bounds");
 object.Container=nullptr;object.Position(LotTilePos::OUT_OF_WORLD);object.PositionChange(&f.Context,true);
 Check(!object.Footprint&&base.TS1NumericFootprint==std::array<int32_t,4>{-32773,-32772,-32761,-32762},
       "out-of-world generic commit retained old source geometry");
}
}
int main(){try{VM::UseWorld(false);NumericLifetime();GenericNumericLifetime();std::cout<<"PASS TS1 numeric versus occupancy footprint lifecycle\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
