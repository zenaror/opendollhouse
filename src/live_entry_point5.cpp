#include "odh/live_entry_point5.hpp"
#include "odh/routing.hpp"

namespace odh {
namespace {
LiveEntryPoint5Result unsupported(std::string message, std::uint64_t steps=0) {
    return {LiveEntryPoint5State::unsupported,steps,std::move(message)};
}

bool source_dispatch_is_live(const LiveEntryPoint5Request& request) noexcept {
    if(request.avatar_constructor_source){
        if(!request.world||request.object_creation_source||request.retired_container_source||request.original_recovery_source||
           request.route_placement_proof||request.route_execution_proof||request.source_vm||request.source_frame||
           request.source_instruction||request.source_dispatch.origin||request.source_dispatch.ticket||
           request.source_dispatch.check_mode||request.ghost!=0)return false;
        const auto& source=*request.avatar_constructor_source;
        if(!live_avatar_constructor_placement_source_matches(source,*request.world))return false;
        if(source.avatar.id==request.tested_entity&&source.avatar.generation==request.tested_generation)
            return request.register_owner==source.thread&&!request.no_thread_proven;
        return source.avatar.id==request.other_entity&&source.avatar.generation==request.other_generation&&request.register_owner!=source.thread;
    }
    if(request.original_recovery_source){
        if(!request.world||request.object_creation_source||request.retired_container_source||request.route_placement_proof||
           request.route_execution_proof||request.source_vm||request.source_frame||request.source_instruction||
           request.source_dispatch.origin||request.source_dispatch.ticket||request.source_dispatch.check_mode||request.ghost!=0)return false;
        const auto& source=*request.original_recovery_source;
        if(!live_original_recovery_placement_source_matches(source,*request.world))return false;
        if(source.person.id==request.tested_entity&&source.person.generation==request.tested_generation)
            return request.register_owner==source.thread&&!request.no_thread_proven;
        return source.person.id==request.other_entity&&source.person.generation==request.other_generation&&request.register_owner!=source.thread;
    }
    if(request.retired_container_source){
        if(!request.world||request.object_creation_source||request.route_placement_proof||request.route_execution_proof||
           request.source_vm||request.source_frame||request.source_instruction||request.source_dispatch.origin||request.source_dispatch.ticket||
           request.source_dispatch.check_mode||request.ghost!=0)
            return false;
        try {const auto& source=*request.retired_container_source;
            return source.world==request.world&&request.world->retired_child_authorized(source.container,source.child)&&
                source.source_still_matches&&source.source_still_matches()&&
                ((source.child.id==request.tested_entity&&source.child.generation==request.tested_generation)||
                 (source.child.id==request.other_entity&&source.child.generation==request.other_generation));
        }catch(...){return false;}
    }
    if(request.object_creation_source){
        try{
            if(request.source_vm||request.source_frame||request.source_instruction||request.source_dispatch.origin||
               request.source_dispatch.ticket||request.source_dispatch.check_mode||request.route_placement_proof||
               request.route_execution_proof||request.ghost!=0||!request.world)return false;
            const auto& source=*request.object_creation_source;
            // Check the scope first: an expired host capability must not touch
            // a VM whose construction owner may already have been released.
            if(!source.source_still_matches||!source.source_still_matches()||!source.source_vm||
               !source.source_run_generation||source.source_vm->run_generation()!=source.source_run_generation||
               source.created_entity<=0||!source.created_generation||
               (source.created_entity!=request.tested_entity&&source.created_entity!=request.other_entity))return false;
            if(source.active_dispatch&&!live_object_creation_active_dispatch_matches(source))return false;
            const auto* created=request.world->find(source.created_entity);
            return created&&(created->kind==EntityKind::object||created->kind==EntityKind::avatar)&&!created->ui_ghost&&
                created->generation==source.created_generation;
        }catch(...){return false;}
    }
    if(!request.source_vm||!request.source_frame||!request.source_instruction||
       request.source_dispatch.origin!=request.source_vm||request.source_dispatch.check_mode)return false;
    if(request.route_execution_proof){
        if(request.route_placement_proof||
           (request.source_instruction->opcode!=45&&request.source_instruction->opcode!=27))return false;
        switch(request.route_execution_phase){
        case RouteExecutionPhase::attempt_walk:if(request.ghost!=1)return false;break;
        case RouteExecutionPhase::set_position:break;
        default:return false;
        }
        return request.route_execution_proof->matches(*request.source_frame,*request.source_vm,
            *request.source_instruction,request.source_dispatch,request.route_execution_phase);
    }
    if(request.source_instruction->opcode==5||request.source_instruction->opcode==16||request.source_instruction->opcode==46)
        return request.source_vm->matches_active_primitive(*request.source_frame,
            *request.source_instruction,request.source_dispatch);
    return (request.source_instruction->opcode==45||
        is_source_goto_relative_slot_instruction(*request.source_instruction))&&request.route_placement_proof&&
        request.route_placement_proof->matches(*request.source_frame,*request.source_vm,
            *request.source_instruction,request.source_dispatch);
}

bool entities_match(const LiveEntryPoint5Request& request) {
    if(!request.world||request.tested_entity<=0||request.other_entity<=0||
       request.tested_entity==request.other_entity||!request.tested_generation||
       !request.other_generation||!request.identities_still_match||
       !request.identities_still_match())return false;
    const auto* tested=request.world->find(request.tested_entity);
    const auto* other=request.world->find(request.other_entity);
    return tested&&other&&tested->generation==request.tested_generation&&
        other->generation==request.other_generation;
}
} // namespace

bool live_object_creation_active_dispatch_matches(const LiveObjectCreationPlacementSource& source) noexcept {
    try {
        // Expire host ownership before dereferencing its borrowed VM.
        if(!source.source_still_matches||!source.source_still_matches()||!source.active_dispatch||
           !source.source_vm||!source.source_run_generation||source.created_entity<=0||!source.created_generation)return false;
        const auto& active=*source.active_dispatch;const auto& vm=*source.source_vm;
        return vm.run_generation()==source.source_run_generation&&active.instruction.opcode==42&&
            active.dispatch.origin==&vm&&active.dispatch.ticket&&
            vm.matches_active_query(active.frame,active.instruction,active.dispatch);
    }catch(...){return false;}
}

bool live_original_recovery_placement_source_matches(const LiveOriginalRecoveryPlacementSource& source,const World& world) noexcept {
    try{
        if(!source.source_still_matches||!source.source_still_matches()||source.world!=&world||!source.registration||
           world.original_load_person_at(*source.registration,0).status==OriginalLoadLinkStatus::unavailable||!source.traversal||
           world.original_load_head(*source.traversal).status==OriginalLoadLinkStatus::unavailable)return false;
        const auto* person=world.resolve_reference(source.person);
        if(!world.is_live_reference(source.person)||!person||person->kind!=EntityKind::avatar||person->dead||person->ui_ghost)return false;
        return source.thread&&source.run_generation&&!source.thread->dispatch_active()&&
            source.thread->run_generation()==source.run_generation;
    }catch(...){return false;}
}

bool live_avatar_constructor_placement_source_matches(const LiveAvatarConstructorPlacementSource& source,const World& world) noexcept {
    try {
        // Host ownership expires before any borrowed pointer is inspected.
        if(!source.source_still_matches||!source.source_still_matches()||source.world!=&world||
           !world.is_live_reference(source.avatar)||!source.thread||!source.run_generation||!source.code_owner)return false;
        const auto* avatar=world.resolve_reference(source.avatar);
        if(!avatar||avatar->kind!=EntityKind::avatar||avatar->dead||avatar->ui_ghost||!avatar->definition||
           !avatar->entry_points||avatar->entry_points->size()<=1||avatar->entry_points->at(1).action<=255)return false;
        const auto& thread=*source.thread;
        if(thread.dispatch_active()||thread.run_generation()!=source.run_generation||thread.status()!=VmStatus::running||
           thread.pending()||thread.stack().size()!=1)return false;
        const auto& frame=thread.stack().front();
        const auto main=routine_key(avatar->entry_points->at(1).action);
        return frame.pc==0&&frame.key.scope==main.scope&&frame.key.id==main.id&&
            frame.context.caller==source.avatar.id&&frame.context.callee==source.avatar.id&&
            frame.context.code_owner==source.code_owner;
    }catch(...){return false;}
}

LiveEntryPoint5Result evaluate_live_entry_point5(const LiveEntryPoint5Request& request) {
    if(request.avatar_constructor_source&&!source_dispatch_is_live(request))
        return unsupported("EP5 avatar constructor scope, prepared Main or exact identity unavailable");
    if(request.original_recovery_source&&!source_dispatch_is_live(request))
        return unsupported("EP5 original recovery scope, person or exclusive source authority unavailable");
    if(request.action<=255)
        return {LiveEntryPoint5State::evaluated_false,0,"entry point action is a native/no-BHAV function"};
    if(request.ghost!=0&&request.ghost!=1)
        return unsupported("EP5 ghost argument is outside the source boolean domain");
    if(request.ghost_sandbox)
        return unsupported("ghost-image EP5 sandbox is not implemented");
    if(!source_dispatch_is_live(request))
        return unsupported("EP5 requires an exact active Snap dispatch, range placement proof, route execution lease or live object-construction capability");
    if(!entities_match(request))
        return unsupported("EP5 entity identity or generation is unavailable");
    if((!request.object_creation_source&&!request.retired_container_source&&!request.original_recovery_source&&!request.avatar_constructor_source&&request.source_frame->context.caller<=0)||!request.code_owner||
       !request.global_program||!request.owner_program)
        return unsupported("EP5 exact owner/global bank evidence is incomplete");
    if(request.register_owner&&request.no_thread_proven)
        return unsupported("EP5 register owner conflicts with the proven no-thread state");
    if(!request.register_owner&&!request.no_thread_proven)
        return unsupported("EP5 missing Thread registers are not proven absent");
    if(!request.instruction_budget)
        return unsupported("EP5 instruction budget is zero");

    const auto key=routine_key(request.action);
    const auto& selected_bank=key.scope==RoutineScope::global
        ? request.global_program->routines() : request.owner_program->routines();
    // ExecuteGenericEntryPoint returns false if GetRoutineWithOwner cannot find
    // the selected BHAV in the exact resource bank.
    const auto selected=selected_bank.find(key);
    if(selected==selected_bank.end())
        return {LiveEntryPoint5State::evaluated_false,0,"EP5 BHAV is absent from its resolved resource bank"};


    // Push on the reference check thread rejects a known empty BHAV, leaving
    // no stack; EvaluateCheck then returns false without executing effects.
    if(selected->second.instructions.empty())
        return {LiveEntryPoint5State::evaluated_false,0,"EP5 BHAV has no instructions"};
    // ExecuteEntryPoint supplies exactly four arguments independent of metadata.

    try {
        VmLimits limits;
        // Source's List capacity5 is an allocation hint, not recursion depth.
        SharedRoutineOwners owners;
        owners.emplace(request.code_owner,request.owner_program);
        Vm check(request.global_program,ShareVmProgramTag{},request.live_memory,limits,{},
            std::move(owners),ShareVmOwnerProgramsTag{});
        if(!check.has_routine(key,request.code_owner))
            return {LiveEntryPoint5State::evaluated_false,0,"EP5 BHAV is absent for the exact code owner"};

        const VmContext context{request.tested_entity,request.tested_entity,
            request.other_entity,request.code_owner};
        const std::vector<std::int16_t> arguments{request.other_entity,request.ghost,0,0};
        if(!check.set_owner_resolver(request.owner_resolver))return unsupported("entry point5 resource context could not bind");
        if(check.start(key,context,arguments,true)!=VmStatus::running)
            return unsupported("EP5 disposable check VM could not start",check.steps());
        if(!source_dispatch_is_live(request)||!entities_match(request))
            return unsupported("EP5 source/World identity changed before check execution");

        const auto result=request.register_owner
            ? check.run_synchronous_check_with_registers(*request.register_owner,true,
                request.instruction_budget,request.check_primitive)
            : check.run_synchronous_check(request.instruction_budget,request.check_primitive);

        // The check uses the live World memory and publishes the tested
        // entity's TEMP/TempXL writes before reporting any non-return status.
        if(!source_dispatch_is_live(request)||!entities_match(request))
            return unsupported("EP5 source/World identity changed during check execution",result.steps);
        if(!result.started)
            return unsupported(result.diagnostic.empty()?"EP5 synchronous check did not start":result.diagnostic,
                result.steps);
        if(result.status==VmStatus::returned_true)
            return {LiveEntryPoint5State::evaluated_true,result.steps,{}};
        if(result.status==VmStatus::returned_false)
            return {LiveEntryPoint5State::evaluated_false,result.steps,{}};
        return unsupported(result.diagnostic.empty()?"EP5 did not return synchronously":result.diagnostic,
            result.steps);
    } catch(const std::exception& error) {
        return unsupported(std::string("EP5 live check failed: ")+error.what());
    } catch(...) {
        return unsupported("EP5 live check raised a non-standard exception");
    }
}

} // namespace odh
