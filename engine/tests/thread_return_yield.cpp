// SPDX-License-Identifier: MPL-2.0
// Exercise real VMThread return edges without adding production diagnostic APIs.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
// Test-only access to route fields, VM identity mapping and the single-instruction
// consumer. Public VMThread::Tick also runs queue scheduling and subsequent BHAV
// instructions, so it cannot isolate this return edge. No production API changes.
#define private public
#include "simantics/VM.h"
#include "simantics/engine/VMThread.h"
#undef private
#include "simantics/VMContext.h"
#include <iostream>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using Flags = FSO::Files::Formats::IFF::Chunks::TTABFlags;
static int failures=0, cases=0;
static void verify(bool ok,const std::string& name){++cases;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";if(!ok)++failures;}
enum class Kind { Ordinary, Interaction, Immediate, Retry };
int main(){
 VMContext context(nullptr); VM vm(&context,nullptr);
 auto run=[&](Kind kind,unsigned edge,bool nextTick,bool recursive,bool falseBranch=false,bool original=false,int primitiveState=0){
  vm.TS1=original;
  VMThread thread(&context,nullptr,16);
  VMRoutine mainRoutine;mainRoutine.Instructions.resize(3);mainRoutine.Instructions[0].TruePointer=1;mainRoutine.Instructions[0].FalsePointer=2;
  VMRoutine middleRoutine;middleRoutine.Instructions.resize(1);middleRoutine.Instructions[0].TruePointer=254;middleRoutine.Instructions[0].FalsePointer=255;
  VMRoutine leafRoutine;leafRoutine.Instructions.resize(1);leafRoutine.Instructions[0].TruePointer=edge;leafRoutine.Instructions[0].FalsePointer=edge;
  auto main=std::make_shared<VMStackFrame>();main->Routine=&mainRoutine;main->Thread=&thread;
  main->PrimitiveState=primitiveState;
  mainRoutine.Instructions[0].Opcode=original?(primitiveState?17:30):0;
  auto middle=std::make_shared<VMStackFrame>();middle->Routine=&middleRoutine;middle->Thread=&thread;
  auto leaf=std::make_shared<VMStackFrame>();leaf->Routine=&leafRoutine;leaf->Thread=&thread;
  auto special=(kind==Kind::Interaction||kind==Kind::Immediate)?VMSpecialResult::Interaction:kind==Kind::Retry?VMSpecialResult::Retry:VMSpecialResult::Normal;
  (recursive?middle:leaf)->SpecialResult=special;
  auto pending=std::make_shared<VMQueuedAction>();pending->UID=99;
  const bool interaction=kind==Kind::Interaction||kind==Kind::Immediate;
  if(interaction){auto active=std::make_shared<VMQueuedAction>();active->UID=42;active->Mode=VMQueueMode::ParentIdle;active->Flags=kind==Kind::Immediate?Flags::RunImmediately:Flags(0);thread.Queue.push_back(active);thread.ActiveQueueBlock=0;}
  thread.Queue.push_back(pending);thread.Stack={main};if(recursive)thread.Stack.push_back(middle);thread.Stack.push_back(leaf);thread.ContinueExecution=true;
  const auto code=nextTick?(falseBranch?VMPrimitiveExitCode::GOTO_FALSE_NEXT_TICK:VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK):(falseBranch?VMPrimitiveExitCode::GOTO_FALSE:VMPrimitiveExitCode::GOTO_TRUE);
  thread.HandleResult(leaf.get(),&leafRoutine.Instructions[0],code);
  const bool originalResult=original&&kind==Kind::Interaction&&primitiveState==0;
  const bool expected=!nextTick&&(kind!=Kind::Interaction||originalResult);
  const bool branch=kind==Kind::Ordinary||originalResult;
  const auto pc=branch?(edge==254?1:2):0;
  const auto state=original&&branch?0:primitiveState;
  return thread.Stack.size()==1&&thread.Stack.back()==main&&thread.ContinueExecution==expected&&
      main->InstructionPointer==pc&&main->PrimitiveState==state&&thread.Queue.size()==1&&
      thread.Queue[0]==pending&&thread.ActiveQueueBlock==-1;
 };
 for(auto kind:{Kind::Ordinary,Kind::Interaction,Kind::Immediate,Kind::Retry})for(unsigned edge:{254u,255u})for(bool nextTick:{false,true})for(bool falseBranch:{false,true}){
  verify(run(kind,edge,nextTick,false,falseBranch),std::string(nextTick?"TSO NEXT_TICK ":"TSO immediate branch ")+std::to_string(edge)+(falseBranch?" false":" true")+" kind"+std::to_string(int(kind))+" preserves unwind/final policy and pending queue");
 }
 for(bool nextTick:{false,true})verify(run(Kind::Interaction,254,nextTick,true),std::string("TSO recursive interaction unwind ")+(nextTick?"NEXT_TICK":"normal"));
 // Original TS1 state0 Gosub parents consume the child's result; state1
 // Idle parents retry. Keep the complete old TSO matrix above as a separate
 // regression gate, including immediate invocations and explicit tick yields.
 for(auto kind:{Kind::Interaction,Kind::Immediate})for(int state:{0,1})for(unsigned edge:{254u,255u})for(bool nextTick:{false,true})for(bool recursive:{false,true}){
  verify(run(kind,edge,nextTick,recursive,false,true,state),"TS1 state"+std::to_string(state)+" kind"+std::to_string(int(kind))+" edge"+std::to_string(edge)+(nextTick?" next-tick":" immediate")+(recursive?" recursive":" direct")+" preserves result/yield and pending queue");
 }
 vm.TS1=true;
 {VMThread thread(&context,nullptr,16);VMRoutine routine;routine.Instructions.resize(1);auto frame=std::make_shared<VMStackFrame>();frame->Routine=&routine;frame->Thread=&thread;thread.Stack={frame};thread.MoveToInstruction(frame.get(),254,true);verify(thread.Stack.empty()&&!thread.ContinueExecution,"root return stops after empty stack");}
 {VMThread thread(&context,nullptr,16);VMRoutine routine;routine.Instructions.resize(2);auto frame=std::make_shared<VMStackFrame>();frame->Routine=&routine;frame->Thread=&thread;thread.Stack={frame};thread.MoveToInstruction(frame.get(),1,false);verify(frame->InstructionPointer==1&&!thread.ContinueExecution,"next tick default branch yields");thread.ThreadBreak=VMThreadBreakMode::Pause;thread.MoveToInstruction(frame.get(),1,true);verify(!thread.ContinueExecution,"debug pause remains paused");}
 std::cout<<cases<<" cases; "<<failures<<" failures\n";return failures?3:0;
}
