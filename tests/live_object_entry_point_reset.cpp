#include "odh/live_object_entry_point.hpp"
#include "odh/object_reset.hpp"
#include <iostream>
namespace {
using namespace odh;
unsigned checks=0;
void require(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
RoutineKey key(unsigned id){return routine_key(static_cast<std::uint16_t>(id));}
Bhav routine(std::initializer_list<Instruction> code){return {0x8002,code,{},Bhav::Metadata{0,4,4,0}};}
void active_creation_entry_matrix(){
 for(const std::uint16_t opcode:{std::uint16_t{42},std::uint16_t{9}})for(const auto entry:{6U,9U})for(const bool authored_true:{false,true})for(unsigned mode=0;mode<4;++mode){
  World world;require(world.create(1,EntityKind::avatar,{0,80})&&world.declare_object_word(1,8,0),"active object EP parent avatar");
  std::vector<EntryPoint> entries(10);entries[1].action=4100;entries[entry].action=4101;
  require(world.create_runtime_object(2,0x12345678,{},entries),"actual created object definition and entry table");
  const WorldEntityReference caller{1,world.find(1)->generation},created{2,world.find(2)->generation};
  const auto globals=std::make_shared<const VmProgram>(RoutineSet{});
  const auto bank=std::make_shared<const VmProgram>(RoutineSet{
    {key(4096),routine({{opcode,1,255,{0x78,0x56,0x34,0x12,0,0,0,0}},{1,254,255,{}}})},
    {key(4100),routine({{1,254,255,{}}})},{key(4101),routine({{99,254,255,{}}})}});
  std::unique_ptr<Vm> source,object_thread;unsigned effects=0,resets=0;LiveObjectEntryPointResult last;
  std::optional<LiveObjectEntryPointRequest> replay;VmFrame copied{};
  require(world.set_avatar_burning_observer([&](World&,const VmFrame& frame,WorldEntityReference victim,std::int16_t,std::int16_t){
    const auto lease=Vm::active_instruction_lease(frame);require(lease&&victim==caller,"EP source setter owns actual child lease and exact parent");
    Vm cleanup(globals,ShareVmProgramTag{},world.vm_memory(),{}, {},SharedRoutineOwners{{11,bank}},ShareVmOwnerProgramsTag{});
    EntryPointTable reset_entries{};reset_entries.entries.resize(4);reset_entries.entries[1].action=4100;
    const auto reset=run_object_reset(*lease,cleanup,*source,1,11,reset_entries);
    ++resets;return reset.completed&&reset.main_started;
  }),"active object EP Reset observer bound");
  source=std::make_unique<Vm>(globals,ShareVmProgramTag{},world.vm_memory(),VmLimits{},
    [&](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome{
      const auto lease=source->instruction_lease(frame);require(lease.has_value(),"actual constructor instruction lease");
      object_thread=std::make_unique<Vm>(globals,ShareVmProgramTag{},world.vm_memory(),VmLimits{},VmPrimitive{},SharedRoutineOwners{{22,bank}},ShareVmOwnerProgramsTag{});
      auto constructor=object_thread->authorize_instruction_child(*lease,VmInstructionChildKind::construction);
      require(constructor&&object_thread->start(key(4100),{2,2,0,22})==VmStatus::running,"object own Main prepared under actual constructor lineage");
      const auto current=[&,lease=*lease]{return lease.valid()&&world.is_live_reference(caller)&&world.is_live_reference(created);};
      LiveObjectEntryPointRequest request;request.world=&world;request.object=2;request.object_generation=created.generation;
      request.entry_point=static_cast<std::uint16_t>(entry);request.action=4101;request.code_owner=22;
      request.global_program=globals;request.owner_program=bank;request.register_owner=object_thread.get();request.live_memory=world.vm_memory();
      request.creation_source={source.get(),source->run_generation(),2,created.generation,current};
      request.creation_source.active_dispatch=LiveObjectCreationActiveDispatch{frame,instruction,dispatch,*lease,&frame};
      if(mode==2){copied=frame;request.creation_source.active_dispatch->protected_frame=&copied;}
      const auto* registered=object_thread.get();request.identities_still_match=[&,registered]{return world.is_live_reference(created)&&object_thread.get()==registered;};
      request.check_primitive=[&](const VmFrame& check_frame,const Instruction&,const Ts1Temps&,const VmDispatch& check_dispatch)->PrimitiveOutcome{
        ++effects;const auto actual=Vm::active_instruction_lease(check_frame);
        require(actual&&actual->origin()==check_dispatch.origin&&check_dispatch.origin!=source.get(),"real EP6/9 fresh executor inherits parent lifetime");
        const_cast<Vm*>(check_dispatch.origin)->temps()[3]=55;
        if(mode==1)require(world.set_entity_flags(check_frame,caller,512),"source EP6/9 flags can Reset the active constructor parent");
        if(mode==3)require(object_thread->start(key(4100),{2,2,0,22})==VmStatus::running,"unrelated register-owner restart changes strict run");
        return {authored_true?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
      };
      last=evaluate_live_object_entry_point(request);replay=request;
      if(mode>=2)require(last.state==LiveObjectEntryPointState::unsupported,"copied actual-frame pointer and unrelated object run rejected");
      else require(last.state==(authored_true?LiveObjectEntryPointState::evaluated_true:LiveObjectEntryPointState::evaluated_false),
        "EP false/true remains completed source callback after genuine parent Reset");
      return {last.state==LiveObjectEntryPointState::unsupported?PrimitiveDisposition::unsupported:PrimitiveDisposition::goto_true,last.diagnostic};
    },SharedRoutineOwners{{11,bank}},ShareVmOwnerProgramsTag{});
  require(source->start(key(4096),{1,1,0,11})==VmStatus::running,"active object EP parent starts");
  const auto status=source->run(1);
  if(mode>=2)require(status==VmStatus::fault&&resets==0&&effects==(mode==2?0U:1U),"invalid provenance fails without invented Reset");
  else require(status==VmStatus::budget_exhausted&&effects==1&&resets==(mode==1?1U:0U)&&
    source->stack().size()==1&&source->stack()[0].key.id==(mode==1?4100:4096)&&
    source->stack()[0].pc==(mode==1?0:1)&&object_thread->temps()[3]==55,
    "ordinary branch updates retained old frame while new parent Main and object TEMP remain exact");
  require(replay&&evaluate_live_object_entry_point(*replay).state==LiveObjectEntryPointState::unsupported&&effects==(mode==2?0U:1U),
    "expired constructor/EP proof cannot replay effects after original instruction unwinds");
 }
}
}
int main(){try{active_creation_entry_matrix();std::cout<<checks<<" active object entry-point Reset checks passed\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
