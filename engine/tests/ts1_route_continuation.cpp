// SPDX-License-Identifier: MPL-2.0
// Synthetic route resume/projection contracts; original gameplay has separate gates.
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define private public
#include "simantics/engine/VMThread.h"
#undef private
#include "simantics/engine/TS1RouteContinuation.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/primitives/VMGotoRoutingSlot.h"
#include "simantics/primitives/VMGotoRelativePosition.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
static int checks=0;
static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why);++checks; }
template<class F> static void rejects(F f,const char* why) {
 bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}check(failed,why);
}
struct Fixture {
 VMContext Context{nullptr};VM Machine{&Context,nullptr};VMThread Thread{&Context,nullptr,8};
 VMRoutine Routine;std::shared_ptr<VMStackFrame> Owner=std::make_shared<VMStackFrame>();
 Fixture(unsigned opcode) {
  Machine.TS1=true;Thread.TS1RouteDefaultsKnown=true;
  Routine.Instructions.resize(3);Routine.Instructions[0].Opcode=opcode;
  Routine.Instructions[0].FalsePointer=2;Routine.Instructions[0].TruePointer=1;
  Owner->Thread=&Thread;Owner->Routine=&Routine;Owner->PrimitiveState=3;
  Thread.Stack={Owner};
 }
 VMPrimitiveExitCode execute() {
  if(Routine.Instructions[0].Opcode==27) {
   VMGotoRelativePosition primitive;VMGotoRelativePositionOperand operand;
   operand.Direction=static_cast<VMGotoRelativeDirection>(99); // fresh validation rejects
   return primitive.Execute(Owner.get(),&operand);
  }
  VMGotoRoutingSlot primitive;VMGotoRoutingSlotOperand operand;
  operand.Type=static_cast<Scopes::VMSlotScope>(99); // no content lookup for fresh path
  return primitive.Execute(Owner.get(),&operand);
 }
 std::shared_ptr<VMRoutingFrame> route() {
  auto route=std::make_shared<VMRoutingFrame>();route->Thread=&Thread;route->Routine=&Routine;
  Thread.Stack.push_back(route);return route;
 }
};
int main() {try {
 {VMContext context(nullptr);VM machine(&context,nullptr);machine.TS1=true;VMThread fresh(&context,nullptr,0);
  check(fresh.TS1RouteDefaultsKnown&&fresh.TS1RouteDefaultsCount==0,"fresh TS1 thread authenticates an empty vector");}
 for(unsigned opcode:{27u,45u}) {
  Fixture f(opcode);f.Thread.TS1RouteDefaultsCount=2;
  const auto result=f.execute();
  check(result==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Owner->PrimitiveState==0&&
   f.Owner->InstructionPointer==0&&f.Thread.TS1RouteDefaultsCount==1&&f.Thread.Stack.size()==1,
   "default top pops exactly one and clears state before descriptor validation");
  f.Thread.ContinueExecution=true;f.Thread.HandleResult(f.Owner.get(),&f.Routine.Instructions[0],result);
  check(!f.Thread.ContinueExecution&&f.Owner->InstructionPointer==0,"actual interpreter yields at the same PC");
  check(f.execute()==VMPrimitiveExitCode::GOTO_FALSE&&f.Thread.TS1RouteDefaultsCount==1,
   "following state-zero execution follows fresh operand validation without another default pop");
  f.Owner->PrimitiveState=3;f.Thread.TS1RouteDefaultsCount=0;
  auto noRecord=f.execute();check(noRecord==VMPrimitiveExitCode::GOTO_FALSE,"no-record continuation selects false");
  f.Thread.HandleResult(f.Owner.get(),&f.Routine.Instructions[0],noRecord);
  check(f.Owner->PrimitiveState==0&&f.Owner->InstructionPointer==2,"normal false branch clears full state");
  f.Owner->InstructionPointer=0;f.Owner->PrimitiveState=3;f.Thread.TS1RouteDefaultsCount=1;
  f.Thread.IsCheck=true;check(f.execute()==VMPrimitiveExitCode::GOTO_FALSE&&f.Thread.TS1RouteDefaultsCount==1,
   "check execution cannot consume defaults");f.Thread.IsCheck=false;
  VMPrimitiveExitCode ignored;
  f.Machine.TS1=false;check(!f.Thread.TryResumeTS1Route(f.Owner.get(),ignored),"TSO bypasses continuation helper");f.Machine.TS1=true;
  f.Thread.TS1RouteDefaultsKnown=false;check(!f.Thread.TryResumeTS1Route(f.Owner.get(),ignored),"old native unknown provenance bypasses helper");
  f.Thread.TS1RouteDefaultsCount=0;f.Thread.TS1RouteDefaultsKnown=true;f.route();
  check(!f.Thread.TryResumeTS1Route(f.Owner.get(),ignored),"suspended parent cannot consume native child as placeholder");
 }
 Fixture f(45);f.Owner->PrimitiveState=0;
 auto route=f.route();auto plan=BuildTS1RouteSavePlan(f.Thread,true);
 check(plan.LeafProjected&&plan.DefaultsKnown&&plan.DefaultsCount==1&&plan.ClassicRecordCount==1&&
  plan.NativeFrames.size()==1&&plan.ClassicFrames.size()==1&&plan.PrimitiveState(f.Owner.get())==1,
  "eligible leaf projects an explicit default and nonzero authored resume state");
 check(f.Thread.Stack.size()==2&&f.Owner->PrimitiveState==0&&f.Thread.TS1RouteDefaultsCount==0,
  "projection never mutates live route or owner");
 f.Owner->PrimitiveState=9;check(plan.PrimitiveState(f.Owner.get())==1,"plan state overlay remains independent of live state");f.Owner->PrimitiveState=0;
 check(plan.NativeDepth(1)==1&&plan.ClassicDepth(1)==1,"shared depth mapping preserves authored ownership");
 rejects([&]{plan.NativeDepth(2);},"removed descriptor cannot own a projected use");
 route->TS1MotionToken=73;
 auto nativeMotion=BuildTS1RouteSavePlan(f.Thread,true,true);
 check(!nativeMotion.LeafProjected&&nativeMotion.NativeFrames.size()==2&&
  nativeMotion.DefaultsCount==0&&nativeMotion.PrimitiveState(f.Owner.get())==0,
  "owned motion retains the native frame and unchanged primitive state");
 check(BuildTS1RouteSavePlan(f.Thread,true).LeafProjected,
  "native model ownership does not change the separate classic projection");
 route->TS1MotionToken=0;
 f.Thread.TS1RouteDefaultsCount=2;plan=BuildTS1RouteSavePlan(f.Thread,true);
 check(plan.DefaultsCount==3&&plan.ClassicRecordCount==3,"default lower prefix survives native leaf conversion");
 auto child=std::make_shared<VMStackFrame>();child->Routine=&f.Routine;child->Thread=&f.Thread;
 f.Thread.Stack.push_back(child);f.Thread.TS1ObjectUses.push_back({nullptr,3,0});
 plan=BuildTS1RouteSavePlan(f.Thread,true);
 check(!plan.LeafProjected&&plan.NativeFrames.size()==3&&plan.ClassicFrames.size()==2&&
  plan.NativeDepth(3)==3&&plan.ClassicDepth(3)==2&&plan.ClassicRecordCount==3,
  "nested child stays native-exact with consistent native/classic use-depth mapping");
 child->PrimitiveState=3;VMPrimitiveExitCode ignored;
 check(!f.Thread.TryResumeTS1Route(child.get(),ignored)&&f.Thread.TS1RouteDefaultsCount==2,
  "default prefix cannot be consumed through a live native ancestor suffix");
 f.Thread.Stack.pop_back();f.Thread.TS1ObjectUses.clear();route->State=VMRoutingFrameState::FAILED;
 plan=BuildTS1RouteSavePlan(f.Thread,true);check(!plan.LeafProjected&&plan.NativeFrames.size()==2,
  "canonical target-null failed route remains native-exact by state rather than target test");
 route->State=VMRoutingFrameState::WALKING;route->HeldResult=int8_t(VMPrimitiveExitCode::RETURN_FALSE);
 check(!BuildTS1RouteSavePlan(f.Thread,true).LeafProjected,"held native result is not reinterpreted as default");route->HeldResult=-1;
 f.Thread.TS1ObjectUses.push_back({nullptr,2,0});check(!BuildTS1RouteSavePlan(f.Thread,true).LeafProjected,
  "use ending on removed route prevents projection");f.Thread.TS1ObjectUses.clear();
 f.route();check(!BuildTS1RouteSavePlan(f.Thread,true).LeafProjected,"multiple routes never silently strip nested records");
 f.Thread.TS1RouteDefaultsKnown=false;f.Thread.TS1RouteDefaultsCount=1;
 rejects([&]{BuildTS1RouteSavePlan(f.Thread,true);},"unknown default provenance cannot carry a nonzero count");
 f.Thread.HandleResult(f.Owner.get(),nullptr,VMPrimitiveExitCode::INTERRUPT);
 check(f.Thread.Stack.empty()&&f.Thread.TS1RouteDefaultsKnown&&f.Thread.TS1RouteDefaultsCount==0,
  "destructive interpreter interruption retires defaults and authenticates a fresh empty vector");
 std::printf("PASS %d TS1 route continuation contracts\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
