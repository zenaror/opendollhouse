#include "odh/live_entry_point5.hpp"
#include "odh/object_reset.hpp"
#include "odh/routing.hpp"
#include <iostream>
namespace {
using namespace odh;
unsigned checks=0;
void require(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
RoutineKey key(unsigned id){return routine_key(static_cast<std::uint16_t>(id));}
Bhav routine(std::initializer_list<Instruction> code){return {0x8002,code,{},Bhav::Metadata{0,4,4,0}};}
void typed_parked_reset_matrix(){
 for(unsigned mode=0;mode<4;++mode){
  World world;require(world.create(1,EntityKind::avatar,{0,80})&&world.create(2,EntityKind::avatar,{0,80}),
    "typed EP5 Reset exact World bodies");
  const auto global=std::make_shared<const VmProgram>(RoutineSet{});
  const auto bank=std::make_shared<const VmProgram>(RoutineSet{
    {key(4096),routine({{45,254,255,{}}})},{key(4100),routine({{1,254,255,{}}})},
    {key(4101),routine({{99,254,255,{}}})}});
  Vm source(global,ShareVmProgramTag{},world.vm_memory(),{},[](const auto&,const auto&,const auto&,const auto&){
    return PrimitiveOutcome{PrimitiveDisposition::wait,{}};},SharedRoutineOwners{{11,bank}},ShareVmOwnerProgramsTag{});
  Vm tested(global,ShareVmProgramTag{},world.vm_memory(),{}, {},SharedRoutineOwners{{22,bank}},ShareVmOwnerProgramsTag{});
  require(source.start(key(4096),{1,1,2,11})==VmStatus::running&&source.run(1)==VmStatus::waiting&&source.pending()&&
    tested.start(key(4100),{2,2,0,22})==VmStatus::running,"typed EP5 actual parked source and tested Thread");
  const auto source_run=source.run_generation(),tested_run=tested.run_generation(),ticket=source.pending()->ticket;
  const WorldEntityReference first{1,world.find(1)->generation},second{2,world.find(2)->generation};
  const auto source_current=[&]{return source.run_generation()==source_run&&source.status()==VmStatus::waiting&&
    source.pending()&&source.pending()->ticket==ticket;};
  const auto identities=[&]{return world.is_live_reference(first)&&world.is_live_reference(second);};
  LiveEntryPoint5Request request;request.world=&world;request.tested_entity=2;request.other_entity=1;
  request.tested_generation=second.generation;request.other_generation=first.generation;
  request.action=4101;request.code_owner=22;request.global_program=global;request.owner_program=bank;
  request.live_memory=world.vm_memory();request.register_owner=&tested;
  request.object_creation_source=LiveObjectCreationPlacementSource{&source,source_run,2,second.generation,source_current};
  request.identities_still_match=[&]{return identities()&&tested.run_generation()==tested_run;};
  request.identities_after_authenticated_reset=identities;
  request.check_primitive=[&](const auto& frame,const auto&,const auto&,const auto&)->PrimitiveOutcome{
    const auto lease=Vm::active_instruction_lease(frame);require(lease.has_value(),"typed parked service check owns real lifecycle lease");
    if(mode==0)return {PrimitiveDisposition::goto_true,{}};
    auto& victim=mode==2?tested:source;const auto id=mode==2?2:1;const auto owner=mode==2?22:11;
    Vm cleanup(global,ShareVmProgramTag{},world.vm_memory(),{}, {},SharedRoutineOwners{{static_cast<CodeOwner>(owner),bank}},ShareVmOwnerProgramsTag{});
    EntryPointTable entries{};entries.entries.resize(4);entries.entries[1].action=4100;
    const auto reset=run_object_reset(*lease,cleanup,victim,static_cast<std::int16_t>(id),static_cast<CodeOwner>(owner),entries);
    require(reset.completed&&reset.main_started,"typed EP5 Reset preserves exact selected victim Thread");
    if(mode==3)require(source.start(key(4100),{1,1,0,11})==VmStatus::running,"unrelated later start changes receipt run");
    return {PrimitiveDisposition::goto_true,{}};
  };
  const auto result=evaluate_live_entry_point5(request);
  if(mode==3)require(result.state==LiveEntryPoint5State::unsupported,"unrelated run cannot reuse authenticated Reset receipt");
  else require(result.state==LiveEntryPoint5State::evaluated_true&&result.source_reset==(mode==1)&&
    result.register_owner_reset==(mode==2),"EP5 distinguishes source Reset, other register-owner Reset and unchanged source");
  if(mode==1)require(!source.pending()&&source.stack().size()==1&&source.stack()[0].key.id==4100,
    "source Reset never completes the retired pending ticket");
  if(mode==2)require(source_current()&&tested.stack().size()==1&&tested.stack()[0].key.id==4100,
    "tested Thread Reset leaves different parked source intact");
 }
}
void routing_retirement_matrix(){
 for(const bool during_request:{false,true})for(const bool forged:{false,true}){
  World world;require(world.create(1,EntityKind::avatar,{0,80})&&world.create(2,EntityKind::object)&&
    world.set_position(1,{4,12,1,0})&&world.set_position(2,{28,12,1,0}),"route Reset matrix source bodies");
  RouteGrid grid{64,32};grid.block({28,12});SlotRecord slot{};slot.type=3;slot.standing=1;slot.flags=0x55;
  slot.snap_target_slot=-1;slot.proximity=std::array<std::int32_t,3>{16,16,16};slot.max_size=0;slot.unknown_i10=0;
  slot.gradient_bits=0x3e400000;slot.height=0;slot.facing=-2;slot.resolution=16;
  SlotTable slots{0,10,{slot},0};Routing route(world,grid,slots);
  const auto global=std::make_shared<const VmProgram>(RoutineSet{});
  const auto bank=std::make_shared<const VmProgram>(RoutineSet{
    {key(4096),routine({{45,254,255,{0,0,2,0,0,0,0,0}}})},
    {key(4100),routine({{1,254,255,{}}})},{key(4101),routine({{99,254,255,{}}})}});
  Vm source(global,ShareVmProgramTag{},world.vm_memory(),{},route.primitive(),SharedRoutineOwners{{11,bank}},ShareVmOwnerProgramsTag{});
  Vm tested(global,ShareVmProgramTag{},world.vm_memory(),{}, {},SharedRoutineOwners{{22,bank}},ShareVmOwnerProgramsTag{});
  require(route.bind(source)&&tested.start(key(4100),{2,2,0,22})==VmStatus::running,"route Reset matrix native Thread binding");
  const WorldEntityReference first{1,world.find(1)->generation},second{2,world.find(2)->generation};
  unsigned callbacks=0;bool caught=false;
  const auto phase=during_request?RouteExecutionPhase::attempt_walk:RouteExecutionPhase::set_position;
  route.set_route_execution_callback(phase,[&](const RouteExecutionContext& context){
    ++callbacks;
    if(forged)throw SourceCheckResetRetirement(&source);
    const auto run=source.run_generation(),register_run=tested.run_generation();
    LiveEntryPoint5Request request;request.world=&world;request.tested_entity=2;request.other_entity=1;
    request.tested_generation=second.generation;request.other_generation=first.generation;
    request.action=4101;request.ghost=during_request?1:0;request.code_owner=22;
    request.source_vm=&source;request.source_frame=&context.frame;request.source_instruction=&context.instruction;
    request.source_dispatch=context.dispatch;request.route_execution_proof=context.proof;request.route_execution_phase=phase;
    request.global_program=global;request.owner_program=bank;request.live_memory=world.vm_memory();request.register_owner=&tested;
    const auto identities=[&]{return world.is_live_reference(first)&&world.is_live_reference(second);};
    request.identities_still_match=[&]{return identities()&&source.run_generation()==run&&tested.run_generation()==register_run;};
    request.identities_after_authenticated_reset=identities;
    request.check_primitive=[&](const VmFrame& frame,const auto&,const auto&,const auto&)->PrimitiveOutcome{
      const auto lease=Vm::active_instruction_lease(frame);require(lease.has_value(),"route EP5 has actual source-check lease");
      Vm cleanup(global,ShareVmProgramTag{},world.vm_memory(),{}, {},SharedRoutineOwners{{11,bank}},ShareVmOwnerProgramsTag{});
      EntryPointTable entries{};entries.entries.resize(4);entries.entries[1].action=4100;
      const auto reset=run_object_reset(*lease,cleanup,source,1,11,entries,30,{},
        ObjectResetHooks{[&]{return route.discard_for_source_reset();},{}});
      require(reset.completed&&reset.main_started&&!route.active(),"Reset retires logical route while retaining in-flight native storage");
      return {PrimitiveDisposition::goto_true,{}};
    };
    const auto result=evaluate_live_entry_point5(request);
    require(result.state==LiveEntryPoint5State::evaluated_true&&result.source_reset&&!result.register_owner_reset,
      "route EP5 reports authenticated source retirement after actual Reset");
    throw SourceCheckResetRetirement(&source);
  });
  require(source.start(key(4096),{1,1,2,11})==VmStatus::running,"start route Reset matrix source");
  try{
    const auto status=source.run(during_request?1:10);
    if(during_request){
      // A foreign signal must propagate as the source exception. A proved
      // retired routing frame instead leaves its new Main runnable.
      if(forged)require(false,"unproved request retirement must propagate rather than be consumed");
      else require(status==VmStatus::budget_exhausted&&!source.pending()&&!route.has_route_record()&&
        source.stack().size()==1&&source.stack()[0].key.id==4100,"retired request returns CONTINUE without old branch/write/wait");
    }else{
      require(status==VmStatus::waiting&&route.active(),"route Reset matrix reaches actual pending frame before tick");
      const auto consumed=route.tick(1);
      require(!forged&&consumed&&!route.has_route_record()&&!source.pending()&&
        source.status()==VmStatus::running&&source.stack().size()==1&&source.stack()[0].key.id==4100,
        "retired tick consumes cancellation without completing old pending ticket or restoring old Job");
    }
  }catch(const SourceCheckResetRetirement&){caught=true;}
  require(callbacks==1&&*world.find(1)->position==EntityPosition{4,12,1,0},"retirement boundary executes once without publishing later movement");
  if(forged){
    require(caught,"forged request/tick signal propagates as a source failure");
    if(during_request)require(!route.has_route_record()&&!source.pending()&&source.stack().size()==1&&
      source.stack()[0].key.id==4096&&source.stack()[0].pc==0,
      "forged request signal neither installs a route nor replaces original Main");
    else require(route.active()&&source.pending(),"forged tick signal cannot cancel pending route");
  }else require(!caught,"authenticated routing reset unwind remains internal to source boundary");
 }
}

}
int main(){try{typed_parked_reset_matrix();routing_retirement_matrix();std::cout<<checks<<" typed EP5 Reset checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
