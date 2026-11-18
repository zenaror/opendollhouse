// SPDX-License-Identifier: MPL-2.0
// Resource-free TS1 dispatch/removal contract. Synthetic actors execute the real
// remove primitive, result dispatcher, VM sweep/clock and removal queue.
// This does not certify all DVD physical publication or multipart behavior.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/primitives/VMRemoveObjectInstance.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "simantics/utils/VMTS1LotSaver.h"
#include "files/formats/iff/IffRawFile.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static int checks=0;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
template<class T,typename T::Type Member>struct Access{friend typename T::Type field(T){return Member;}};
struct Dispatch{using Type=void(VMThread::*)(VMStackFrame*,VMInstruction*,VMPrimitiveExitCode);friend Type field(Dispatch);};
template struct Access<Dispatch,&VMThread::HandleResult>;
struct Probe:VMGameObject{
 std::function<void()> Body,BeforeRemoval;
 Probe(FSO::Content::GameObject* object):VMGameObject(object,nullptr){}
 void Tick()override{if(Body)Body();}
 void PrePositionChange(VMContext*)override{if(BeforeRemoval)BeforeRemoval();}
};
struct Fixture{
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"deferred-removal",true};
 OBJD Def;FSO::Content::GameObject Object;Entities::VMMultitileGroup ActorGroup,AGroup,BGroup,CGroup;
 std::unique_ptr<Probe> Actor,A,B,C;VMRoutine Routine;
 Fixture(){
  Machine.GlobalState.assign(64,0);
  Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
  Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);
  Def.GUID=0x1234abcd;Object.OBJ=&Def;Object.GUID=Def.GUID;Object.Resource=&Resource;
  auto add=[&](std::unique_ptr<Probe>& p,Entities::VMMultitileGroup& g){p=std::make_unique<Probe>(&Object);p->MultitileGroup=&g;g.AddObject(p.get());Machine.AddEntity(p.get());p->Thread=std::make_unique<VMThread>(&Context,p.get(),16);};
  add(Actor,ActorGroup);add(A,AGroup);add(B,BGroup);add(C,CGroup);
  Routine.Instructions.resize(1);Routine.Instructions[0].TruePointer=254;Routine.Instructions[0].FalsePointer=255;
 }
 std::shared_ptr<VMStackFrame> frame(VMEntity* target){
  auto f=std::make_shared<VMStackFrame>();f->Routine=&Routine;f->Caller=Actor.get();f->Callee=target;f->StackObject=target;f->Thread=Actor->Thread.get();f->CodeOwner=&Object;f->SpecialResult=VMSpecialResult::Interaction;
  Actor->Thread->Stack.push_back(f);auto q=std::make_shared<VMQueuedAction>();q->UID=77;q->Callee=target;q->ActionRoutine=&Routine;Actor->Thread->Queue.push_back(q);Actor->Thread->ActiveQueueBlock=0;return f;
 }
 VMPrimitiveExitCode request(VMStackFrame* f){VMRemoveObjectInstance handler;VMRemoveObjectInstanceOperand op;op.Target=1;return handler.Execute(f,&op);}
};
static void ordinaryBoundary(){
 Fixture f;auto frame=f.frame(f.A.get());const auto start=f.Context.Clock->Ticks;bool returned=false;std::vector<int> order;
 auto removal=[&](Probe* p){return [&,p]{if(order.empty()||order.back()!=p->ObjectID)order.push_back(p->ObjectID);need(!f.Machine.EntityTickRunning(),"removal ran inside ordinary object dispatch");need(f.Context.Clock->Ticks==start+1,"removal preceded ordinary clock step");};};
 f.A->BeforeRemoval=removal(f.A.get());f.B->BeforeRemoval=removal(f.B.get());f.C->BeforeRemoval=removal(f.C.get());
 bool queuedC=false;auto first=f.A->BeforeRemoval;f.A->BeforeRemoval=[&]{first();if(!queuedC){queuedC=true;auto nested=*frame;nested.StackObject=f.C.get();need(f.request(&nested)==VMPrimitiveExitCode::GOTO_TRUE,"callback removal result");need(!f.C->Dead&&f.Machine.GetObjectById(f.C->ObjectID)==f.C.get(),"callback deletion recursed instead of joining FIFO");}};
 f.Machine.OnTraceFrameReturn=[&](const VMStackFrame* current,int result){need(current==frame.get()&&result==int(VMPrimitiveExitCode::RETURN_TRUE),"authored true edge did not reach normal Pop");need(f.Context.Clock->Ticks==start&&f.Machine.GetObjectById(f.A->ObjectID)==f.A.get()&&!f.A->Dead,"target identity released before authored return");returned=true;};
 f.Actor->Body=[&]{
  need(f.Machine.EntityTickRunning(),"fixture not in real ordinary dispatcher");const auto code=f.request(frame.get());need(code==VMPrimitiveExitCode::GOTO_TRUE,"stack-object remove must continue its true edge");need(!f.A->Dead&&f.Machine.HasTS1PendingRemovals(),"request removed target synchronously");
  need(f.request(frame.get())==code,"duplicate changed primitive result");auto second=*frame;second.StackObject=f.B.get();f.request(&second);
  IffRawFile destination;destination.Set("SIMI",1,{7,8,9});bool refused=false;try{Utils::VMTS1InteractionCheckpoint::Capture(f.Machine,destination,false);}catch(const std::runtime_error& e){refused=std::string(e.what()).find("dispatch")!=std::string::npos;}
  need(refused&&destination.Find("SIMI",1)->Data==std::vector<uint8_t>({7,8,9}),"pending capture did not fail before synchronization");
  VMTS1LotSaver saver(&f.Machine);std::vector<uint8_t> output{1,2,3};need(!saver.BuildHouse("/not-a-test-input",output)&&saver.Error.find("dispatch")!=std::string::npos&&output==std::vector<uint8_t>({1,2,3}),"BuildHouse touched input/output before pending guard");
  (f.Actor->Thread.get()->*field(Dispatch{}))(frame.get(),&f.Routine.Instructions[0],code);
 };
 f.Machine.InternalTick();need(returned,"no attributed normal return");need(f.A->Dead&&f.B->Dead&&f.C->Dead&&!f.Machine.HasTS1PendingRemovals(),"drain left live targets or pending work");need(order==std::vector<int>({f.A->ObjectID,f.B->ObjectID,f.C->ObjectID}),"duplicate/callback requests changed FIFO order");
}
static void isolatedSandbox(){
 Fixture f;f.Actor->Body=[&]{auto request=f.frame(f.A.get());f.request(request.get());auto state=f.Machine.Sandbox();
  Probe preview(&f.Object);Entities::VMMultitileGroup group;preview.MultitileGroup=&group;group.AddObject(&preview);f.Machine.AddEntity(&preview);auto temporary=*request;temporary.StackObject=&preview;f.request(&temporary);need(f.Machine.HasTS1PendingRemovals(),"sandbox request missing");f.Machine.SandboxRestore(std::move(state));need(!preview.Dead&&f.Machine.GetObjectById(f.Actor->ObjectID)==f.Actor.get(),"sandbox removal leaked to restored identity domain");
 };
 f.Machine.InternalTick();need(f.A->Dead&&!f.Actor->Dead&&!f.B->Dead&&!f.C->Dead&&!f.Machine.HasTS1PendingRemovals(),"sandbox failed to restore original pending requests exclusively");
}
static void identityAndGroup(){
 {Fixture f;std::unique_ptr<Probe> replacement;Entities::VMMultitileGroup group;const int id=f.A->ObjectID;
  f.Actor->Body=[&]{auto req=f.frame(f.A.get());f.request(req.get());f.A->Delete(true,&f.Context);replacement=std::make_unique<Probe>(&f.Object);replacement->MultitileGroup=&group;group.AddObject(replacement.get());f.Machine.AddEntity(replacement.get());need(replacement->ObjectID==id,"fixture did not reuse queued ID");};
  f.Machine.InternalTick();need(!replacement->Dead&&f.Machine.GetObjectById(id)==replacement.get()&&!f.Machine.HasTS1PendingRemovals(),"stale queued ID removed a replacement identity");
 }
 {Fixture f;f.BGroup.RemoveObject(f.B.get());f.B->MultitileGroup=&f.AGroup;f.AGroup.AddObject(f.B.get());
  f.Actor->Body=[&]{VMStackFrame req;req.Caller=f.Actor.get();req.StackObject=f.A.get();req.Thread=f.Actor->Thread.get();f.request(&req);need(!f.A->Dead&&!f.B->Dead,"multipart request removed members early");
   f.CGroup.RemoveObject(f.C.get());f.C->MultitileGroup=&f.AGroup;f.AGroup.AddObject(f.C.get());
  };
  f.Machine.InternalTick();need(f.A->Dead&&f.B->Dead&&f.C->Dead&&f.AGroup.Objects.empty(),"queued removal lost existing whole-group cleanup behavior");
 }
}
static void failedDrainBlocksPublication(){
 Fixture f;int sweeps=0;f.Actor->Body=[&]{++sweeps;auto req=f.frame(f.A.get());f.request(req.get());};
 f.A->BeforeRemoval=[](){throw std::runtime_error("injected deletion failure");};bool failed=false;
 try{f.Machine.InternalTick();}catch(const std::runtime_error& e){failed=std::string(e.what())=="injected deletion failure";}
 need(failed&&f.Machine.HasTS1PendingRemovals(),"failed singleton drain lost the save barrier");const auto tick=f.Context.Clock->Ticks;
 IffRawFile destination;destination.Set("SIMI",1,{4,5,6});bool refused=false;try{Utils::VMTS1InteractionCheckpoint::Capture(f.Machine,destination,false);}catch(const std::runtime_error&e){refused=std::string(e.what()).find("dispatch")!=std::string::npos;}
 need(refused&&destination.Find("SIMI",1)->Data==std::vector<uint8_t>({4,5,6}),"incomplete deletion published a checkpoint");
 VMTS1LotSaver saver(&f.Machine);std::vector<uint8_t> output{9};need(!saver.BuildHouse("/not-a-test-input",output)&&saver.Error.find("dispatch")!=std::string::npos&&output==std::vector<uint8_t>({9}),"failed drain bypassed BuildHouse guard");
 bool stopped=false;try{f.Machine.InternalTick();}catch(const std::runtime_error&e){stopped=std::string(e.what()).find("reload")!=std::string::npos;}
 need(stopped&&sweeps==1&&f.Context.Clock->Ticks==tick,"failed drain resumed another ordinary sweep");
}
static void preservedImmediatePaths(){
 {Fixture f;auto frame=f.frame(f.A.get());need(f.request(frame.get())==VMPrimitiveExitCode::GOTO_TRUE,
   "outside-sweep script removal did not follow TRUE");
  need(!f.A->Dead&&f.Machine.GetObjectById(f.A->ObjectID)==f.A.get()&&f.Machine.HasTS1PendingRemovals()&&
   f.Machine.CanCaptureTS1RemovalState(),"outside-sweep script request deleted inline or left active execution");
  const auto id=f.A->ObjectID;f.Machine.InternalTick();
  need(f.A->Dead&&!f.Machine.GetObjectById(id)&&!f.Machine.HasTS1PendingRemovals(),
   "outside-sweep script removal did not drain at the next ordinary boundary");}
 {Fixture f;need(!f.Machine.QueueTS1Removal(f.A.get())&&!f.A->Dead&&!f.Machine.HasTS1PendingRemovals(),
   "direct non-script queue request acquired implicit script execution mode");}
 {Fixture f;f.Actor->Body=[&]{f.A->Delete(true,&f.Context);need(f.A->Dead&&!f.Machine.HasTS1PendingRemovals(),"internal rollback delete was deferred");};f.Machine.InternalTick();}
 {Fixture f;f.Machine.TS1=false;auto frame=f.frame(f.A.get());f.Actor->Body=[&]{f.request(frame.get());need(f.A->Dead&&!f.Machine.HasTS1PendingRemovals(),"non-TS1 delete was deferred");};f.Machine.InternalTick();}
}
int main(){try{VM::UseWorld(false);ordinaryBoundary();isolatedSandbox();identityAndGroup();failedDrainBlocksPublication();preservedImmediatePaths();std::cout<<"PASS TS1 deferred removal: "<<checks<<" checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
