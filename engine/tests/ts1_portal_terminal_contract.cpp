// SPDX-License-Identifier: MPL-2.0
// Source no-stop terminal branch through real Avatar/Route consumers.
// Synthetic resources/placement responses do not establish DVD scene parity.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define private public
#include "simantics/engine/VMRoutingFrame.h"
#undef private
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/primitives/VMLookTowards.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "content/ContentManager.h"
#include <iostream>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Engine::Routing;
namespace IFF=FSO::Files::Formats::IFF;
using Pos=FSO::LotView::Model::LotTilePos;
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool Near(float a,float b){return std::abs(a-b)<.00002f;}
struct Probe:VMAvatar{
 using VMAvatar::VMAvatar;
 bool Reject=false;int Publications=0;Pos Proposed;
 VMPlacementResult SetPosition(const Pos& p,DirectionT,VMContext*)override{
  ++Publications;Proposed=p;
  if(Reject)return VMPlacementResult(VMPlacementError::CantIntersectOtherObjects);
  Position(p);return VMPlacementResult(VMPlacementError::Success);
 }
};
struct Fixture{
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IFF::IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"terminal-contract",true};
 IFF::Chunks::OBJD Def;FSO::Content::GameObject Object;
 std::unique_ptr<Probe> Actor;std::unique_ptr<VMGameObject> BodyTarget;
 VMRoutine BodyRoutine;std::shared_ptr<VMStackFrame> BodyFrame;
 std::shared_ptr<VMRoutingFrame> Route;
 std::shared_ptr<VMAnimationState> Foreground;FSO::Vitaboy::Animation* Walk=nullptr;
 Fixture(){
  Machine.TS1=true;Def.GUID=0xf0620001;Object.GUID=Def.GUID;Object.OBJ=&Def;Object.Resource=&Resource;
  Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
  Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);
  for(auto& heads:Context.SpatialTileHeads)heads.assign(64,0);
  Actor=std::make_unique<Probe>(&Object);Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
  Actor->Position(Pos(24,24,1));Actor->RadianDirection(1.1f);Machine.AddEntity(Actor.get());
  Context.TS1CommitPhysicalPlacement(Actor.get(),Context.TS1PhysicalRequest(Actor.get(),nullptr,-1));
  Actor->SetPersonData(VMPersonDataVariable::Posture,0);Actor->SetPersonData(VMPersonDataVariable::Gender,0);
  // A real person template supplies floor/terrain placement permissions.
  // This isolated resource must establish them before the real ready poll.
  Actor->SetValue(VMStackObjectVariable::PlacementFlags,
   int(VMPlacementFlags::OnTerrain)|int(VMPlacementFlags::OnFloor));
  for(int i=0;i<32;++i){
   const auto name="fixture-terminal-"+std::to_string(i);Actor->WalkAnimations[i]=Actor->SwimAnimations[i]=name;
   auto* a=FSO::Content::Content::Get()->AvatarAnimations->Get(name+".anim");
   Check(a!=nullptr,"fixture numeric animation resource unavailable");
   a->Duration=1000;a->Distance=3;a->Motions.resize(1);a->Motions[0].FrameCount=30;
  }
  Walk=FSO::Content::Content::Get()->AvatarAnimations->Get(Actor->WalkAnimations[20]+".anim");
  Foreground=std::make_shared<VMAnimationState>(Walk,false);Actor->StartTS1Animation(Foreground,1.4f);
  Foreground->Loop=true;Foreground->TS1Phase=.25f;Actor->Animations={Foreground};
  auto& c=Actor->TS1AnimationController;c.MotionCode=20;c.Moving=c.StepLatched=1;
  Actor->SetValue(VMStackObjectVariable::RoutePrefference,1);
  Route=std::make_shared<VMRoutingFrame>();Route->Thread=Actor->Thread.get();Route->Caller=Route->Callee=Actor.get();Route->CodeOwner=&Object;
  Route->State=VMRoutingFrameState::WALKING;Route->WalkTo=std::make_shared<std::list<st::Point>>();
  Route->CurRoute=std::make_shared<VMFindLocationResult>();Route->CurRoute->Position=Pos(26,24,1);
  Route->CurRoute->FaceAnywhere=false;Route->CurRoute->RadianDirection=-1.f;
  Route->CurrentWaypoint=Route->CurRoute->Position;Route->PreviousPosition=Pos(16,24,1);
  Actor->Thread->Stack.push_back(Route);
  Check(Actor->StartTS1MotionRequest(Route.get(),TS1MotionKind::Route,{{16,24},{26,24},{26,8}})!=0,"fixture real route request rejected");
  Actor->TS1MotionRate=1.4f;Actor->SetTS1MotionModelState(9);
  c.MotionStepKnown=true;c.MotionStepX=.3f;c.MotionStepY=.4f;
 }
 ~Fixture(){
  if(BodyTarget&&Machine.GetObjectById(BodyTarget->ObjectID)==BodyTarget.get())Machine.RemoveEntity(BodyTarget.get());
  if(Machine.GetObjectById(Actor->ObjectID)==Actor.get())Machine.RemoveEntity(Actor.get());
 }
 VMPrimitiveExitCode Body(){
  using namespace FSO::SimAntics::Primitives;
  if(!BodyFrame){
   Actor->DetachTS1MotionRequest(Actor->TS1ModelMotion.Token);Actor->Thread->Stack.clear();
   BodyTarget=std::make_unique<VMGameObject>(&Object,nullptr);BodyTarget->Position(Pos(26,24,1));
   Machine.AddEntity(BodyTarget.get());BodyTarget->SetValue(VMStackObjectVariable::Flags,16);
   Context.TS1CommitPhysicalPlacement(BodyTarget.get(),Context.TS1PhysicalRequest(BodyTarget.get(),nullptr,-1));
   BodyRoutine.ID=4096;BodyRoutine.Instructions.resize(1);BodyRoutine.Instructions[0].Opcode=22;
   BodyFrame=std::make_shared<VMStackFrame>();BodyFrame->Thread=Actor->Thread.get();
   BodyFrame->Caller=BodyFrame->Callee=Actor.get();BodyFrame->StackObject=BodyTarget.get();
   BodyFrame->CodeOwner=&Object;BodyFrame->Routine=&BodyRoutine;Actor->Thread->Stack.push_back(BodyFrame);
  }
  VMLookTowards primitive;VMLookTowardsOperand operand;operand.Mode=static_cast<VMLookTowardsMode>(2);
  return primitive.Execute(BodyFrame.get(),&operand);
 }
 void Step(){Actor->UpdateTS1ModelMotion();++Context.Clock->Ticks;}
};
void Admission(){
 for(int alternate:{0,1})for(int direct:{0,1})for(int latched:{0,1}){
  Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;auto& m=a.TS1ModelMotion;
  a.SetValue(VMStackObjectVariable::RoutePrefference,alternate);
  a.SetPersonData(static_cast<VMPersonDataVariable>(64),direct);c.StepLatched=latched;
  const auto physical=a.Position();const auto heading=a.RadianDirection();
  const int result=a.PrepareTS1NoStopTerminal(26,24,-1);
  Check(result==(!alternate?0:direct?(latched?3:2):1),"OD9/PD64/step-latch source branch selection differs");
  Check(a.Position()==physical&&Near(a.TS1MotionX,24)&&Near(a.TS1MotionY,24),"terminal preparation must not snap current or physical pose to goal");
  if(!alternate){
   Check(m.Status==1&&m.ModelState==9&&a.CurrentAnimationState()==f.Foreground.get(),"OD9zero must leave admission and foreground to ordinary terminal consumer");
   continue;
  }
  if(!direct){
   Check(a.CurrentAnimationState()==f.Foreground.get()&&c.MotionCode==20&&f.Foreground->TS1Phase==.25f&&Near(a.RadianDirection(),heading),"no-stop preparation must preserve foreground/code/phase/heading");
   Check(m.StopDeadline==0&&c.StopLatched==1,"no-stop branch must not create common deadline");
  }else if(latched){
   Check(c.ModelState==7&&a.CurrentAnimationState()->PlayingBackwards&&a.CurrentAnimationState()->TS1Phase==1,
    "swim/direct latched branch must use reverse source19 state7");
   Check(m.Outcome==1,"portal reverse transition must retain its source continuation outcome1");
  }
 }
 Fixture ordinary;ordinary.Actor->SetValue(VMStackObjectVariable::RoutePrefference,0);
 Check(ordinary.Actor->PrepareTS1NoStopTerminal(26,24,-1)==0,"ordinary route must not enter no-stop branch");
 Fixture unknown;unknown.Actor->TS1AnimationController.MotionStepKnown=false;
 Check(unknown.Actor->PrepareTS1NoStopTerminal(26,24,-1)==0&&!unknown.Actor->TS1AnimationController.MotionStepKnown,
  "old continuation must not invent analytical motion history");
 Check(unknown.Actor->StartTS1MotionRequest(unknown.Route.get(),TS1MotionKind::Route,{{24,24},{26,24},{26,8}})!=0
  &&!unknown.Actor->TS1AnimationController.MotionStepKnown,"new request must not invent missing old analytical history");
 Fixture fresh;fresh.Actor->TS1AnimationController.MotionStepKnown=false;
 fresh.Route->CurRoute->Position=Pos(72,24,1);fresh.Route->CurrentWaypoint=Pos(72,24,1);
 fresh.Step();
 Check(fresh.Actor->Publications==1&&fresh.Actor->TS1AnimationController.MotionStepKnown,
  "an actual accepted ordinary movement must initialize previously unknown analytical history");
 Fixture noForeground;noForeground.Actor->ClearTS1MotionAnimation();
 Check(noForeground.Actor->PrepareTS1NoStopTerminal(26,24,-1)==2&&noForeground.Actor->TS1ModelMotion.Status==2,
  "foreground-less portal must become ready at current pose without fake extra step");
}
void ThresholdAdmission(){
 for(int style:{0,1})for(bool inside:{false,true}){
  Fixture f;auto& a=*f.Actor;auto& m=a.TS1ModelMotion;
  a.SetValue(VMStackObjectVariable::WalkStyle,style);
  const float threshold=style?4.5f:3.f;
  // Use the model fraction to exercise exact source boundary with an integer goal.
  const int goal=style?29:27;
  a.TS1MotionX=float(goal)-threshold+(inside?.0001f:0.f);
  f.Route->CurRoute->Position=Pos(goal,24,1);f.Route->CurrentWaypoint=Pos(goal,24,1);
  f.Step();
  Check(inside?m.Status==2:m.Status==1,
   "source portal admission must stay moving at1.5*style threshold equality, finishing only inside it");
  Check(inside?a.TS1AnimationController.StopLatched==1:a.TS1AnimationController.StopLatched==0,
   "unadmitted portal distance must not manufacture terminal stop history");
 }
}
void RealConsumer(){
 Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;auto& m=a.TS1ModelMotion;
 const auto token=m.Token;const auto phase=f.Foreground->TS1Phase;const auto heading=a.RadianDirection();
 f.Step();
 Check(a.Publications==1&&a.Proposed.y==24,"real route no-stop must use normal placement and segment constraint");
 Check(Near(a.TS1MotionX,24.56448f)&&Near(a.TS1MotionY,24.75264f),"proposal must follow retained analytical direction with resource magnitude, not final anchor");
 Check(c.MotionStepKnown&&Near(c.MotionStepX,.56448f)&&Near(c.MotionStepY,.75264f),"accepted ordinary proposal must refresh analytical delta independent of rounded physical movement");
 Check(m.Status==2&&m.ModelState==3&&m.Outcome==1&&m.Token==token&&m.StopDeadline==0,"same-model-update terminal must publish ready without retiring owner or creating stop deadline");
 Check(a.CurrentAnimationState()==f.Foreground.get()&&c.MotionCode==20&&f.Foreground->TS1Phase==phase&&Near(a.RadianDirection(),heading),
  "real portal terminal must preserve foreground phase and current heading instead of final facing");
 const int before=a.Publications;const auto constrained=a.Position();
 Check(a.PollTS1MotionRequest(token)==1&&a.Publications==before+1&&a.Position()==constrained,"ready request must validate/publish through actual script poll");
 Check(a.StopTS1MotionRequest(token),"ready portal request must stop through normal poll lifecycle");a.DetachTS1MotionRequest(token);
 Check(c.MotionCode==20&&c.StopLatched==1&&c.MotionStepKnown&&Near(c.MotionStepX,.56448f),"stop/detach must retain independent model/channel direction history");
 Check(a.StartTS1MotionRequest(f.Route.get(),TS1MotionKind::Route,{{24,24},{28,24},{28,8}})!=0&&c.MotionStepKnown&&Near(c.MotionStepY,.75264f),
  "next request must inherit known source direction rather than rounded pointer/Velocity reconstruction");
}
void FullPathReadyPoll(){
 Fixture f;auto& a=*f.Actor;auto& m=a.TS1ModelMotion;
 m.Path={{24,24},{40,24},{40,40},{40,56}};m.PathCursor=1;m.Status=2;
 a.TS1MotionX=30.3f;a.TS1MotionY=24.1f;
 Check(a.PollTS1MotionRequest(m.Token)==1&&a.Proposed==Pos(30,24,1),
  "ready poll must round model pose then constrain over all physical segments, excluding orientation tangent");
 Check(Near(a.TS1MotionX,30.3f)&&Near(a.TS1MotionY,24.1f),
  "script physical publication must not overwrite independent fractional model pose");
}
void BeginPublishedHeading(){
 for(bool known:{false,true})for(int diagonal:{0,1}){
  Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;
  const float incoming=diagonal?2.f:1.1f;
  const float published=diagonal?float(3*M_PI/4):float(M_PI/4);
  a.RadianDirection(incoming);a.TS1MotionX=37.5f;a.TS1MotionY=44.25f;
  c.Moving=c.StopLatched=1;c.MotionStepKnown=known;c.MotionStepX=.125f;c.MotionStepY=-.25f;
  f.Foreground->TS1Phase=.37f;
  const auto token=a.TS1ModelMotion.Token;
  Check(a.StartTS1MotionRequest(f.Route.get(),TS1MotionKind::Route,{{24,24},{40,24},{40,40},{40,56}})!=0
   &&a.TS1ModelMotion.Token!=token,"new full-path request must obtain its own owner token");
  Check(Near(a.RadianDirection(),known?published:incoming)
   &&Near(a.TS1ModelMotion.TargetHeading,known?published:incoming),
   "source Begin must rebase fractional heading from published eight-way physical direction, preserving unknown older adapter");
  Check(Near(a.TS1MotionX,24)&&Near(a.TS1MotionY,24)&&a.Position()==Pos(24,24,1),
   "Begin must copy committed physical position rather than retain prior fractional model pose");
  Check(c.Moving==1&&c.StopLatched==1&&c.MotionStepKnown==known
   &&c.MotionStepX==.125f&&c.MotionStepY==-.25f&&c.MotionCode==20
   &&a.CurrentAnimationState()==f.Foreground.get()&&f.Foreground->TS1Phase==.37f,
   "Begin must preserve independent moving/stop/analytical/channel history while rebasing pose");
  const auto& path=a.TS1ModelMotion.Path;
  Check(path.size()==4&&path[0].X==24&&path[1].X==40&&path[2].Y==40&&path[3].Y==56,
   "full-path Begin must preserve physical segment sequence and separate orientation point");
 }
}
void SharedBodyConsumer(){
 Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;
 Check(f.Body()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&a.TS1ModelMotion.Kind==TS1MotionKind::Body,
  "real look-towards producer must preserve OD9 and establish a body motion owner");
 const float heading=a.RadianDirection();const auto phase=f.Foreground->TS1Phase;
 f.Step();
 Check(a.TS1ModelMotion.Status==2&&a.TS1ModelMotion.Outcome==1&&Near(a.TS1MotionX,24.56448f)&&Near(a.TS1MotionY,24.75264f),
  "shared body FSM must execute no-stop analytical proposal, not snap its duplicate self anchor");
 Check(a.CurrentAnimationState()==f.Foreground.get()&&f.Foreground->TS1Phase==phase&&c.MotionCode==20&&Near(a.RadianDirection(),heading),
  "body alternate route must retain current heading/channel rather than final object-facing turn");
 Check(f.Body()==VMPrimitiveExitCode::GOTO_TRUE&&a.Position()==Pos(24,24,1)&&a.TS1ModelMotion.Kind==TS1MotionKind::None,
  "real primitive continuation must poll/publish constrained duplicate physical self segment and retire owner");
 Fixture unknown;unknown.Actor->TS1AnimationController.MotionStepKnown=false;unknown.Body();unknown.Step();
 Check(!unknown.Actor->TS1AnimationController.MotionStepKnown&&unknown.Actor->CurrentAnimationState()!=unknown.Foreground.get(),
  "older body continuation must retain explicit legacy fallback until analytical history exists");
 Fixture normal;normal.Actor->SetValue(VMStackObjectVariable::RoutePrefference,0);
 normal.Foreground->TS1ScriptInterruptible=1;normal.Body();normal.Step();
 Check(normal.Actor->TS1ModelMotion.StopDeadline!=0&&normal.Actor->TS1ModelMotion.Status!=2,
  "ordinary body-facing path must retain its existing queued final stop window");
 Fixture blocked;blocked.Body();blocked.Actor->Reject=true;blocked.Step();
 Check(blocked.Actor->TS1ModelMotion.Status==3&&blocked.Actor->TS1ModelMotion.Outcome==3
  &&blocked.Actor->TS1ModelMotion.PendingCollisionStatus==-1&&blocked.Actor->Position()==Pos(24,24,1)
  &&Near(blocked.Actor->TS1MotionX,24)&&Near(blocked.Actor->TS1MotionY,24),
  "body rejected proposal must fail its model request without route-only collision staging or pose mutation");
 Check(blocked.Body()==VMPrimitiveExitCode::GOTO_TRUE&&blocked.Actor->TS1ModelMotion.Kind==TS1MotionKind::None,
  "original look-towards maps failed model poll0 to true and retires ownership through Stop");
 for(int latched:{0,1}){
  Fixture swim;swim.Actor->SetPersonData(static_cast<VMPersonDataVariable>(64),1);
  swim.Actor->TS1AnimationController.StepLatched=latched;swim.Body();swim.Step();
  Check(latched?swim.Actor->TS1ModelMotion.ModelState==7:swim.Actor->TS1ModelMotion.Status==2,
   "body direct/swim branch must distinguish absent step from source reverse transition");
 }
}
void CollisionAndOrdinaryWindow(){
 Fixture f;auto& a=*f.Actor;auto& m=a.TS1ModelMotion;const auto pose=a.Position();a.Reject=true;
 f.Step();
 Check(m.PendingCollisionStatus==static_cast<int16_t>(VMPlacementError::CantIntersectOtherObjects)&&m.Status!=2,
  "rejected no-stop proposal must stage physical collision, not report ready or execute child script");
 Check(a.Position()==pose&&Near(a.TS1MotionX,24)&&Near(a.TS1MotionY,24)&&a.Thread->Stack.size()==1,
  "failed proposal must preserve committed/model pose and owning script stack");
 Check(a.TS1AnimationController.MotionStepKnown&&Near(a.TS1AnimationController.MotionStepX,.56448f)&&Near(a.TS1AnimationController.MotionStepY,.75264f),
  "source analytical proposal is recorded before placement query even when the resulting move is rejected");
 Fixture normal;auto& b=*normal.Actor;b.SetValue(VMStackObjectVariable::RoutePrefference,0);
 normal.Foreground->TS1ScriptInterruptible=1;
 b.TS1ModelMotion.StopDistance1=b.TS1ModelMotion.StopDistance2=4;
 normal.Step();
 Check(b.TS1ModelMotion.StopDeadline!=0&&b.TS1ModelMotion.Status!=2,
  "ordinary OD9zero route must retain the common queued stop-window contract");
}
}
int main(){try{VM::UseWorld(false);Admission();ThresholdAdmission();RealConsumer();FullPathReadyPoll();BeginPublishedHeading();SharedBodyConsumer();CollisionAndOrdinaryWindow();std::cout<<"PASS portal terminal controls: "<<checks<<" checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
