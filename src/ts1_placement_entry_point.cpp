// SPDX-License-Identifier: MPL-2.0
#include "odh/ts1_placement_entry_point.hpp"
namespace odh {
Ts1PlacementEntryPointResult evaluate_ts1_avatar_placement_entry_point(const Ts1AvatarPlacementEntryPointRequest& request){
    const auto fail=[](std::string why,std::uint64_t steps=0){return Ts1PlacementEntryPointResult{Ts1PlacementEntryPointState::unsupported,steps,std::move(why)};};
    const auto valid=[&](){
        if(!request.world||!request.invocation||!request.register_owner||!request.identities_still_match||!request.identities_still_match())return false;
        const auto& inv=*request.invocation;const auto op=inv.instruction.opcode;
        if((op!=5&&op!=16&&op!=46)||!inv.dispatch.origin||!inv.source_still_matches||!inv.source_still_matches()||
            !inv.dispatch.origin->matches_active_primitive(inv.frame,inv.instruction,inv.dispatch))return false;
        const auto* avatar=request.world->find(request.avatar.id);
        if(!avatar||avatar->generation!=request.avatar.generation||avatar->kind!=EntityKind::avatar||avatar->ui_ghost||
            !request.avatar.generation||!avatar->entry_points||avatar->entry_points->size()<=9||
            !request.global_program||!request.owner_program||!request.code_owner||
            request.register_owner->shared_program_for_owner(0)!=request.global_program||
            request.register_owner->shared_program_for_owner(request.code_owner)!=request.owner_program)return false;
        return request.register_owner->stack().empty()||request.register_owner->stack().front().context.caller==request.avatar.id;
    };
    try{
        if(!request.instruction_budget||!valid())return fail("avatar placement EP9 requires exact live source and avatar Thread/resource identity");
        const auto action=request.world->find(request.avatar.id)->entry_points->at(9).action;
        if(action<=255)return {Ts1PlacementEntryPointState::evaluated_false,0,"avatar EP9 has no executable action"};
        const auto key=routine_key(action);
        const auto& bank=key.scope==RoutineScope::global?request.global_program:request.owner_program;
        const auto found=bank->routines().find(key);
        if(found==bank->routines().end()||found->second.instructions.empty())return {Ts1PlacementEntryPointState::evaluated_false,0,"avatar EP9 routine is known absent or empty"};
        VmLimits limits;
        SharedRoutineOwners owners{{request.code_owner,request.owner_program}};
        Vm check(request.global_program,ShareVmProgramTag{},request.live_memory,limits,{},std::move(owners),ShareVmOwnerProgramsTag{});
        const VmContext context{request.avatar.id,request.avatar.id,0,request.code_owner};
        if(check.start(key,context,{0,0,0,0},true)!=VmStatus::running||!valid())return fail("avatar placement EP9 could not start");
        const auto result=check.run_synchronous_check_with_registers(*request.register_owner,true,request.instruction_budget,request.check_primitive);
        if(!valid())return fail("avatar/source identity changed during placement EP9",result.steps);
        if(!result.started)return fail(result.diagnostic.empty()?"avatar placement EP9 check did not start":result.diagnostic,result.steps);
        if(result.status==VmStatus::returned_true)return {Ts1PlacementEntryPointState::evaluated_true,result.steps,{}};
        if(result.status==VmStatus::returned_false)return {Ts1PlacementEntryPointState::evaluated_false,result.steps,{}};
        return fail(result.diagnostic.empty()?"avatar placement EP9 did not return synchronously":result.diagnostic,result.steps);
    }catch(const std::exception& error){return fail(error.what());}
}
}
