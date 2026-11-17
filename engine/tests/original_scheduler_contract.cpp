// SPDX-License-Identifier: MPL-2.0
// Literal original-PC manager/Sleep/wake cases, without commercial assets.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/primitives/VMSleep.h"
#include "simantics/primitives/VMNotifyOutOfIdle.h"
#include "files/formats/iff/IffFile.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
static int checks=0;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;}
struct CountedObject : VMGameObject {
    int visits=0;
    explicit CountedObject(FSO::Content::GameObject* def):VMGameObject(def,nullptr){}
    void Tick() override {++visits;}
};
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
    vm.Entities.clear();
    printf("original scheduler checks=%d failures=0\n",checks);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
