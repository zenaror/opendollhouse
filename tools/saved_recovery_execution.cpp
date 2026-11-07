// SPDX-License-Identifier: MPL-2.0
#include "saved_recovery_execution.hpp"
#include <limits>
#include <memory>
#include <set>
#include <algorithm>
namespace odh_host {
namespace {
thread_local std::set<const odh::Vm*> active_threads;
std::optional<std::size_t> entry(SavedRecoveryExecutionPhase phase){
    switch(phase){case SavedRecoveryExecutionPhase::load2:return 2;case SavedRecoveryExecutionPhase::init0:return 0;
        case SavedRecoveryExecutionPhase::cleanup3:return 3;case SavedRecoveryExecutionPhase::skipped4:return 4;
        case SavedRecoveryExecutionPhase::placement9:return 9;
        case SavedRecoveryExecutionPhase::remove10:return 10;
        case SavedRecoveryExecutionPhase::wall6:return 6;
        case SavedRecoveryExecutionPhase::restart_main:
        case SavedRecoveryExecutionPhase::reset_execution:case SavedRecoveryExecutionPhase::start_main:return 1;}return {};
}
struct CurrentBinding {odh::Vm* thread=nullptr;odh::CodeOwner owner=0;};
std::optional<CurrentBinding> acquire_current_binding(const SavedRecoveryExecutionContext& c){
    // Authentication/reacquisition precedes even dispatch_active()/bank reads:
    // a retired Load Thread may already have been destroyed by its real owner.
    if(!c.thread||!c.owner||!c.authority||!c.thread_for||!c.owner_for)return {};
    auto* actual=c.thread_for(c.entity);const auto owner=c.owner_for(c.entity);
    if(actual!=c.thread||!owner||*owner!=c.owner||!c.world.is_live_reference(c.entity)||!c.authority())return {};
    return CurrentBinding{actual,*owner};
}
bool current(const SavedRecoveryExecutionContext& c){return acquire_current_binding(c).has_value();}
bool main_phase(SavedRecoveryExecutionPhase phase){return phase==SavedRecoveryExecutionPhase::restart_main||
    phase==SavedRecoveryExecutionPhase::reset_execution||phase==SavedRecoveryExecutionPhase::start_main;}
bool stack_object_current(const SavedRecoveryExecutionContext& c,const SavedRecoveryExecutionRequest& request){
    if(request.phase!=SavedRecoveryExecutionPhase::skipped4)return true;
    const auto* person=c.world.resolve_reference(request.stack_object);
    return c.world.is_live_reference(request.stack_object)&&person&&person->kind==odh::EntityKind::avatar&&!person->dead&&!person->ui_ghost;
}
bool detached(const odh::Vm& vm){return !vm.pending()&&vm.suspended_pending().empty()&&!vm.interaction_return()&&
    !vm.external_frame_return()&&vm.immediate_continuations().empty()&&!vm.immediate_return()&&
    std::none_of(vm.stack().begin(),vm.stack().end(),[](const auto& frame){return frame.return_mode==odh::VmCallReturnMode::external_frame_next_tick||
        frame.return_mode==odh::VmCallReturnMode::immediate;});}
}
static SavedRecoveryExecutionPreflight prepare(const SavedRecoveryExecutionContext& c,
        const SavedRecoveryExecutionRequest& request,bool own_lease=false){
    SavedRecoveryExecutionPreflight result;
    const auto reject=[&](const char* text){result.diagnostic=text;return result;};
    try{
        const auto index=entry(request.phase);if(!index)return reject("unknown recovery execution phase");
        if(request.argument_mode!=SavedRecoveryArgumentMode::native_temp_defaults&&
           request.argument_mode!=SavedRecoveryArgumentMode::explicit_four_zeros)return reject("unknown recovery argument mode");
        if(!current(c))return reject("recovery execution exact Thread/owner/World authority unavailable");
        if(!own_lease&&active_threads.count(c.thread))return reject("recovery execution Thread already has an active lifecycle loan");
        const auto* entity=c.world.resolve_reference(c.entity);
        if(!entity||entity->dead||entity->ui_ghost||!entity->definition||!entity->entry_points||
           (entity->entry_points->size()<=*index&&request.phase!=SavedRecoveryExecutionPhase::skipped4))
            return reject("recovery execution actual entity/entry table unavailable");
        if(!stack_object_current(c,request))return reject("recovery EP4 exact live person StackObject unavailable");
        if(!c.guarded_operation)return reject("recovery execution LifecycleScope authority unavailable");
        if(request.allow_lifecycle_deletion&&!c.lifecycle_deleted)return reject("recovery execution exact lifecycle deletion authority unavailable");
        if(!request.instruction_budget||request.instruction_budget>1000000)return reject("recovery execution instruction budget outside bounds");
        const auto& vm=*c.thread;
        if(vm.dispatch_active())return reject("recovery execution requires the actual Thread safe point");
        if(!vm.shared_program_for_owner(c.owner))return reject("recovery execution immutable owner bank is not installed");
        odh::VmContext context{c.entity.id,c.entity.id,request.phase==SavedRecoveryExecutionPhase::skipped4?request.stack_object.id:std::int16_t{0},c.owner};
        if(!vm.bind_context_live(context)||odh::vm_context_reference(context,odh::VmEntityRole::caller)!=c.entity||
           odh::vm_context_reference(context,odh::VmEntityRole::callee)!=c.entity)
            return reject("recovery execution Thread memory resolves a different World identity");
        if(request.phase==SavedRecoveryExecutionPhase::skipped4&&
           odh::vm_context_reference(context,odh::VmEntityRole::stack_object)!=request.stack_object)
            return reject("recovery EP4 Thread memory resolves a different person StackObject");
        result.action=request.phase==SavedRecoveryExecutionPhase::reset_execution||entity->entry_points->size()<=*index?0:entity->entry_points->at(*index).action;
        const auto resource_action=request.phase==SavedRecoveryExecutionPhase::reset_execution?
            entity->entry_points->at(*index).action:result.action;
        if(resource_action>255){const auto key=odh::routine_key(resource_action);
            const auto bank=vm.shared_program_for_owner(key.scope==odh::RoutineScope::global?0:c.owner);
            if(!bank)return reject("recovery execution root bank unavailable");
            const auto routine=bank->routines().find(key);result.executable=routine!=bank->routines().end()&&!routine->second.instructions.empty();
            if(result.executable&&!main_phase(request.phase)){
                const auto& root=routine->second;
                std::optional<std::size_t> native_count;
                if(root.version==0x8000)native_count=4;
                else if(root.version==0x8001)native_count=std::min<std::size_t>(root.header[1],4);
                else if(root.version==0x8002&&root.metadata)native_count=root.metadata->arguments;
                if(!native_count)return reject("recovery native entry argument metadata unavailable");
                if(request.argument_mode==SavedRecoveryArgumentMode::native_temp_defaults){
                    result.argument_count=*native_count;
                    if(result.argument_count>8)return reject("recovery native entry arguments exceed proven TEMP words");
                }
            }
        }
        if(main_phase(request.phase)||request.argument_mode==SavedRecoveryArgumentMode::explicit_four_zeros)result.argument_count=4;
        if(request.phase==SavedRecoveryExecutionPhase::reset_execution||request.phase==SavedRecoveryExecutionPhase::start_main){
            if(entity->kind!=odh::EntityKind::object)return reject("split recovery reset/start requires the actual object class");
        }
        if(request.phase==SavedRecoveryExecutionPhase::restart_main||request.phase==SavedRecoveryExecutionPhase::reset_execution||
           request.phase==SavedRecoveryExecutionPhase::start_main){
            // Original object restart retires the entire frame vector after
            // its real services release ownership. Residual VM bookkeeping is
            // not itself an external owner. Person use-unwind remains ordered.
            if(entity->kind==odh::EntityKind::avatar&&!detached(vm))
                return reject("person recovery restart requires ordered continuation detachment before reset");
            if(!c.external_owners_detached || !c.external_owners_detached(c.entity,*c.thread,c.owner))
                return reject("recovery restart requires proof of actual external owner detachment before reset");
            if(!current(c))return reject("recovery restart Thread authority changed during external owner proof");
            const auto maximum=std::numeric_limits<std::uint64_t>::max();
            if(request.phase==SavedRecoveryExecutionPhase::start_main?
                (result.executable&&vm.run_generation()==maximum):
                (vm.run_generation()==maximum||(result.executable&&vm.run_generation()==maximum-1)))
                return reject("recovery restart run-generation budget exhausted before reset");
            if(request.phase==SavedRecoveryExecutionPhase::start_main&&
               (!request.reset_authority||!request.reset_authority->matches(&c.world,c.entity,c.thread,c.owner,vm.run_generation())||
                vm.status()!=odh::VmStatus::returned_false||!vm.stack().empty()||vm.steps()||!detached(vm)))
                return reject("recovery Main start requires its exact successful reset authority and idle Thread");
        }else if(result.executable&&!c.synchronous_primitive)return reject("recovery execution actual synchronous primitive unavailable");
        if(!current(c))return reject("recovery execution authority changed during preflight");
        result.supported=true;return result;
    }catch(const std::exception& e){result.diagnostic=e.what();return result;}
    catch(...){return reject("recovery execution preflight authority raised an exception");}
}
SavedRecoveryExecutionPreflight preflight_saved_recovery_execution(const SavedRecoveryExecutionContext& c,
        const SavedRecoveryExecutionRequest& request){return prepare(c,request);}
SavedRecoveryExecutionResult execute_saved_recovery_execution(const SavedRecoveryExecutionContext& c,
        const SavedRecoveryExecutionRequest& request){
    SavedRecoveryExecutionResult result;const auto prepared=preflight_saved_recovery_execution(c,request);
    result.action=prepared.action;if(!prepared.supported){result.diagnostic=prepared.diagnostic;return result;}
    if(request.phase!=SavedRecoveryExecutionPhase::restart_main&&request.phase!=SavedRecoveryExecutionPhase::reset_execution&&
       request.phase!=SavedRecoveryExecutionPhase::start_main&&prepared.action<=255){result.completed=true;result.no_action=true;return result;}
    if(active_threads.size()>=64){result.diagnostic="recovery execution nested Thread budget exceeded";return result;}
    active_threads.insert(c.thread);struct Release{const odh::Vm* thread;~Release(){active_threads.erase(thread);}}release{c.thread};
    struct Lease{bool active=true,accepted=false;unsigned calls=0;};const auto lease=std::make_shared<Lease>();
    struct End{std::shared_ptr<Lease> value;~End(){value->active=false;}}end{lease};
    try{
        const std::function<bool()> operation=[&,lease]{
            if(!lease->active||++lease->calls!=1)return false;
            const auto now=prepare(c,request,true);
            if(!now.supported||now.action!=prepared.action||now.executable!=prepared.executable||now.argument_count!=prepared.argument_count){result.diagnostic="recovery execution changed before guarded operation";return false;}
            odh::VmContext context{c.entity.id,c.entity.id,request.phase==SavedRecoveryExecutionPhase::skipped4?request.stack_object.id:std::int16_t{0},c.owner};
            if(!c.thread->bind_context_live(context))return false;
            result.effects_started=true;
            if(request.phase==SavedRecoveryExecutionPhase::restart_main||request.phase==SavedRecoveryExecutionPhase::reset_execution||
               request.phase==SavedRecoveryExecutionPhase::start_main){
                if(request.phase!=SavedRecoveryExecutionPhase::start_main&&!c.thread->reset_execution_state()){
                    result.diagnostic="recovery restart reset refused; no async ownership was fabricated";return false;
                }
                if(request.phase==SavedRecoveryExecutionPhase::reset_execution){
                    result.reset_authority=std::shared_ptr<SavedRecoveryResetAuthority>(new SavedRecoveryResetAuthority(
                        &c.world,c.entity,c.thread,c.owner,c.thread->run_generation()));
                    result.vm_status=c.thread->status();lease->accepted=true;return true;
                }
                if(prepared.executable){result.vm_status=c.thread->start_after_reset(odh::routine_key(prepared.action),context,{0,0,0,0},true);
                    result.main_started=result.vm_status==odh::VmStatus::running;
                    if(!result.main_started){result.diagnostic="recovery Main did not start after actual reset";return false;}
                }else{result.vm_status=c.thread->status();result.no_action=true;}
                if(request.phase==SavedRecoveryExecutionPhase::start_main)request.reset_authority->consumed_=true;
                lease->accepted=true;return true;
            }
            const bool native_defaults=request.argument_mode==SavedRecoveryArgumentMode::native_temp_defaults;
            std::vector<std::int16_t> arguments(native_defaults?prepared.argument_count:4,0);
            if(native_defaults)std::copy_n(c.thread->temps().begin(),prepared.argument_count,arguments.begin());
            const auto checked=c.thread->evaluate_entry_point_check(odh::routine_key(prepared.action),context,arguments,
                request.instruction_budget,native_defaults||request.share_registers,c.synchronous_primitive,c.trace,request.allow_lifecycle_deletion);
            result.vm_status=checked.status;result.instructions=checked.steps;
            const bool deleted=request.allow_lifecycle_deletion&&c.lifecycle_deleted&&c.lifecycle_deleted(c.entity);
            if(checked.started&&checked.lifecycle_deleted&&deleted){result.lifecycle_deleted=true;lease->accepted=true;return true;}
            if(!checked.started||checked.lifecycle_deleted||deleted||
               (checked.status!=odh::VmStatus::returned_true&&checked.status!=odh::VmStatus::returned_false)){
                result.diagnostic="recovery entry did not complete synchronously: "+checked.diagnostic;return false;
            }
            lease->accepted=true;return true;
        };
        // Pass the freshly reacquired Thread into the guard. The application
        // must build its LifecycleScope from this reference, never from a
        // pointer captured before preflight.
        const auto guarded_binding=acquire_current_binding(c);
        if(!guarded_binding){result.diagnostic="recovery Thread/owner changed before LifecycleScope";return result;}
        const bool guarded=c.guarded_operation(*guarded_binding->thread,c.entity,guarded_binding->owner,operation);lease->active=false;
        if(!guarded||lease->calls!=1||!lease->accepted){if(result.diagnostic.empty())result.diagnostic="recovery LifecycleScope did not complete exactly one guarded operation; completed effects retained";return result;}
        // Do not touch a cached Vm* after the host's guard may have drained a
        // successful self-delete. The exact deletion proof is its terminal fact.
        if(!result.lifecycle_deleted&&!current(c)){result.diagnostic="recovery execution exact Thread authority lost after operation";return result;}
        if(!stack_object_current(c,request)){result.diagnostic="recovery EP4 exact person StackObject lost after operation";return result;}
        if(result.reset_authority)result.reset_authority->usable_=true;
        result.completed=true;return result;
    }catch(const std::exception& e){result.diagnostic=e.what();return result;}
    catch(...){result.diagnostic="recovery execution callback failed; completed effects retained";return result;}
}
} // namespace odh_host
