// SPDX-License-Identifier: MPL-2.0
// TS1 immutable CodeOwner lookup is independent of execution/queue authority.
#include "live_resource_owner_binding.hpp"
#include "saved_object_subroutine.hpp"
#include "odh/interaction.hpp"
#include "odh/object_reset.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace odh;
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
RoutineKey key(unsigned id){return routine_key(static_cast<std::uint16_t>(id));}
Bhav tree(std::initializer_list<Instruction> instructions){return {0x8002,instructions,{},Bhav::Metadata{0,4,4,0}};}
Instruction set(unsigned index,unsigned value){return {2,254,255,{static_cast<std::uint8_t>(index),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
std::shared_ptr<const VmProgram> bank(){return std::make_shared<const VmProgram>(RoutineSet{{key(4200),tree({set(0,77)})},{key(4201),tree({set(3,88)})}});}
void admission_matrix(){
 // 0 ordinary foreign callback;1 linked fresh Check;2 copied frame;3 wrong
 // dispatch;4 unregistered body;5 missing resolver;6 immutable conflict;
 // 7 owner budget;8 content-equivalent separately allocated registered bank;
 // 9 unlinked Check root;10 recycled recipient generation;11 empty bank.
 // All receivers are executing throughout lookup.
 for(unsigned mode=0;mode<12;++mode){
  World bodies;require(bodies.create(1,EntityKind::avatar),"registry recipient initial generation");
  const WorldEntityReference recipient{1,bodies.find(1)->generation};
  const auto expected=bank();unsigned loads=0;bool accepted=false;std::optional<VmInstructionLease> expired;VmFrame old_frame{};Instruction old_instruction{};VmDispatch old_dispatch{};
  Vm* receiver=nullptr;Vm* source=nullptr;
  const auto source_code=RoutineSet{{key(4096),tree({{99,254,255,{}}})}};
  const VmPrimitive source_handler=[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch)->PrimitiveOutcome{
   require(receiver->dispatch_active(),"foreign receiver remains in its real instruction");
   require(!receiver->install_shared_owner(77,expected),"public idle installation still refuses executing receiver");
   expired=source->instruction_lease(frame);old_frame=frame;old_instruction=instruction;old_dispatch=dispatch;
   if(mode==10)require(bodies.remove(1)&&bodies.create(1,EntityKind::avatar),"same-ID receiver replacement has a new generation");
   auto copied=frame;auto wrong=dispatch;++wrong.ticket;std::string why;
   accepted=odh_host::bind_instruction_resource_owner(*receiver,77,mode==11?std::make_shared<const VmProgram>(RoutineSet{}):mode==8?bank():expected,[&]{return mode!=4&&bodies.is_live_reference(recipient);},
       mode==2?copied:frame,instruction,mode==3?wrong:dispatch,&why);
   require(accepted==(mode==0||mode==1||mode==8),"only exact registered source boundaries admit immutable bank");
   if(!accepted)require(!why.empty(),"rejection retains a diagnostic");
   return {PrimitiveDisposition::goto_true,{}};
  };
  Vm source_vm(source_code,{}, {},source_handler);source=&source_vm;
  VmLimits limits;if(mode==7)limits.code_owners=1;
  Vm receiver_vm(source_code,{},limits,[&](const auto& frame,const auto&,const auto&,const auto&)->PrimitiveOutcome{
   const auto run=receiver->run_generation();const auto depth=receiver->stack().size();const auto pc=frame.pc;
   const auto temps=receiver->temps();const auto xl=receiver->temp_xl();
   auto link=mode==1?source->authorize_instruction_child(*receiver->instruction_lease(frame),VmInstructionChildKind::source_check):nullptr;
   require(mode!=1||bool(link),"fresh source Check linked to actual parent instruction");
   require(source->start(key(4096),{2,2,1})==VmStatus::running,"source callback starts");
   if(mode==1||mode==9)require(source->run_synchronous_check(8,source_handler).status==VmStatus::returned_true,"linked and query-root Checks return synchronously");
   else require(source->run(1)==VmStatus::returned_true,"ordinary foreign source returns");
   require(receiver->run_generation()==run&&receiver->stack().size()==depth&&frame.pc==pc&&receiver->temps()==temps&&receiver->temp_xl()==xl&&!receiver->pending(),
       "bank lookup alone changes no receiver execution, registers or wait");
   return {PrimitiveDisposition::goto_true,{}};
  });receiver=&receiver_vm;
  if(mode==6)require(receiver->install_shared_owner(77,std::make_shared<const VmProgram>(RoutineSet{{key(4200),tree({set(0,12)})}})),"conflicting immutable owner seeded at safe point");
  require(receiver->set_owner_resolver([&](CodeOwner owner){++loads;require(owner==77,"resolver sees exact nonaliased owner");return mode==5?std::shared_ptr<const VmProgram>{}:expected;}),"register receiver-owned resolver");
  require(receiver->start(key(4096),{1,1,1})==VmStatus::running&&receiver->run(1)==VmStatus::returned_true,"receiver unwinds normally");
  const bool admitted=mode==0||mode==1||mode==8;
  require(loads==((admitted||mode==5)?1U:0U),"invalid authority/content conflict cannot perform extra lazy lookup");
  if(expired)require(!receiver->prepare_instruction_owner(*expired,old_frame,old_instruction,old_dispatch,78),"expired instruction proof cannot load another bank");
  require(!receiver->shared_program_for_owner(78),"no postcallback cache authority");
 }
}
void push_active_and_cold(){
 for(const bool check_source:{false,true}){
  World world;require(world.create(1,EntityKind::avatar,{0,88,0,16})&&world.create(2,EntityKind::object)&&world.declare_object_word(1,8,16),"real recipient and source object");
  InteractionQueue queue(world,1);Vm* receiver=nullptr;Vm* source=nullptr;unsigned loads=0;const auto selected=bank();
  const Instruction idle{17,2,255,{1,0,1,0,0,0,0,0}};
  const auto main=RoutineSet{{key(4096),tree({{99,1,255,{}},idle,set(2,99)})}};
  const auto push=ts1_push_interaction_primitive(world,Ts1PushInteractionContextLookup{[&](const auto& request,const auto& frame,const auto& instruction,const auto& dispatch)->std::optional<Ts1PushInteractionAction>{
   require(receiver->dispatch_active()&&request.actor==1&&request.source==2&&dispatch.origin==source,"Push lookup separates current executor from active recipient Thread");
   const WorldEntityReference actor{1,world.find(1)->generation},object{2,world.find(2)->generation};
   require(odh_host::bind_instruction_resource_owner(*receiver,77,selected,[&]{return world.is_live_reference(actor)&&world.is_live_reference(object);},frame,instruction,dispatch),"Push13 resolves new recipient bank without idle-install bypass");
   return Ts1PushInteractionAction{key(4200),77,0,key(4201)};
  }},[&](auto id){return id==1?Ts1PushInteractionTarget{receiver,&queue}:Ts1PushInteractionTarget{};});
  Vm source_vm({{key(4100),tree({{13,254,255,{7,0,0,0,0,0,0,0}}})}},world.vm_memory(),{},push);source=&source_vm;
  Vm receiver_vm(main,world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->PrimitiveOutcome{
   if(instruction.opcode!=99)return queue.primitive()(frame,instruction,temps,dispatch);
   auto link=check_source?source->authorize_instruction_child(*receiver->instruction_lease(frame),VmInstructionChildKind::source_check):nullptr;
   require(!check_source||bool(link),"Push fresh Check inherits only exact parent lifecycle");
   require(source->start(key(4100),{1,1,1},{2})==VmStatus::running,"Push source starts with authored source object argument");
   require((check_source?source->run_synchronous_check(16,push).status:source->run(16))==VmStatus::returned_true,"ordinary/linked Check Push returns true");
   return {PrimitiveDisposition::goto_true,{}};
  });receiver=&receiver_vm;
  require(receiver->set_owner_resolver([&](auto owner){++loads;return owner==77?selected:nullptr;})&&queue.bind(*receiver),"bind recipient queue and immutable resolver");
  require(receiver->start(key(4096),{1,1,1},{0,0})==VmStatus::running,"recipient real Main starts");
  require(receiver->run(1)==VmStatus::budget_exhausted,"real active receiver returns to its original next PC");
  require(queue.size()==1&&loads==1&&receiver->temps()[0]==0&&receiver->temps()[3]==0&&receiver->stack().back().pc==1,"owner cache admission does not execute queued action/check");
  const auto world_bytes=encode_world_snapshot(world.snapshot()),vm_bytes=encode_vm_snapshot(receiver->snapshot());
  EntityThreadControllersSnapshot state{};state.interactions=queue.snapshot();const auto bytes=encode_entity_thread_controllers_snapshot(state);
  const auto decoded=decode_entity_thread_controllers_snapshot(bytes);
  require(decoded.interactions.actions.front().call.context.code_owner==77&&decoded.interactions.actions.front().check->id==4201&&encode_entity_thread_controllers_snapshot(decoded)==bytes,"queue codec preserves exact owner and action/check without runtime capability");
  World cold;require(cold.restore_snapshot(decode_world_snapshot(world_bytes)),"cold World restores without construction replay");InteractionQueue cold_queue(cold,1);
  Vm cold_vm(main,cold.vm_memory(),{},cold_queue.primitive(),{{77,selected->routines()}});
  require(cold_vm.restore_snapshot(decode_vm_snapshot(vm_bytes))&&cold_queue.bind(cold_vm)&&cold_queue.restore_snapshot(decoded.interactions),"cold queue binds verified owner resources and exact restored VM");
  Vm missing(main,cold.vm_memory());InteractionQueue missing_queue(cold,1);
  require(missing.restore_snapshot(decode_vm_snapshot(vm_bytes))&&missing_queue.bind(missing)&&!missing_queue.restore_snapshot(decoded.interactions),"cold queue rejects unavailable action/check bank rather than loading from serialized authority");
  require(cold_vm.run(20)==VmStatus::yielded&&cold_vm.temps()[0]==77&&cold_vm.temps()[3]==88&&cold_queue.observe()&&cold_queue.completion()->result,"restored queue executes real check and action exactly through normal IdleForInput");
  require(cold_queue.tick(1)&&cold_vm.run(20)==VmStatus::returned_true&&cold_vm.temps()[2]==99,"cold action completion resumes original Main without changing owner context");
 }
}
void external_queue_check(){
 World world;require(world.create(1,EntityKind::avatar,{0,88,0,16})&&world.create(2,EntityKind::object)&&world.declare_object_word(1,8,16),"external queue Check real actor and action source");
 InteractionQueue queue(world,1);const auto selected=bank();Vm* receiver=nullptr;unsigned loads=0,checks_run=0,action_effects=0;
 // Existing owner88 supplies an immediate action and its eligibility check.
 // That real PUBLIC queue operation creates the private typed source_check
 // bridge; the test never calls or exposes the VM's private entry point.
 const RoutineOwners trigger{{88,{{key(4100),tree({{98,254,255,{}}})},{key(4101),tree({{99,254,255,{}}})}}}};
 Vm vm({{key(4096),tree({{17,254,255,{0,0,1,0,0,0,0,0}}})}},world.vm_memory(),{},
     [&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->PrimitiveOutcome{
      if(instruction.opcode==98){++action_effects;return {PrimitiveDisposition::goto_true,{}};}
      return queue.primitive()(frame,instruction,temps,dispatch);
     },trigger);receiver=&vm;
 require(vm.set_owner_resolver([&](auto owner){++loads;return owner==77?selected:nullptr;})&&queue.bind(vm),"external queue binds immutable resolver and actual recipient");
 queue.set_check_primitive([&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch)->PrimitiveOutcome{
  ++checks_run;require(instruction.opcode==99&&dispatch.check_mode&&dispatch.origin!=receiver&&!receiver->dispatch_active()&&dispatch.origin->instruction_lease(frame).has_value(),
      "TryRunImmediately owns a real linked fresh Check while parent execution is parked");
  bool install_denied=false;try{install_denied=!receiver->install_shared_owner(77,selected);}catch(const std::logic_error&){install_denied=true;}
  require(install_denied,"external Check retains public parent mutation guard");
  const WorldEntityReference actor{1,world.find(1)->generation},object{2,world.find(2)->generation};
  require(odh_host::bind_instruction_resource_owner(*receiver,77,selected,[&]{return world.is_live_reference(actor)&&world.is_live_reference(object);},frame,instruction,dispatch),
      "actual external queue Check permits only bounded immutable cache admission on real recipient");
  // Ordinary CheckAction cannot reenter enqueue on its already executing
  // queue. Successful cache admission must not grant that separate authority.
  bool enqueue_denied=false;try{queue.enqueue(key(4200),{1,2,2,77});}catch(const std::logic_error&){enqueue_denied=true;}
  require(enqueue_denied,"immutable cache capability does not loosen queue reentry");
  return {PrimitiveDisposition::goto_false,{}};
 });
 require(vm.start(key(4096),{1,1,1},{0})==VmStatus::running&&queue.enqueue_ts1(key(4100),{1,2,2,88},{},1,InteractionQueueMode::normal,4,false,false,key(4101)),
     "source immediate candidate enters queue with its existing exact eligibility owner");
 const auto before=encode_vm_snapshot(vm.snapshot());
 require(queue.try_run_immediate()&&checks_run==1&&action_effects==0&&loads==1&&queue.size()==0&&!queue.active()&&vm.shared_program_for_owner(77),
     "public immediate queue consumer executes false Check once, retains new cache, and removes candidate without action execution");
 require(encode_vm_snapshot(vm.snapshot())==before,"external source Check preserves entire parent execution and register snapshot");
}

void foreign_named(){
 for(const bool result:{false,true}){
  World world;require(world.create(1,EntityKind::avatar)&&world.create(2,EntityKind::object),"named exact participants");
  const auto named=named_routine_key(123,false);const auto selected=std::make_shared<const VmProgram>(RoutineSet{{named,tree({{98,254,255,{}}})}});
  Vm* receiver=nullptr;Vm* source=nullptr;unsigned effects=0,loads=0;SavedObjectSubroutineReport report;
  Vm source_vm({{key(4100),tree({{28,254,255,{0,0,0,0,1,1,0,0}}})}},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
   require(receiver->dispatch_active()&&odh_host::bind_instruction_resource_owner(*receiver,77,selected,[]{return true;},frame,instruction,dispatch),"foreign named lookup admits exact bank into executing target");
   return run_saved_named_tree(*source,receiver,{frame,instruction,dispatch,1,77,named,1},temps,report,[&](const auto& inner,const auto&,const auto&,const auto&)->PrimitiveOutcome{
    ++effects;require(inner.context.code_owner==77&&inner.context.caller==1&&inner.context.callee==1&&inner.context.stack_object==1,"named execution retains target Thread caller and exact owner");
    return {result?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
   },8);
  });source=&source_vm;
  Vm target({{key(4096),tree({{99,254,255,{}}})}},world.vm_memory(),{},[&](const auto& frame,const auto&,const auto&,const auto&)->PrimitiveOutcome{
   const auto run=receiver->run_generation();const auto context=frame.context;
   require(source->start(key(4100),{2,2,1})==VmStatus::running&&source->run(8)==(result?VmStatus::returned_true:VmStatus::returned_false),"foreign named preserves authored true/false");
   require(receiver->run_generation()==run&&receiver->stack().size()==1&&frame.pc==0&&frame.context.code_owner==context.code_owner,"foreign named restores receiver's active parent frame");
   return {PrimitiveDisposition::goto_true,{}};
  });receiver=&target;
  require(target.set_owner_resolver([&](auto owner){++loads;return owner==77?selected:nullptr;})&&target.start(key(4096),{1,1,1})==VmStatus::running&&target.run(1)==VmStatus::returned_true&&effects==1&&loads==1,"named target executes selected new bank once without aliasing receiver owner0");
 }
}
void retained_reset_source(){
 World world;require(world.create(1,EntityKind::avatar),"Reset source entity");Vm* source=nullptr;const auto selected=bank();
 const RoutineOwners existing{{11,{{key(4096),tree({{99,254,255,{}}})},{key(4100),tree({{1,254,255,{}}})}}}};
 Vm vm({},world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch)->PrimitiveOutcome{
  const auto lease=source->instruction_lease(frame);require(bool(lease),"source actual callback lease before Reset");
  Vm cleanup({},world.vm_memory(),{}, {},existing);EntryPointTable entries;entries.entries.resize(4);entries.entries[1].action=4100;
  const auto reset=run_object_reset(*lease,cleanup,*source,1,11,entries);
  require(reset.completed&&reset.main_started&&lease->detached(),"source Reset retains original callback while new Main becomes execution");
  require(odh_host::bind_instruction_resource_owner(*source,77,selected,[]{return true;},frame,instruction,dispatch),"retained exact old instruction can finish immutable owner lookup after Reset");
  require(source->stack().size()==1&&source->stack().back().key.id==4100&&source->stack().back().pc==0,"admission neither advances nor replaces new Main");
  return {PrimitiveDisposition::goto_true,{}};
 },existing);source=&vm;
 require(vm.set_owner_resolver([&](auto owner){return owner==77?selected:nullptr;})&&vm.start(key(4096),{1,1,1,11})==VmStatus::running&&vm.run(1)==VmStatus::returned_true,"detached old TRUE follows actual source new-stack pop semantics");
}
}
int main(){try{admission_matrix();push_active_and_cold();external_queue_check();foreign_named();retained_reset_source();std::cout<<checks<<" immutable owner checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
