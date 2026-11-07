// SPDX-License-Identifier: MPL-2.0
#include "odh/autonomy_check.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class T> odh::AutonomyResult<T> ready(T value){return {odh::AutonomyStatus::ready,std::move(value),{}};}
odh::RoutineKey key(std::uint16_t id){return odh::routine_key(id);}
odh::Bhav tree(std::initializer_list<odh::Instruction> instructions){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
void source_error_check_matrix(){
 for(unsigned mode=0;mode<5;++mode){
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,88,3,16});world.create(2,odh::EntityKind::object);
  world.set_person_data(1,58,25);world.declare_object_word(1,8,0);
  const auto global=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4096),tree({{3,254,255,{}}})}});
  const auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4100),tree({{99,254,255,{}}})},{key(4101),tree({{98,254,255,{}}})}});
  odh::Interaction row{};row.action=4100;row.test=4101;unsigned checks=0;
  odh::AutonomyResult<odh::AutonomyCheck> result;odh::Vm* actual=nullptr;
  const odh::VmPrimitive checks_primitive=[&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
   ++checks;if(mode==4)throw std::runtime_error("authored absent object");
   return {mode==0?odh::PrimitiveDisposition::source_error:mode==1?odh::PrimitiveDisposition::unsupported:
      mode==2?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::continue_execution,"authored absent object"};
  };
  odh::Vm vm(global,odh::ShareVmProgramTag{},world.vm_memory(),{},
   [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
    result=odh::evaluate_saved_autonomy_check(world,*actual,1,2,22,row,global,bank,world.vm_memory(),checks_primitive,{},16);
    return {odh::PrimitiveDisposition::goto_true,{}};
   });actual=&vm;
  vm.start(key(4096),{1,1,1});require(vm.run(1)==odh::VmStatus::returned_true&&checks>0,"autonomy ERROR uses actual disposable Check and restores caller");
  if(mode==0)require(result.status==odh::AutonomyStatus::ready&&!result.value.allowed,"known source ERROR makes autonomous choice ineligible");
  else require(result.status!=odh::AutonomyStatus::ready,"autonomy does not convert unsupported/wait/budget/host exception into denied choice");
 }
}
void check_advertisement_ownership(){
    for(const bool explicit_variant:{false,true}){
        odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&world.create(2,odh::EntityKind::object)&&
            world.set_person_data(1,58,25)&&world.declare_object_word(1,8,0),"autonomy advertisement source identities");
        const auto motives=world.find(1)->motives;
        const auto global=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4096),tree({
            {2,1,255,{7,0,55,0,0,5,12,7}},{3,254,255,{}}})}});
        const auto resource=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {key(4100),tree({{99,254,255,{}}})},
            {key(4101),tree({{2,1,255,{7,0,88,0,0,5,12,7}},
                {2,2,255,{7,0,9,0,0,5,28,7}},{2,3,255,{7,0,253,255,0,5,29,7}},
                {static_cast<std::uint16_t>(explicit_variant?50:99),4,255,{}},
                {2,254,255,{6,0,7,0,0,5,8,12}}})}});
        odh::Interaction row{};row.action=4100;row.test=4101;
        odh::AutonomyResult<odh::AutonomyCheck> result;odh::Vm* actual=nullptr;
        odh::Vm vm(global,odh::ShareVmProgramTag{},world.vm_memory(),{},
            [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                result=odh::evaluate_saved_autonomy_check(world,*actual,1,2,22,row,global,resource,world.vm_memory(),{},
                    [](const auto&,const auto&,const auto&){return ready(std::optional<std::string>{"variant"});},100,
                    [](const odh::VmMemory& memory){return odh::VmPrimitive{[memory](const auto& frame,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
                        require(memory.read(frame,12,7)==88&&memory.read(frame,28,7)==9&&memory.read(frame,29,7)==-3&&
                            memory.read(frame,12,9)==0&&memory.write(frame,28,9,0),
                            "AutoCheck expressions and operand-scope consumers share its own dictionary and null action fallback");
                        return {odh::PrimitiveDisposition::goto_true,{}};
                    }};});
                return {odh::PrimitiveDisposition::goto_true,{}};
            });actual=&vm;
        require(vm.start(key(4096),{1,1,1})==odh::VmStatus::running&&vm.run(2)==odh::VmStatus::returned_true&&
            result.status==odh::AutonomyStatus::ready&&result.value.allowed&&result.value.variants.size()==1&&
            vm.temps()[6]==88&&vm.advertisement_changes()->at((1U<<16)|7)==55&&world.find(1)->motives==motives,
            "AutoCheck shares caller Temps while isolating advertisements from real Thread state and actual needs");
        const auto& variant=result.value.variants.front();
        if(explicit_variant)require(variant.name==std::optional<std::string>{"variant"}&&!variant.motive_ad_changes,
            "explicit action-string variants do not acquire the check dictionary");
        else require(variant.motive_ad_changes&&variant.motive_ad_changes->at((1U<<16)|7)==88&&
            variant.motive_ad_changes->at(7)==-3&&variant.motive_ad_changes->at((2U<<16)|9)==0,
            "only the implicit check variant receives exact local advertisement overrides including zero");
    }
}
// Exercises the joined native services, not a fake successful enqueue callback.
void check_choice_handover_effect(){
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&
        world.create(2,odh::EntityKind::object)&&world.set_position(1,{0,0,1,0})&&
        world.set_position(2,{32,0,1,0})&&world.set_person_data(1,58,25)&&
        world.declare_object_word(1,8,0)&&world.declare_object_word(2,8,0),"live household facts");
    auto table=std::make_shared<odh::Ttab>();table->status=odh::TtabStatus::decoded;table->declared_count=1;
    odh::Interaction row{};row.label_index=7;row.action=4098;row.test=4099;
    row.attenuation_code=1;row.joining_index=-1;row.motives.resize(16);row.motives[8].delta=1000;
    table->interactions.push_back(row);odh::validate_saved_autonomy_table(*table);
    const auto global=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {key(4096),tree({{3,1,255,{}},{30,254,255,{}}})}});
    // Check publishes a real shared temp; selected action changes hygiene.
    const auto resource=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {key(4098),tree({{2,1,255,{0,0,0,0,0,2,9,7}}, // Authored variant Param0 remains zero.
                       {2,254,255,{8,0,75,0,0,5,14,7}}})},
        {key(4099),tree({{2,1,255,{0,0,1,0,0,2,9,7}}, // Eligibility is autonomous-only.
                       {2,254,255,{7,0,19,0,0,5,8,7}}})}});
    odh::StringTable strings{odh::StringTableState::decoded,-3,{},0};
    for(unsigned i=0;i<9;++i)strings.entries.push_back({1,"(0;0);(100;100)",""});
    const auto curves=std::make_shared<const odh::Ts1InteractionCurves>(odh::read_ts1_interaction_curves(strings));
    odh::InteractionQueue queue(world,1,8);odh::Vm* caller=nullptr;
    unsigned random_calls=0,check_calls=0,enqueues=0;
    const odh::AutonomyIdentity target{2,world.find(2)->generation};
    const odh::VmPrimitive primitive=[&](const auto& frame,const auto& op,const auto& temps,const auto& dispatch){
        if(op.opcode!=3)return queue.primitive()(frame,op,temps,dispatch);
        odh::FindBestActionRequest request;
        request.world=&world;request.source_vm=caller;request.source_frame=&frame;
        request.source_instruction=&op;request.source_dispatch=dispatch;
        request.identities_still_match=[&]{return caller->matches_active_primitive(frame,op,dispatch);};
        request.first_higher_priority=[&](auto priority){std::optional<odh::InteractionActionSnapshot> found;
            require(queue.first_higher_priority(priority,found),"real queue priority lookup");
            return ready(found?std::optional<odh::AutonomyIdentity>{{found->call.context.callee,found->target_generation}}:
                std::optional<odh::AutonomyIdentity>{});};
        request.ordered_with_autonomy=[&]{return ready(std::vector<odh::AutonomyIdentity>{target});};
        request.resolve_object=[&](auto identity){require(identity.id==2&&identity.generation==target.generation,"resource target identity");
            return ready(odh::AutonomyObject{target,table,true,0,true});};
        request.check_action=[&](auto identity,const auto& selected){++check_calls;
            return odh::evaluate_saved_autonomy_check(world,*caller,1,identity.id,22,selected,
                global,resource,world.vm_memory(),{},{});};
        request.use_count=[&](auto){std::vector<odh::InteractionActionSnapshot> active;
            require(queue.active_actions(active),"real active queue inspection");return ready(static_cast<std::int16_t>(active.size()));};
        request.enqueue=[&](const auto& choice){++enqueues;
            require(choice.callee.id==2&&choice.id==7&&choice.param0==0&&
                odh::saved_autonomy_action_available(row,*global,*resource),"selected source GetAction and arguments");
            return ready(queue.enqueue_source_autonomous(key(row.action),{1,2,2,22},
                {choice.param0,0,0,0},row.flags,key(row.test),choice.id));};
        request.next_random=[&](auto range){require(range==10000,"shared weighted random range");++random_calls;return ready(std::uint64_t{0});};
        request.interaction_curves=[&](bool child){require(!child,"adult curves");return ready(curves);};
        return odh::find_best_action(request);
    };
    odh::Vm vm(global,odh::ShareVmProgramTag{},world.vm_memory(),{},primitive,
        odh::SharedRoutineOwners{{22,resource}},odh::ShareVmOwnerProgramsTag{});
    caller=&vm;
    require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0})==odh::VmStatus::running,"real bound resident VM");
    require(vm.run(1)==odh::VmStatus::budget_exhausted&&queue.size()==1&&check_calls==1&&
        random_calls==1&&enqueues==1&&vm.temps()[7]==19,"AutoCheck effects survive scoring and actual queue admission");
    const auto chosen=queue.snapshot();
    require(chosen.actions.front().priority==2&&chosen.actions.front().flags==row.flags&&
        chosen.actions.front().check_permissions&&chosen.actions.front().check_context==odh::InteractionCheckContext::autonomous&&
        chosen.actions.front().call.arguments==std::vector<std::int16_t>{0,0,0,0}&&world.find(1)->motives[8]==0,
        "admission preserves source metadata and does not execute the action");
    require(vm.run(1)==odh::VmStatus::budget_exhausted&&queue.active()&&vm.stack().size()==2&&
        vm.stack().back().context.code_owner==22&&world.find(1)->motives[8]==0,
        "source30 enters exact action resource without premature effect");
    require(vm.run(2)==odh::VmStatus::yielded&&world.find(1)->motives[8]==75&&queue.observe()&&
        queue.completion()&&queue.completion()->result,"selected action completes and changes live hygiene");
    require(queue.tick(1)&&vm.run(1)==odh::VmStatus::returned_false&&queue.size()==0&&
        world.find(1)->motives[8]==75,"source30 resumes its real no-action branch after completion");
}
}
int main(){try{source_error_check_matrix();check_advertisement_ownership();check_choice_handover_effect();std::cout<<"native autonomy check/choice/queue/execution passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
