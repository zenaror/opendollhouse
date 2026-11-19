// SPDX-License-Identifier: MPL-2.0
// Literal original-PC manager/Sleep/wake cases, without commercial assets.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/primitives/VMSleep.h"
#include "simantics/primitives/VMNotifyOutOfIdle.h"
#include "files/formats/iff/IffFile.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
static int checks=0;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
// Test-only access to the existing half-frame adapter flag; no product API.
template<class Tag,typename Tag::Type Member>struct Access{friend typename Tag::Type field(Tag){return Member;}};
struct AlternateField{using Type=bool VM::*;friend Type field(AlternateField);};
template struct Access<AlternateField,&VM::AlternateTick>;
struct CountedObject : VMGameObject {
    int visits=0;bool DrawRandom=false;uint64_t LastRandom=0;
    explicit CountedObject(FSO::Content::GameObject* def):VMGameObject(def,nullptr){}
    void Tick() override {++visits;if(DrawRandom)LastRandom=Thread->Context->NextRandom(97);}
};

struct CountedAvatar:VMAvatar {
    int visits=0;
    explicit CountedAvatar(FSO::Content::GameObject* def):VMAvatar(def){}
    void Tick()override{++visits;VMAvatar::Tick();}
};
static void restrictedOwnerContracts(VM& vm,VMContext& context,FSO::Content::GameObject& object){
    vm.TS1=true;vm.Entities.clear();
    CountedObject independent(&object);independent.ObjectID=2;independent.Thread=std::make_unique<VMThread>(&context,&independent,16);
    FSO::Vitaboy::Animation animation;animation.Motions.resize(1);animation.Motions[0].FrameCount=30;
    CountedAvatar normal(&object);normal.ObjectID=3;normal.Thread=std::make_unique<VMThread>(&context,&normal,16);
    float motives[16],oldMotives[16];std::fill_n(motives,16,42.25f);std::fill_n(oldMotives,16,40.5f);
    normal.RestorePersonData(std::vector<int16_t>(101,0),motives,oldMotives);normal.TS1ModelMotion.LastModelTick=123;
    std::array<int16_t,101> person{};for(size_t i=0;i<person.size();++i)person[i]=normal.GetPersonData(Model::VMPersonDataVariable(i));
    auto clip=std::make_shared<Model::VMAnimationState>(&animation,false);clip->CurrentFrame=7.25f;clip->TS1Phase=.25f;normal.Animations={clip};
    auto* pending=static_cast<CountedObject*>(vm.OwnEntity(new CountedObject(&object)));
    pending->Thread=std::make_unique<VMThread>(&context,pending,16);
    auto* group=vm.OwnGroup(new Entities::VMMultitileGroup());group->AddObject(pending);pending->MultitileGroup=group;
    vm.AddEntity(pending);check(vm.QueueTS1Removal(pending,true),"pending removal fixture was not registered");
    vm.Entities={pending,&independent,&normal};
    context.Clock->Ticks=232560;context.Clock->TicksPerMinute=30;context.Clock->MinuteFractions=29;
    context.Clock->Hours=23;context.Clock->Minutes=59;context.Clock->DayOfMonth=30;context.Clock->Month=12;context.Clock->Year=1997;context.Clock->FirePercent=100;
    vm.GlobalState[28]=1234;independent.ObjectData[int(Model::VMStackObjectVariable::CurrentValue)]=500;
    context.RandomSeed=3784694001u;context.TS1SimulationRandomSeed=1918150980u;
    independent.DrawRandom=true;independent.TS1SchedulerState=0x3ffff;normal.TS1SchedulerState=0x1ffff;pending->TS1SchedulerState=0x20002;
    vm.GlobalState[17]=1;vm.GlobalState[19]=0;vm.GlobalState[10]=7;
    vm.InternalTick();
    check(independent.visits==1&&independent.LastRandom==13&&context.TS1SimulationRandomSeed==2822175277u&&context.RandomSeed==3784694001u,"restricted owner callback used shared RNG or made an extra draw");
    independent.DrawRandom=false;
    for(int mode:{0,1,2,3}){
        vm.GlobalState[17]=mode==0?1:0;vm.GlobalState[19]=mode;vm.GlobalState[10]=7;
        check(vm.TS1RestrictedDispatch(),"pause or occupied edit mode did not select restricted source dispatch");
        independent.TS1SchedulerState=0x30001;normal.TS1SchedulerState=0x10005;
        const auto visits=independent.visits;vm.InternalTick();
        check(independent.visits==visits&&independent.TS1SchedulerDelay()==0&&normal.TS1SchedulerDelay()==5,"actual restricted pass did not gate before decrementing independent/normal delays");
        vm.InternalTick();check(independent.visits==visits+1,"actual restricted pass did not admit eligible zero delay");
    }
    vm.GlobalState[17]=0;vm.GlobalState[19]=3;vm.GlobalState[10]=0;
    check(!vm.TS1RestrictedDispatch(),"source house0 exception was turned into a generic edit pause");
    vm.GlobalState[17]=-1;check(vm.TS1RestrictedDispatch(),"nonzero signed pause flag was normalized incorrectly");
    vm.GlobalState[17]=1;vm.GlobalState[19]=0;vm.GlobalState[10]=7;independent.TS1SchedulerState=0x3ffff;
    for(bool ready:{false,true})for(bool alternate:{false,true}){
        vm.Ready=ready;vm.*field(AlternateField{})=alternate;const auto visits=independent.visits;vm.DispatchTS1Owner();
        check(independent.visits==visits+1&&vm.Ready==ready&&vm.*field(AlternateField{})==alternate,"restricted owner entry changed fractional scheduling or skipped the source pass");
    }
    check(normal.visits==0,"restricted passes admitted a normal avatar Tick");
    for(int i=0;i<16;++i)check(normal.GetMotiveDataFloat(Model::VMMotive(i))==motives[i]&&normal.GetOldMotiveDataFloat(Model::VMMotive(i))==oldMotives[i],"restricted passes advanced normal current/old motives");
    for(size_t i=0;i<person.size();++i)check(normal.GetPersonData(Model::VMPersonDataVariable(i))==person[i],"restricted passes changed normal person state");
    check(clip->CurrentFrame==7.25f&&clip->TS1Phase==.25f&&normal.TS1ModelMotion.LastModelTick==123,"restricted owner entry advanced a normal model or pose");
    check(context.Clock->Ticks==232560&&context.Clock->MinuteFractions==29&&context.Clock->Hours==23&&context.Clock->Minutes==59&&context.Clock->DayOfMonth==30&&context.Clock->Month==12&&context.Clock->Year==1997&&context.Clock->FirePercent==100&&vm.GlobalState[28]==1234&&independent.GetValue(Model::VMStackObjectVariable::CurrentValue)==500,"restricted passes advanced time, calendar, fire recovery or daily value state");
    check(!pending->Dead&&pending->TS1SchedulerState==0x20002&&vm.GetTS1PendingRemovalIDs()==std::vector<int16_t>{pending->ObjectID},"restricted passes admitted disabled independent state or drained pending removal");
    // Positive control: the same owner entry resumes ordinary time and the
    // queued removal at the first unrestricted source sweep.
    context.Clock->MinuteFractions=19;context.Clock->Hours=7;context.Clock->Minutes=11;context.Clock->DayOfMonth=18;context.Clock->Month=6;context.Clock->Year=1997;
    vm.Entities={pending,&independent};vm.GlobalState[17]=0;vm.GlobalState[19]=0;vm.GlobalState[10]=7;
    check(!vm.TS1RestrictedDispatch(),"normal mode remained restricted");vm.DispatchTS1Owner();
    check(context.Clock->Ticks==232561&&pending->Dead&&!vm.HasTS1PendingRemovals(),"first ordinary source sweep failed to resume time or drain deferred removal");
    vm.Entities.clear();
}
int main(){try{
    VM::UseWorld(false);
    VMContext context(nullptr);VM vm(&context,nullptr);vm.GlobalState.resize(64,0);
    FSO::Files::Formats::IFF::IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"scheduler-fixture",true);
    FSO::Files::Formats::IFF::Chunks::OBJD definition;definition.GUID=0x12345678;
    FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    CountedObject actor(&object);actor.Thread=std::make_unique<VMThread>(&context,&actor,16);
    VMStackFrame frame;frame.Thread=actor.Thread.get();frame.Caller=&actor;frame.Callee=&actor;
    VMSleep sleep;VMSleepOperand operand;
    check(actor.TS1SchedulerState==0x1ffff,"new objects default to enabled, signed delay -1");
    for(uint32_t high:{0x00010000u,0x00090000u,0xabcd0000u})for(int delay:{-32768,-2,-1,0,1,2,163,32767}){
        actor.TS1SchedulerState=high|uint16_t(delay);
        const bool admitted=actor.TS1AdmitSchedulerTick();
        check(admitted==(delay<=0),"only positive enabled delays skip this visit");
        check((actor.TS1SchedulerState&0xffff0000u)==high,"countdown preserves the complete high word");
        check(actor.TS1SchedulerDelay()==(delay>0?delay-1:delay),"countdown changes once before skipping");
    }
    // Restricted source passes (paused or a non-live occupied lot) still
    // visit enabled independent objects, without consuming other delays.
    for(uint32_t high:{0u,0x00010000u,0x00020000u,0x00030000u,0xabcf0000u})
      for(int delay:{-2,-1,0,1,2,163}){
        actor.TS1SchedulerState=high|uint16_t(delay);const auto before=actor.TS1SchedulerState;
        const bool eligible=(high&0x00030000u)==0x00030000u;
        check(actor.TS1AdmitSchedulerTick(true)==(eligible&&delay<=0),"restricted admission ignored an enable/independent gate");
        check(actor.TS1SchedulerState==(eligible&&delay>0?((before&0xffff0000u)|uint16_t(delay-1)):before),"restricted scan changed an ineligible object's countdown or flags");
      }
    for(uint32_t word:{0u,0x00010002u,0x00030004u,0xfedcffffu})for(int16_t literal:{int16_t(0),int16_t(1),int16_t(-1),int16_t(32767)}){
        actor.TS1SchedulerState=word;actor.ObjectData[int(Model::VMStackObjectVariable::SimIndependent)]=literal;
        actor.TS1PublishSimIndependent();
        check(actor.TS1SchedulerState==((word&~0x00020000u)|(literal?0x00020000u:0u)),"lifecycle independent publication lost literal truth or unrelated manager bits");
        check(actor.GetValue(Model::VMStackObjectVariable::SimIndependent)==literal,"manager publication rewrote authored OD60");
    }
    actor.TS1SchedulerState=0x800a3;
    check(!actor.TS1AdmitSchedulerTick()&&actor.TS1SchedulerState==0x800a3,"disabled instances retain their delay and registration");
    actor.TS1SchedulerState=0x100a3;frame.Args={200};
    for(int i=0;i<163;++i)check(!actor.TS1AdmitSchedulerTick()&&frame.Args[0]==200,"saved suspension does not decrement the original argument");
    check(actor.TS1SchedulerDelay()==0&&actor.TS1AdmitSchedulerTick(),"the visit after 163 skipped visits is admitted");
    check(sleep.Execute(&frame,&operand)==VMPrimitiveExitCode::GOTO_TRUE&&frame.Args[0]==0&&actor.TS1SchedulerState==0x1ffff,"saved Sleep completes without waiting another 200 visits");
    for(int value:{-32768,-29536,-1001,-1,0,1,2,32767}){
        actor.TS1SchedulerState=0x9fffe;frame.Args={int16_t(value)};frame.PrimitiveState=7;
        actor.Thread->Interrupt=true;
        const auto result=sleep.Execute(&frame,&operand);
        check(result==(value<=0?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::CONTINUE_NEXT_TICK),"Sleep uses signed arguments and tests completion before decrement");
        check(frame.Args[0]==(value<=0?0:value-1),"Sleep clamps negatives instead of guessing an unsigned duration");
        check(actor.TS1SchedulerState==0x9fffe&&frame.PrimitiveState==7&&actor.Thread->Interrupt,"per-call Sleep leaves manager, interpreter state and unrelated latch intact");
    }
    for(int index:{-1,1}){operand.StackVarToDec=index;bool caught=false;try{sleep.Execute(&frame,&operand);}catch(const VMSimanticsException&){caught=true;}check(caught,"invalid Sleep parameter indices are rejected");}
    operand.StackVarToDec=1;frame.Args={23,200};actor.TS1SchedulerState=0x90000;
    check(sleep.Execute(&frame,&operand)==VMPrimitiveExitCode::GOTO_TRUE&&frame.Args==std::vector<int16_t>({23,0})&&actor.TS1SchedulerState==0x9ffff,"exhausted Sleep clears its selected parameter and preserves flags");
    operand.StackVarToDec=0;
    auto targetFrame=std::make_shared<VMStackFrame>();VMRoutine routine;
    routine.Instructions.emplace_back();targetFrame->Routine=&routine;actor.Thread->Stack={targetFrame};
    frame.StackObject=&actor;VMNotifyOutOfIdle notify;
    for(int opcode:{0,17,2,0x8000}){
        routine.Instructions[0].Opcode=uint16_t(opcode);targetFrame->Args={7,99};
        actor.TS1SchedulerState=0x800a3;actor.Thread->Interrupt=false;
        check(notify.Execute(&frame,nullptr)==VMPrimitiveExitCode::GOTO_TRUE,"Notify returns true");
        const bool sleeping=(opcode&0x7fff)==0;const bool idle=opcode==17;
        check(targetFrame->Args==std::vector<int16_t>({int16_t(sleeping||idle?0:7),99}),"Notify targets argument zero only on Sleep or Idle");
        check(actor.TS1SchedulerState==(sleeping?0x80000u:0x800a3u)&&!actor.Thread->Interrupt,"Notify clears Sleep countdown without enabling a disabled actor or using a generic interrupt");
    }
    actor.TS1SchedulerState=0x800a3;actor.Reset(&context);
    check(actor.TS1SchedulerState==0x8ffff,"deliberate Reset clears the delay without enabling a dormant actor");
    // Exercise the production whole-entity dispatch seam, not just its helper.
    context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
    context.Architecture()->Rooms[0].Map.assign(64,0);context.RoomInfo.resize(1);
    actor.ObjectID=1;actor.TS1SchedulerState=0x80000;vm.Entities={&actor};
    vm.InternalTick();check(actor.visits==0,"ordinary VM dispatch skips the complete disabled entity Tick");
    actor.TS1SchedulerState=0x10001;vm.InternalTick();check(actor.visits==0&&actor.TS1SchedulerDelay()==0,"delay one skips the whole visit even after decrementing to zero");
    vm.InternalTick();check(actor.visits==1,"the next ordinary dispatch invokes the virtual Tick");
    actor.TS1SchedulerState=0x80001;vm.TS1=false;vm.InternalTick();check(actor.visits==2&&actor.TS1SchedulerState==0x80001,"non-TS1 dispatch retains its existing policy");
    vm.Entities.clear();restrictedOwnerContracts(vm,context,object);
    printf("original scheduler checks=%d failures=0\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
