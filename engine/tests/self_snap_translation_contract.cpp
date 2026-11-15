// SPDX-License-Identifier: MPL-2.0
// Synthetic geometry through real Snap / placement consumers. Does not prove
// the original executable's implementation or the appearance of commercial art.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMSnap.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/SLOT.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Engine;
using namespace FSO::LotView::Model;
using namespace FSO::Files::Formats::IFF;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);std::printf("PASS %s\n",why);}
using Box=std::array<int,4>;
static Box box(VMEntity& e){check(e.Footprint!=nullptr,"fixture has a real nonzero collision footprint");auto f=e.Footprint.get();return {f->x1,f->y1,f->x2,f->y2};}
static Box shifted(Box b,LotTilePos delta){return {b[0]+delta.x,b[1]+delta.y,b[2]+delta.x,b[3]+delta.y};}
int main(){try{
 VM::UseWorld(false);VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.TS1=true;
 ctx.Architecture(std::make_unique<VMArchitecture>(16,16,nullptr,&ctx));ctx.Architecture()->Rooms[0].Map.assign(256,0);ctx.RoomInfo.resize(1);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"self-snap-fixture",true);
 auto slots=new Chunks::SLOT();slots->ChunkID=128;slots->ChunkProcessed=true;slots->Slots[3].resize(1);iff.AddChunk(slots);
 auto& slot=slots->Slots[3][0];slot.Type=3;slot.Offset=st::Vector3(0,-6,0);
 Chunks::OBJD def;def.SlotID=128;def.StackSize=8;def.NumAttributes=1;
 FSO::Content::GameObject object;object.OBJ=&def;object.Resource=&resource;
 VMGameObject a(&object,nullptr),b(&object,nullptr),c(&object,nullptr),target(&object,nullptr);
 Entities::VMMultitileGroup group;group.MultiTile=true;
 group.AddDynamicObject(&b,LotTilePos(0,16,0));group.AddDynamicObject(&a,LotTilePos(0,0,0));group.AddDynamicObject(&c,LotTilePos(16,32,0));
 for(auto e:{&a,&b,&c}){e->MultitileGroup=&group;e->SetValue(VMStackObjectVariable::Flags,258);e->SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));e->SetValue(VMStackObjectVariable::AllowedHeightFlags,1);e->SetAttribute(0,42);}
 a.ObjectID=1;b.ObjectID=2;c.ObjectID=3;target.ObjectID=4;target.Position(LotTilePos(144,144,1));
 VMThread thread(&ctx,&a,8);VMStackFrame frame;frame.Caller=&a;frame.Callee=&a;frame.StackObject=&a;frame.Thread=&thread;
 Primitives::VMSnap snap;Primitives::VMSnapOperand op;op.Mode=Primitives::VMSnapSlotScope::Literal;
 auto publish=[&](){for(auto e:{&a,&b,&c})e->PositionChange(&ctx,true);};
 // Solid square tile masks (like the real NPCCar data) with an asymmetric group.
 // No HasZeroExtent preparation: collision and bounds remain active.
 for(int notch=0;notch<8;++notch){
  a.Position(LotTilePos(112,112,1));b.Position(LotTilePos(112,96,1));c.Position(LotTilePos(96,80,1));
  a.Direction((Direction)(1<<notch));b.Direction(Direction::SOUTH);c.Direction(Direction::SOUTH);
  publish();check(group.CanTranslate(),"solid published geometry is coherent before translation");
  const std::array<Box,3> feet{box(a),box(b),box(c)};
  const std::array<LotTilePos,3> before{a.Position(),b.Position(),c.Position()};
  const std::array<Direction,3> dirs{a.Direction(),b.Direction(),c.Direction()};
  check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"self-slot accepts finite translation in each of eight directions");
  auto delta=a.Position()-before[0];
  check((delta.x!=0||delta.y!=0)&&b.Position()==before[1]+delta&&c.Position()==before[2]+delta,"all physical parts translate rigidly including non-first base");
  check(box(a)==shifted(feet[0],delta)&&box(b)==shifted(feet[1],delta)&&box(c)==shifted(feet[2],delta),"every nonzero collision footprint translates by exactly the same displacement");
  check(a.Direction()==dirs[0]&&b.Direction()==dirs[1]&&c.Direction()==dirs[2],"self-snap without SnapToDirection never rewrites tile facings");
  for(auto e:{&a,&b,&c})check(e->GetAttribute(0)==42,"translation leaves authored attributes intact");
 }
 // Any group part can be the self-slot caller; displacement is not a new origin.
 frame.Caller=&b;frame.StackObject=&b;auto na=a.Position(),nb=b.Position(),nc=c.Position();
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"non-base self-slot caller accepts translation");
 auto nd=b.Position()-nb;check(a.Position()==na+nd&&c.Position()==nc+nd,"non-base caller preserves full group displacement");
 frame.Caller=&a;frame.StackObject=&a;
 // Failure on a child after a valid base goal cannot partially commit movement.
 a.Position(LotTilePos(80,24,1));b.Position(LotTilePos(80,8,1));c.Position(LotTilePos(96,8,1));
 a.Direction(Direction::NORTH);slot.Offset=st::Vector3(0,-16,0);publish();
 const std::array<Box,3> boundaryFeet{box(a),box(b),box(c)};
 const std::array<Routing::VMObstacle*,3> boundaryPointers{a.Footprint.get(),b.Footprint.get(),c.Footprint.get()};
 const auto pa=a.Position(),pb=b.Position(),pc=c.Position();const auto da=a.Direction(),db=b.Direction(),dc=c.Direction();
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_FALSE,"child outside lot rejects whole self-snap");
 check(a.Position()==pa&&b.Position()==pb&&c.Position()==pc&&a.Direction()==da&&b.Direction()==db&&c.Direction()==dc,"rejected translation returns every tile and facing unchanged");
 check(box(a)==boundaryFeet[0]&&box(b)==boundaryFeet[1]&&box(c)==boundaryFeet[2]&&a.Footprint.get()==boundaryPointers[0]&&b.Footprint.get()==boundaryPointers[1]&&c.Footprint.get()==boundaryPointers[2],"boundary rollback restores every published footprint including identity");
 check(a.GetValue(VMStackObjectVariable::PrimitiveResult)==2,"existing out-of-bounds primitive failure is preserved");
 // Canonical initial placement and explicit rotation remain separate consumers.
 for(auto e:{&a,&b,&c})e->Position(LotTilePos::OUT_OF_WORLD);
 a.Direction(Direction::NORTH);
 check(group.ChangePosition(LotTilePos(112,112,1),Direction::NORTH,&ctx).Status==VMPlacementError::Success,"initial off-world group uses ordinary canonical placement");
 check(b.Position()==LotTilePos(112,128,1)&&c.Position()==LotTilePos(128,144,1),"initial placement creates original canonical offsets");
 slot.Offset=st::Vector3();slot.Rsflags=Chunks::SLOTFlags::SnapToDirection;slot.Facing=(Chunks::SLOTFacing)2;
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"explicit self-slot SnapToDirection remains a rotation");
 check(a.Direction()==Direction::EAST&&b.Direction()==Direction::EAST&&c.Direction()==Direction::EAST,"explicit rotation publishes the full group facing");
 check(b.Position()!=LotTilePos(112,128,1),"explicit rotation still rotates asymmetric geometry");
 slot.Rsflags=(Chunks::SLOTFlags)0;slot.Offset=st::Vector3();a.Direction(Direction::NORTH);frame.StackObject=&target;
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"snap to another object keeps ordinary placement");
 check(a.Position()==target.Position()&&b.Position()==target.Position()+LotTilePos(0,16,0),"other-object snap still establishes canonical orientation");
 // Single-part object and avatar continue through their existing path.
 Entities::VMMultitileGroup single;single.AddDynamicObject(&target,LotTilePos());target.MultitileGroup=&single;
 target.SetValue(VMStackObjectVariable::Flags,int16_t(VMEntityFlags::HasZeroExtent));target.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));target.SetValue(VMStackObjectVariable::AllowedHeightFlags,1);
 frame.Caller=&target;frame.StackObject=&target;slot.Offset=st::Vector3(0,-6,0);target.Direction(Direction::NORTH);auto pt=target.Position();
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE&&target.Position()==pt+LotTilePos(0,-6,0),"single-part object self-snap unchanged");
 VMAvatar avatar(&object);Entities::VMMultitileGroup ag;ag.AddDynamicObject(&avatar,LotTilePos());avatar.MultitileGroup=&ag;avatar.Position(LotTilePos(112,112,1));avatar.Direction(Direction::NORTH);avatar.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));frame.Caller=&avatar;frame.StackObject=&avatar;
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE&&avatar.Position()==LotTilePos(112,106,1),"avatar self-snap unchanged");
 // Asymmetric masks exercise the distinction between facing and physical shape.
 frame.Caller=&a;frame.StackObject=&a;slot.Offset=st::Vector3(0,-6,0);slot.Rsflags=(Chunks::SLOTFlags)0;
 def.FootprintMask=0x0008; // NORTH bottom half; EAST left half: not rotation invariant.
 check(group.ChangePosition(LotTilePos(128,128,1),Direction::NORTH,&ctx).Status==VMPlacementError::Success,"establish asymmetric solid group through normal placement");
 check(group.CanTranslate(),"coherent asymmetric masks are eligible, not just square masks");
 Chunks::OBJD blockerDef;FSO::Content::GameObject blockerObject;blockerObject.OBJ=&blockerDef;blockerObject.Resource=&resource;
 VMGameObject blocker(&blockerObject,nullptr);blocker.ObjectID=9;Entities::VMMultitileGroup blockerGroup;blockerGroup.AddDynamicObject(&blocker,LotTilePos());blocker.MultitileGroup=&blockerGroup;
 auto blockAt=[&](LotTilePos pos){blocker.Position(pos);blocker.Footprint=blocker.GetObstacle(pos,Direction::NORTH);ctx.RoomInfo[0].Entities.push_back(&blocker);};
 auto unblock=[&](){auto& es=ctx.RoomInfo[0].Entities;es.erase(std::remove(es.begin(),es.end(),&blocker),es.end());};
 const std::array<Box,3> asymBefore{box(a),box(b),box(c)};const std::array<LotTilePos,3> asymPose{a.Position(),b.Position(),c.Position()};
 const std::array<Routing::VMObstacle*,3> asymPointers{a.Footprint.get(),b.Footprint.get(),c.Footprint.get()};
 blockAt(LotTilePos(128,138,1));
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_FALSE,"real blocker of a child rejects coherent asymmetric translation");
 check(a.Position()==asymPose[0]&&b.Position()==asymPose[1]&&c.Position()==asymPose[2]&&box(a)==asymBefore[0]&&box(b)==asymBefore[1]&&box(c)==asymBefore[2],"blocked asymmetric translation returns positions and mask bounds unchanged");
 check(a.Footprint.get()==asymPointers[0]&&b.Footprint.get()==asymPointers[1]&&c.Footprint.get()==asymPointers[2],"blocked asymmetric translation restores original footprint objects");
 check(a.GetValue(VMStackObjectVariable::PrimitiveResultID)==9,"real blocker identity reaches normal Snap failure consumer");unblock();
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"coherent asymmetric translation succeeds after blocker removed");
 const auto asymDelta=a.Position()-asymPose[0];check(box(a)==shifted(asymBefore[0],asymDelta)&&box(b)==shifted(asymBefore[1],asymDelta)&&box(c)==shifted(asymBefore[2],asymDelta),"successful asymmetric translation preserves complete mask geometry");
 check(group.ChangePosition(LotTilePos(128,128,1),Direction::NORTH,&ctx).Status==VMPlacementError::Success,"restore authored NORTH orientation for pending-rotation regression");
 const std::array<Box,3> pendingFeet{box(a),box(b),box(c)};const std::array<LotTilePos,3> pendingPose{a.Position(),b.Position(),c.Position()};
 const std::array<Routing::VMObstacle*,3> pendingPointers{a.Footprint.get(),b.Footprint.get(),c.Footprint.get()};
 a.Direction(Direction::EAST);check(!group.CanTranslate(),"caller-only facing write with stale NORTH mask does not qualify as translation");
 blockAt(LotTilePos(118,128,1));
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_FALSE,"pending asymmetric rotation uses ordinary placement and detects its actual child blocker");
 check(a.Position()==pendingPose[0]&&b.Position()==pendingPose[1]&&c.Position()==pendingPose[2]&&a.Direction()==Direction::EAST&&b.Direction()==Direction::NORTH&&c.Direction()==Direction::NORTH,"rejected ordinary rotation preserves previous positions and logical facings");
 check(box(a)==pendingFeet[0]&&box(b)==pendingFeet[1]&&box(c)==pendingFeet[2]&&a.Footprint.get()==pendingPointers[0]&&b.Footprint.get()==pendingPointers[1]&&c.Footprint.get()==pendingPointers[2],"rejected ordinary rotation does not silently rebuild EAST collision masks");unblock();
 check(snap.Execute(&frame,&op)==VMPrimitiveExitCode::GOTO_TRUE,"unblocked pending rotation retains ordinary placement success");
 check(a.Direction()==Direction::EAST&&b.Direction()==Direction::EAST&&c.Direction()==Direction::EAST&&group.CanTranslate(),"ordinary rotation commits facing and refreshed collision geometry together");
 std::puts("self-snap translation contract passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"self-snap translation contract: %s\n",e.what());return 1;}}
