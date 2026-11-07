#include "functional_selector_host.hpp"
#include <iostream>
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
odh::RoutineKey key(std::uint16_t id){return odh::routine_key(id);}
odh::Bhav routine(std::uint16_t opcode){return {0x8002,{{opcode,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}};}
}
int main(){try{
    odh::World world;
    require(world.create(1,odh::EntityKind::avatar,{0,88,0,16})&&world.create(2,odh::EntityKind::object)&&
        world.create(3,odh::EntityKind::object)&&world.declare_object_word(2,8,0)&&world.declare_object_word(3,8,0),
        "selector host owns actual live actor and candidate banks");
    const auto reference=[&](std::int16_t id){return odh::WorldEntityReference{id,world.find(id)->generation};};
    auto target=reference(2);const auto actor=reference(1);
    bool group_two=false;
    const odh_host::FunctionalSelectorGroup group=[&](auto candidate)->std::optional<std::vector<odh::WorldEntityReference>>{
        if(candidate.id!=2||candidate.generation!=target.generation)return {};
        auto result=std::vector<odh::WorldEntityReference>{target};if(group_two)result.push_back(reference(3));return result;
    };
    odh::Vm vm({{key(4096),routine(99)},{key(4097),routine(98)}},world.vm_memory());
    const odh_host::FunctionalSelectorThread thread=[&](auto owner)->const odh::Vm*{
        return owner.id==actor.id&&owner.generation==actor.generation?&vm:nullptr;
    };
    require(vm.start(key(4096),{1,1,1,0})==odh::VmStatus::running&&
        odh_host::functional_selector_in_use(world,target,group,thread)==false,"complete empty-use scan returns false");
    group_two=true;require(world.declare_object_word(2,8,32)&&
        odh_host::functional_selector_in_use(world,target,group,{})==true,
        "source first part's occupied flag returns before consulting any Thread provider");
    require(world.declare_object_word(2,8,0)&&world.declare_object_word(3,8,32)&&
        odh_host::functional_selector_in_use(world,target,group,thread)==true,
        "source later part's InUse flag is checked after the first part's real Thread scan");
    require(world.declare_object_word(3,8,0)&&vm.start(key(4096),{1,3,3,0})==odh::VmStatus::running&&
        odh_host::functional_selector_in_use(world,target,group,thread)==true,
        "a real Thread Callee reference to another physical group member counts as in use");
    group_two=false;
    require(odh_host::functional_selector_in_use(world,target,group,thread)==false,
        "a different Callee outside the authenticated group never counts by proximity");
    require(vm.start(key(4096),{1,2,2,0})==odh::VmStatus::running&&
        odh_host::functional_selector_in_use(world,target,group,thread)==true,
        "real Main captures the candidate generation before native removal");
    require(world.remove_retaining_body(target)&&world.create(2,odh::EntityKind::object)&&world.declare_object_word(2,8,0),
        "old Callee body is retained while its numeric ID is reused");
    target=reference(2);
    require(odh_host::functional_selector_in_use(world,target,group,thread)==false,
        "retained old Callee with the same ID does not occupy a replacement candidate");
    require(vm.start(key(4096),{1,77,77,0})==odh::VmStatus::running&&
        odh_host::functional_selector_in_use(world,target,group,thread)==false,
        "known nil positive Callee ID never binds to an unrelated candidate");
    require(world.create(4,odh::EntityKind::avatar)&&
        !odh_host::functional_selector_in_use(world,target,group,thread),
        "unregistered visitor Thread cannot prove a complete negative use scan");
    require(world.remove(4)&&vm.start(key(4096),{1,2,2,0})==odh::VmStatus::running,
        "all live avatar Threads are again authenticated");
    unsigned checks_inside=0;
    // Reuse immutable source code with the actual service binding in a fresh
    // graph; no copied frames or fabricated invocation are used.
    odh::Vm active({{key(4096),routine(99)},{key(4097),routine(98)}},world.vm_memory(),{},
        [&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
            if(instruction.opcode!=99)return {};
            const odh_host::FunctionalSelectorThread active_thread=[&](auto owner)->const odh::Vm*{
                return owner.id==actor.id&&owner.generation==actor.generation?dispatch.origin:nullptr;
            };
            const auto result=const_cast<odh::Vm*>(dispatch.origin)->evaluate_check(key(4097),{1,3,3,0},{0,0,0,0},32,
                [&](const auto& current,const auto& op,const auto&,const auto&)->odh::PrimitiveOutcome{
                    if(op.opcode!=98)return {};
                    require(current.context.callee==3&&odh_host::functional_selector_in_use(world,target,group,active_thread)==true,
                        "real nested Check's Callee differs while real Entity.Thread Main still uses the candidate");
                    ++checks_inside;return {odh::PrimitiveDisposition::goto_true,{}};
                },true);
            require(result.started&&result.status==odh::VmStatus::returned_true,"actual source condition completed");
            return {odh::PrimitiveDisposition::goto_true,{}};
        });
    require(active.start(key(4096),{1,2,2,0})==odh::VmStatus::running&&active.run(32)==odh::VmStatus::returned_true&&checks_inside==1,
        "host selector uses the genuine parked Main through a synchronous source dispatch");
    const auto named=odh::named_routine_key(400,false);
    require(vm.install_owner(33,{{named,routine(98)}}),"explicit named resource installed on the real Thread");
    const auto named_result=vm.run_named_in_stack(named,{1,3,3,33},{0,0,0,0},16,
        [&](const auto&,const auto& instruction,const auto&,const auto&)->odh::PrimitiveOutcome{
            if(instruction.opcode!=98)return {};
            require(odh_host::functional_selector_in_use(world,target,group,thread)==false,
                "RunInMyStack replaces the real Thread stack, so its current Callee controls source IsInUse");
            return {odh::PrimitiveDisposition::goto_true,{}};
        });
    require(named_result.started&&named_result.status==odh::VmStatus::returned_true,
        "named real-stack view restores the parent without a fake use claim");
    std::cout<<checks<<" functional selector host checks passed (synthetic fixtures only)\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
