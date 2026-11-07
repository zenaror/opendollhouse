#include "odh/run_functional_tree.hpp"

#include <array>
#include <limits>
#include <utility>

namespace odh {
namespace {

constexpr std::array<std::uint8_t, 15> kFunctionEntryPoint{{
    18,19,20,21,22,23,24,25,26,27,14,28,16,17,29
}};

PrimitiveOutcome branch(bool value) {
    return {value ? PrimitiveDisposition::goto_true : PrimitiveDisposition::goto_false,{}};
}

PrimitiveOutcome unsupported(std::string why,std::uint64_t = 0) {
    return {PrimitiveDisposition::unsupported,std::move(why)};
}

std::uint16_t operand_function(const Instruction& instruction) noexcept {
    return static_cast<std::uint16_t>(instruction.operands[0] |
        (static_cast<std::uint16_t>(instruction.operands[1]) << 8));
}

const Bhav* resolved_bhav(const FunctionalTreeResolvedCode& code) {
    if(code.status!=FunctionalTreeCodeStatus::resolved)return nullptr;
    if(code.key.scope==RoutineScope::global) {
        if(!code.global_program)return nullptr;
        const auto found=code.global_program->routines().find(code.key);
        return found==code.global_program->routines().end()?nullptr:&found->second;
    }
    if(!code.owner_program)return nullptr;
    const auto found=code.owner_program->routines().find(code.key);
    return found==code.owner_program->routines().end()?nullptr:&found->second;
}

bool valid_code(const FunctionalTreeResolvedCode& code) noexcept {
    return code.status==FunctionalTreeCodeStatus::resolved &&
        code.owner && code.owner_program && code.global_program;
}

bool active_request(const RunFunctionalTreeRequest& request) noexcept {
    return request.world&&request.source_vm&&request.source_frame&&
        request.source_instruction&&request.source_instruction->opcode==20&&
        request.source_dispatch.origin==request.source_vm&&
        !request.source_dispatch.check_mode&&
        request.source_vm->matches_active_primitive(*request.source_frame,
            *request.source_instruction,request.source_dispatch);
}

} // namespace

PrimitiveOutcome run_functional_tree(const RunFunctionalTreeRequest& request) {
    if(!active_request(request))
        return unsupported("RunFunctionalTree requires the exact active opcode-20 dispatch");

    const auto function=operand_function(*request.source_instruction);
    // Wedding cake and similar authored operands intentionally skip.
    if(function==65535)return branch(false);
    if(function>=kFunctionEntryPoint.size())
        return unsupported("RunFunctionalTree function index is outside the source mapping");

    if(!request.target_is_live||!request.identities_still_match||!request.resolve_routine)
        return unsupported("RunFunctionalTree lacks live target/resource identity evidence");
    const auto target_id=request.source_frame->context.stack_object;
    if(target_id<=0)return branch(false);
    const auto* target=request.world->find(target_id);
    if(!target)return branch(false);
    const auto target_generation=target->generation;
    if(!target_generation||!request.target_is_live(target_id,target_generation))return branch(false);
    if(!request.identities_still_match())
        return unsupported("RunFunctionalTree caller/target identity evidence is stale");
    if(!target->entry_points)
        return unsupported("RunFunctionalTree target has no authenticated entry-point table");

    const auto entry_index=kFunctionEntryPoint[function];
    if(entry_index>=target->entry_points->size())
        return unsupported("RunFunctionalTree entry-point table is shorter than the selected source index");
    const auto selected=(*target->entry_points)[entry_index];
    if(!selected.action)return branch(false);

    if(selected.condition) {
        FunctionalTreeResolvedCode condition;
        try { condition=request.resolve_routine(target_id,target_generation,selected.condition); }
        catch(const std::exception& error) {
            return unsupported(std::string("RunFunctionalTree condition resource lookup failed: ")+error.what());
        } catch(...) {
            return unsupported("RunFunctionalTree condition resource lookup failed");
        }
        if(condition.status==FunctionalTreeCodeStatus::unsupported)
            return unsupported("RunFunctionalTree condition resource provenance is unresolved");
        bool execute=true;
        if(condition.status==FunctionalTreeCodeStatus::resolved) {
            const auto expected_condition_key=routine_key(selected.condition);
            if(!valid_code(condition)||condition.key.scope!=expected_condition_key.scope||
               condition.key.id!=expected_condition_key.id)
                return unsupported("RunFunctionalTree condition bank is incomplete");
            const auto* check_bhav=resolved_bhav(condition);
            // GetRoutineWithOwner returning null skips the condition. A known
            // empty BHAV is different: EvaluateCheck cannot push it and returns
            // false. This distinction is source-visible.
            if(check_bhav&&!check_bhav->instructions.empty()) {
                if(!check_bhav->metadata || check_bhav->version!=0x8002)
                    return unsupported("RunFunctionalTree condition BHAV metadata is unsupported");
                if(!request.check_instruction_budget)
                    return unsupported("RunFunctionalTree check instruction budget is zero");
                if(request.caller_register_owner&&request.caller_thread_absent_proven)
                    return unsupported("RunFunctionalTree caller register-owner evidence conflicts");
                if(!request.caller_register_owner&&!request.caller_thread_absent_proven)
                    return unsupported("RunFunctionalTree caller Thread registers are unproven");
                try {
                    VmLimits limits;
                    SharedRoutineOwners owner_programs;
                    owner_programs.emplace(condition.owner,condition.owner_program);
                    Vm check(condition.global_program,ShareVmProgramTag{},condition.memory,limits,
                        VmPrimitive{},std::move(owner_programs),ShareVmOwnerProgramsTag{});
                    if(!check.set_owner_resolver(condition.owner_resolver))
                        return unsupported("RunFunctionalTree condition resource context could not bind");
                    if(!check.has_routine(condition.key,condition.owner))
                        return unsupported("RunFunctionalTree condition key is not executable in its exact bank");
                    const VmContext check_context{request.source_frame->context.caller,
                        target_id,target_id,condition.owner};
                    const std::vector<std::int16_t> zero_arguments(4,0);
                    if(check.start(condition.key,check_context,zero_arguments,true)!=VmStatus::running)
                        return unsupported("RunFunctionalTree condition check VM could not start");
                    if(!request.identities_still_match()||
                       !request.target_is_live(target_id,target_generation))
                        return unsupported("RunFunctionalTree identity changed before condition execution");
                    const auto result=request.caller_register_owner
                        ? check.run_synchronous_check_with_registers(*request.caller_register_owner,true,
                            request.check_instruction_budget,request.check_primitive)
                        : check.run_synchronous_check(request.check_instruction_budget,request.check_primitive);
                    if(!request.identities_still_match()||
                       !request.target_is_live(target_id,target_generation))
                        return unsupported("RunFunctionalTree identity changed during condition execution", result.steps);
                    if(!result.started)
                        return unsupported(result.diagnostic.empty()?"RunFunctionalTree condition check did not start":result.diagnostic,
                            result.steps);
                    if(result.status==VmStatus::returned_true)execute=true;
                    else if(result.status==VmStatus::returned_false)execute=false;
                    else return unsupported(result.diagnostic.empty()?"RunFunctionalTree condition did not return synchronously":result.diagnostic,
                        result.steps);
                } catch(const std::exception& error) {
                    return unsupported(std::string("RunFunctionalTree check execution failed: ")+error.what());
                } catch(...) {
                    return unsupported("RunFunctionalTree check execution failed");
                }
            } else if(check_bhav) {
                execute=false;
            }
        }
        if(!execute)return branch(false);
    }

    if(!request.identities_still_match()||
       !request.target_is_live(target_id,target_generation))
        return unsupported("RunFunctionalTree target changed before action lookup");

    const auto* live_target=request.world->find(target_id);
    if(!live_target||live_target->generation!=target_generation||!live_target->entry_points||
       entry_index>=live_target->entry_points->size())
        return unsupported("RunFunctionalTree target entry table changed during condition execution");
    const auto current_action=(*live_target->entry_points)[entry_index].action;
    // Source rereads the function after checks without repeating the initial
    // zero-action guard; even ID0 must be looked up in the exact global bank.

    FunctionalTreeResolvedCode action;
    try { action=request.resolve_routine(target_id,target_generation,current_action); }
    catch(const std::exception& error) {
        return unsupported(std::string("RunFunctionalTree action resource lookup failed: ")+error.what());
    } catch(...) {
        return unsupported("RunFunctionalTree action resource lookup failed");
    }
    if(action.status==FunctionalTreeCodeStatus::unsupported)
        return unsupported("RunFunctionalTree action resource provenance is unresolved");
    if(!request.identities_still_match()||
       !request.target_is_live(target_id,target_generation))
        return unsupported("RunFunctionalTree target/resource identity changed during action lookup");
    if(action.status==FunctionalTreeCodeStatus::target_resource_absent)return branch(false);
    const auto expected_action_key=routine_key(current_action);
    if(!valid_code(action)||action.key.scope!=expected_action_key.scope||
       action.key.id!=expected_action_key.id)
        return unsupported("RunFunctionalTree action bank is incomplete or has the wrong routine key");
    const auto* action_bhav=resolved_bhav(action);
    if(!action_bhav)return branch(false); // exact owner bank is known and key is absent
    if(action_bhav->version!=0x8002||!action_bhav->metadata)
        return unsupported("RunFunctionalTree action BHAV metadata is unsupported");
    VmCall child{action.key,
        {request.source_frame->context.caller,target_id,target_id,action.owner},
        std::vector<std::int16_t>(action_bhav->metadata->arguments,0),
        VmCallReturnMode::ordinary};
    child.exact_arguments=true;

    std::optional<FunctionalTreeIconUpdate> icon_update;
    const auto flags=request.source_instruction->operands[2];
    if(flags>0) {
        if(!request.action_tree_for)
            return unsupported("RunFunctionalTree icon flag lacks an ActionTree resolver");
        std::optional<bool> action_tree;
        try { action_tree=request.action_tree_for(*request.source_frame,
            *request.source_instruction,request.source_dispatch); }
        catch(...) { return unsupported("RunFunctionalTree ActionTree lookup failed"); }
        if(!action_tree)return unsupported("RunFunctionalTree ActionTree identity is unresolved");
        if(*action_tree) {
            if(!request.prepare_icon_update)
                return unsupported("RunFunctionalTree lacks atomic active-action icon support");
            const auto* icon=request.world->find(target_id);
            if(!icon||icon->generation!=target_generation)
                return unsupported("RunFunctionalTree icon owner identity changed");
            try { icon_update=request.prepare_icon_update(*request.source_frame,
                *request.source_instruction,request.source_dispatch,target_id,
                target_generation,action_bhav->instructions.empty()?std::optional<VmCall>{}:
                    std::optional<VmCall>{child}); }
            catch(...) { return unsupported("RunFunctionalTree icon preparation failed"); }
            bool icon_prepared=false;
            try {
                icon_prepared=icon_update&&icon_update->can_commit&&icon_update->commit&&
                    icon_update->can_commit();
            } catch(...) {
                return unsupported("RunFunctionalTree icon preflight failed");
            }
            if(!icon_prepared)
                return unsupported("RunFunctionalTree icon update could not be preflighted");
        }
    }

    if(action_bhav->instructions.empty()) {
        // FreeSO Push(empty) returns false but RunFunctionalTree still returns
        // CONTINUE, so the current opcode is retried. It is not GOTO_FALSE.
        if(icon_update&&!icon_update->commit())
            return unsupported("RunFunctionalTree empty-tree icon commit was rejected");
        return {PrimitiveDisposition::continue_execution,{}};
    }
    bool call_ready=false;
    try { call_ready=request.source_vm->prepare_primitive_call(*request.source_frame,
            *request.source_instruction,request.source_dispatch,child); }
    catch(const std::exception& error) {
        return unsupported(std::string("RunFunctionalTree action call preparation failed: ")+error.what());
    } catch(...) {
        return unsupported("RunFunctionalTree action call preparation failed");
    }
    if(!call_ready)
        return unsupported("RunFunctionalTree child frame/resource cannot be accepted by the active VM");
    if(!request.identities_still_match()||
       !request.target_is_live(target_id,target_generation))
        return unsupported("RunFunctionalTree target changed before action publication");

    PrimitiveOutcome outcome{PrimitiveDisposition::call,{}};
    outcome.call=std::move(child);
    if(icon_update) {
        try {
            auto update=std::make_shared<FunctionalTreeIconUpdate>(std::move(*icon_update));
            outcome.call_commit=[update]() { return update->commit(); };
        } catch(...) {
            return unsupported("RunFunctionalTree icon commit callback could not be retained");
        }
    }
    return outcome;
}

} // namespace odh
