// SPDX-License-Identifier: MPL-2.0
#include "odh/live_object_entry_point.hpp"

namespace odh {
namespace {
LiveObjectEntryPointResult unsupported(std::string message,std::uint64_t steps=0) {
    return {LiveObjectEntryPointState::unsupported,steps,std::move(message)};
}
}

LiveObjectEntryPointResult evaluate_live_object_entry_point(const LiveObjectEntryPointRequest& request) {
    if(!request.world||!request.register_owner||!request.identities_still_match||
       !request.global_program||!request.owner_program||!request.code_owner||!request.instruction_budget)
        return unsupported("object entry point requires live World, Thread, bank and budget evidence");
    if(request.object<=0||!request.object_generation||
       request.stack_object<0||(request.stack_object==0)!=(!request.stack_object_generation)||
       (request.source_arguments&&request.source_arguments->size()>4096))
        return unsupported("object entry point identity or argument extent is outside bounds");
    if(request.entry_point==11)
        return unsupported("user placement entry point 11 requires its source architecture wrapper");
    std::uint64_t executed_steps=0;
    try {
        const auto source=request.creation_source;
        const auto* world=request.world;auto* const registers=request.register_owner;
        const auto object=request.object;const auto generation=request.object_generation;
        const auto entry=request.entry_point,action=request.action;
        const auto stack_object=request.stack_object;const auto stack_generation=request.stack_object_generation;
        const auto owner=request.code_owner;const auto globals=request.global_program,bank=request.owner_program;
        const auto identities=request.identities_still_match;
        // Expired construction scopes must return before touching a released
        // source VM. The host binds this callback to its ephemeral factory lease.
        if(!source.source_still_matches||!source.source_still_matches()||!identities())
            return unsupported("object entry point construction capability has expired");
        if(!source.source_vm||!source.source_run_generation||source.created_entity!=object||source.created_generation!=generation)
            return unsupported("object entry point capability does not own this new object");
        const auto run=registers->run_generation();
        const auto current=[&]() {
            if(!source.source_still_matches()||!identities()||
               source.source_vm->run_generation()!=source.source_run_generation||registers->run_generation()!=run||
               registers->shared_program_for_owner(0)!=globals||registers->shared_program_for_owner(owner)!=bank)return false;
            const auto* entity=world->find(object);
            if(!entity||entity->kind!=EntityKind::object||entity->ui_ghost||entity->generation!=generation||!entity->entry_points)
                return false;
            if(entry<entity->entry_points->size()){
                if((*entity->entry_points)[entry].action!=action)return false;
            }else if(action!=0)return false;
            if(stack_object){const auto* other=world->find(stack_object);
                if(!other||other->generation!=stack_generation)return false;}
            return true;
        };
        if(!current())return unsupported("object entry point source, entry table, Thread or bank identity changed");
        // ExecuteEntryPoint returns false for a known short table, native entry
        // IDs or a missing routine in the exact selected resource bank.
        if(action<=255)return {LiveObjectEntryPointState::evaluated_false,0,{}};
        const auto key=routine_key(action);
        const auto& selected=key.scope==RoutineScope::global?globals->routines():bank->routines();
        const auto routine=selected.find(key);
        if(routine==selected.end()||routine->second.instructions.empty())
            return {LiveObjectEntryPointState::evaluated_false,0,{}};
        VmLimits limits;
        Vm check(globals,ShareVmProgramTag{},request.live_memory,limits,{},
            SharedRoutineOwners{{owner,bank}},ShareVmOwnerProgramsTag{});
        if(!check.set_owner_resolver(request.owner_resolver))return unsupported("object entry point resource context could not bind");
        const auto arguments=request.source_arguments?*request.source_arguments:std::vector<std::int16_t>(4,0);
        if(check.start(key,{object,object,stack_object,owner},arguments,true)!=VmStatus::running)
            return unsupported("object entry point disposable check could not start",check.steps());
        if(!current())return unsupported("object entry point source changed before synchronous execution");
        const auto result=check.run_synchronous_check_with_registers(*registers,true,
            request.instruction_budget,request.check_primitive);
        executed_steps=result.steps;
        if(!current())return unsupported("object entry point source changed during synchronous execution",result.steps);
        if(!result.started)return unsupported(result.diagnostic.empty()?"object entry point check did not start":result.diagnostic,result.steps);
        if(result.status==VmStatus::returned_true)return {LiveObjectEntryPointState::evaluated_true,result.steps,{}};
        if(result.status==VmStatus::returned_false)return {LiveObjectEntryPointState::evaluated_false,result.steps,{}};
        return unsupported(result.diagnostic.empty()?"object entry point did not return synchronously":result.diagnostic,result.steps);
    }catch(const std::exception& error){return unsupported(std::string("object entry point failed: ")+error.what(),executed_steps);}
    catch(...){return unsupported("object entry point raised a non-standard exception",executed_steps);}
}

} // namespace odh
