// SPDX-License-Identifier: MPL-2.0
#include "odh/resident_action_control.hpp"
namespace odh {
std::optional<ResidentControlIdentity> resident_control_identity(const World& world,const Vm& vm,const InteractionQueue& queue){
    if(vm.dispatch_active()||!queue.bound_to(vm))return {};
    const auto saved=queue.snapshot();const auto* actor=world.find(saved.actor_id);
    if(!actor||actor->kind!=EntityKind::avatar||!actor->generation||actor->generation!=saved.actor_generation||
       !saved.run_generation||saved.run_generation!=vm.run_generation()||vm.dispatch_active())return {};
    return ResidentControlIdentity{actor->id,actor->generation,saved.run_generation};
}
namespace {
bool current(const World& world,const Vm& vm,const InteractionQueue& queue,ResidentControlIdentity expected){
    const auto actual=resident_control_identity(world,vm,queue);
    return actual&&actual->actor==expected.actor&&actual->generation==expected.generation&&actual->run_generation==expected.run_generation;
}
}
InteractionCancelResult cancel_resident_action(const World& world,const Vm& vm,InteractionQueue& queue,
    ResidentControlIdentity expected,std::uint64_t uid,std::string* diagnostic){
    if(diagnostic)diagnostic->clear();
    if(!uid||!current(world,vm,queue,expected)){
        if(diagnostic)*diagnostic="resident cancellation identity or VM boundary is stale";
        return InteractionCancelResult::unsupported;
    }
    const auto result=queue.cancel(uid);if(diagnostic)*diagnostic=queue.diagnostic();return result;
}
bool enqueue_resident_user_action(const World& world,const Vm& vm,InteractionQueue& queue,ResidentControlIdentity expected,
    const VmCall& call,std::uint64_t generation,std::uint32_t flags,std::optional<RoutineKey> check,
    std::optional<std::int32_t> number,std::string* diagnostic){
    if(diagnostic)diagnostic->clear();
    auto context=call.context;
    if(!world.bind_context_live(context)){if(diagnostic)*diagnostic="resident action reference binding failed";return false;}
    const auto* target=world.resolve_reference(vm_context_reference(context,VmEntityRole::callee));
    if(!current(world,vm,queue,expected)||context.caller!=expected.actor||context.caller_generation!=expected.generation||!target||!generation||target->generation!=generation||
       !world.is_live_reference(vm_context_reference(context,VmEntityRole::callee))||world.reference_is_dead(vm_context_reference(context,VmEntityRole::callee))||
       call.return_mode!=VmCallReturnMode::ordinary||!vm.has_routine(call.key,call.context.code_owner)){
        if(diagnostic)*diagnostic="resident user action identity, resource or VM boundary is stale";
        return false;
    }
    const auto accepted=queue.enqueue_source_user(call.key,context,call.arguments,flags,check,number);
    if(diagnostic)*diagnostic=queue.diagnostic();
    return accepted;
}
}
