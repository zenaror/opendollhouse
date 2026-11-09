#pragma once

#include "odh/interaction.hpp"
#include "odh/sleep.hpp"

namespace odh {
struct EntityThreadControllersSnapshot {
    SleepSnapshot sleep;
    InteractionQueueSnapshot interactions;
    bool thread_interrupt=false;
    // Source VMThread.ScheduleIdleStart is shared by Sleep and IdleForInput.
    // Legacy controllers retain their existing independent timing until an
    // immediate action first requires preserving this shared source state.
    std::optional<std::uint64_t> schedule_idle_start={};
};

// Controllers that belong to one entity execution thread. Each instance owns
// an independent Sleep clock and interaction queue while sharing the World.
// World and the bound VM must stay valid while these controllers are used.
class EntityThreadControllers {
public:
    // Explicit actor-bound service capability. The service owns its shared
    // state; only its pending instruction is parked in the VM continuation.
    struct ImmediateService {
        ImmediatePendingAudit can_park;
        std::function<bool(std::uint64_t)> park,resume;
        ImmediatePendingAudit validate_parked;
    };
    bool set_immediate_service(ImmediateService service) {
        if(!fresh_||(sleep_.vm_&&sleep_.vm_->dispatch_active())||!service.can_park||
           !service.park||!service.resume||!service.validate_parked)return false;
        immediate_service_=std::move(service);return true;
    }
    EntityThreadControllers(World& world,std::int16_t actor,
                            std::function<void()> rng_cycle={})
        :sleep_(world,std::move(rng_cycle)),interactions_(world,actor) {
        sleep_.set_thread_interrupt_source([this]{return consume_thread_interrupt();});
        interactions_.set_thread_interrupt_source([this]{return consume_thread_interrupt();});
        const auto read_clock=[this]{return schedule_idle_start_;};
        const auto write_clock=[this](std::uint64_t value){
            if(schedule_idle_start_)schedule_idle_start_=value;
        };
        sleep_.set_schedule_idle_clock(read_clock,write_clock);
        interactions_.set_schedule_idle_clock(read_clock,write_clock);
        interactions_.set_immediate_parent_resume([this](std::uint64_t ticket){
            const auto* vm=sleep_.vm_;
            if(!ticket)return vm&&!vm->pending()&&(vm->status()==VmStatus::running||
                vm->status()==VmStatus::budget_exhausted);
            if(!vm||!vm->pending()||vm->pending()->ticket!=ticket)return false;
            const auto opcode=vm->pending()->instruction.instruction.opcode;
            if(opcode==0)return sleep_.retry_immediate_parent(ticket);
            if(opcode==17)return interactions_.retry_immediate_parent(ticket);
            return immediate_service_&&immediate_service_->resume(ticket);
        });
    }
    EntityThreadControllers(const EntityThreadControllers&)=delete;
    EntityThreadControllers& operator=(const EntityThreadControllers&)=delete;
    EntityThreadControllers(EntityThreadControllers&&)=delete;
    EntityThreadControllers& operator=(EntityThreadControllers&&)=delete;

    bool bind(Vm& vm) {return sleep_.bind(vm)&&interactions_.bind(vm);}
    bool bound_to(const Vm& vm) const noexcept {return sleep_.vm_==&vm&&interactions_.vm_==&vm;}
    // Runtime-only source Reset owner. The host discards its routing/playback
    // continuations and runs actual Cleanup/Main; no old wait is completed.
    void set_source_reset_handler(std::function<bool(Vm&,std::uint64_t)> handler){
        if(sleep_.vm_&&sleep_.vm_->dispatch_active())throw std::logic_error("cannot bind Reset during dispatch");
        source_reset_handler_=std::move(handler);
    }
    bool discard_for_source_reset(){
        if(!sleep_.vm_)return false;
        const bool authorized=sleep_.vm_->source_reset_lifecycle_active();
        if((sleep_.vm_->dispatch_active()&&!authorized)||sleep_.executing_||
           (interactions_.executing_&&(!authorized||!interactions_.eligibility_check_active_)))return false;
        interactions_.queue_.clear();interactions_.active_.reset();interactions_.suspended_active_.clear();
        interactions_.run_generation_=sleep_.vm_->run_generation();
        interactions_.wait_.reset();interactions_.completion_.reset();interactions_.resume_tick_.reset();
        interactions_.interrupted_=false;interactions_.diagnostic_.clear();
        sleep_.job_.reset();sleep_.interrupted_=false;sleep_.notify_resume_=false;sleep_.diagnostic_.clear();
        return true;
    }
    // Called only in the source Reset helper's avatar-effects stage. Preserve
    // schedule/clock fields, but bind remaining controllers to the new Main.
    bool finish_source_reset(){
        if(!sleep_.vm_||sleep_.vm_!=interactions_.vm_||!sleep_.vm_->source_reset_lifecycle_active()||
           sleep_.executing_||(interactions_.executing_&&!interactions_.eligibility_check_active_))return false;
        interactions_.run_generation_=sleep_.vm_->run_generation();
        if(sleep_.actor_)sleep_.actor_->run_generation=sleep_.vm_->run_generation();
        return true;
    }
    // Original Person recovery detaches asynchronous owners before popping
    // frames. Queued/active action headers remain available for ordered EP4
    // and for the later Main/EP3 phases; ordinary cancellation is separate.
    bool discard_waits_for_source_recovery(){
        if(!sleep_.vm_||sleep_.vm_->dispatch_active()||sleep_.executing_||interactions_.executing_)return false;
        sleep_.job_.reset();sleep_.interrupted_=false;sleep_.notify_resume_=false;sleep_.diagnostic_.clear();
        interactions_.wait_.reset();interactions_.resume_tick_.reset();interactions_.completion_.reset();
        return true;
    }
    bool rebase_after_source_recovery(){
        if(!discard_waits_for_source_recovery()||!interactions_.source_recovery_rebase())return false;
        if(sleep_.actor_)sleep_.actor_->run_generation=sleep_.vm_->run_generation();
        thread_interrupt_=false;schedule_idle_start_=sleep_.clock_;
        return true;
    }
    EntityThreadControllersSnapshot snapshot() const {
        return {sleep_.snapshot(),interactions_.snapshot(),thread_interrupt_,schedule_idle_start_};
    }
    // Both services must be fresh and already bound to the same restored VM.
    // Recreate equivalent World/resource/RNG callbacks before restoring. The
    // value-only snapshot omits these bindings. Stage both copies before any
    // publication so invalid values and allocation failures are atomic.
    bool restore_snapshot(const EntityThreadControllersSnapshot& state) {
        if(!fresh_||sleep_.vm_!=interactions_.vm_||!sleep_.valid_snapshot(state.sleep)||
           !interactions_.valid_snapshot(state.interactions))return false;
        const auto vm=sleep_.vm_->snapshot();
        if(state.schedule_idle_start&&(*state.schedule_idle_start>state.sleep.clock||
           state.sleep.clock!=state.interactions.clock))return false;
        if(!vm.immediate_continuations.empty()&&!state.schedule_idle_start)return false;
        for(const auto& continuation:vm.immediate_continuations){
            if(!continuation.parent_pending)continue;
            const auto& pending=*continuation.parent_pending;
            const auto opcode=pending.instruction.instruction.opcode;
            if(pending.context.caller!=state.interactions.actor_id||
               !continuation.parent_depth||continuation.parent_depth>vm.stack.size())return false;
            const auto& frame=vm.stack[continuation.parent_depth-1];
            if(opcode!=0&&opcode!=17){
                if(!immediate_service_||!immediate_service_->validate_parked(pending,frame))return false;
                continue;
            }
            const auto& operands=pending.instruction.instruction.operands;
            const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
            if(index>32767||index>=frame.arguments.size()||frame.arguments[index]<0)return false;
            if(opcode==0&&(!state.sleep.actor||
               state.sleep.actor->id!=state.interactions.actor_id||
               state.sleep.actor->generation!=state.interactions.actor_generation||
               state.sleep.actor->run_generation!=vm.run_generation))return false;
            if(opcode==17&&(operands[2]|(static_cast<unsigned>(operands[3])<<8))>1)return false;
        }
        auto candidate=state;
        sleep_.publish_snapshot(std::move(candidate.sleep));
        interactions_.publish_snapshot(std::move(candidate.interactions));
        thread_interrupt_=candidate.thread_interrupt;schedule_idle_start_=candidate.schedule_idle_start;fresh_=false;
        return true;
    }
    // Freshly born avatar with Main prepared but never dispatched. Establish
    // both service clocks atomically at the host's current simulation tick;
    // this does not tick either service or consume VM/RNG work.
    bool join_at(std::uint64_t current_tick) {
        if(!fresh_||!sleep_.vm_||sleep_.vm_!=interactions_.vm_||
           sleep_.executing_||interactions_.executing_||sleep_.vm_->dispatch_active()||
           current_tick>UINT32_MAX)return false;
        const auto vm=sleep_.vm_->snapshot();
        if(vm.status!=VmStatus::running||vm.steps||vm.stack.size()!=1||vm.stack.front().pc||
           vm.pending||!vm.suspended_pending.empty()||vm.interaction_return||vm.external_frame_return||
           vm.stack.front().return_mode!=VmCallReturnMode::ordinary)return false;
        auto state=snapshot();
        const auto& sleep=state.sleep;const auto& queue=state.interactions;
        if(state.thread_interrupt||sleep.actor||sleep.job||sleep.last_tick||sleep.clock||
           sleep.interrupted||sleep.notify_resume||queue.last_tick||queue.clock||queue.idle_start||
           queue.resume_tick||queue.sequence||!queue.actions.empty()||queue.active||queue.wait||
           queue.completion||queue.interrupted||!queue.suspended_active.empty())return false;
        state.sleep.clock=current_tick;state.sleep.last_tick=current_tick;
        state.interactions.clock=current_tick;state.interactions.last_tick=current_tick;
        // Binding before Main.start is supported for an otherwise empty queue.
        state.interactions.run_generation=vm.run_generation;
        return restore_snapshot(state);
    }
    VmPrimitive sleep_primitive() {return sleep_.primitive();}
    VmPrimitive interaction_primitive() {
        auto primitive=interactions_.primitive();
        return [this,primitive=std::move(primitive)](const auto& frame,const auto& instruction,
            const auto& temps,const auto& dispatch) {
            auto outcome=primitive(frame,instruction,temps,dispatch);
            if(outcome.call&&outcome.call->return_mode==VmCallReturnMode::immediate){
                auto commit=std::move(outcome.call_commit);
                outcome.call_commit=[this,commit=std::move(commit)]{
                    return park_immediate_parent(0)&&(!commit||commit());
                };
            }
            return outcome;
        };
    }
    bool stage_tick(std::uint64_t tick) {
        fresh_=false;
        if(!sleep_.vm_||(staged_tick_&&tick<=*staged_tick_))return false;
        const auto& vm=*sleep_.vm_;
        staged_dispatch_=vm.status()==VmStatus::running||vm.status()==VmStatus::budget_exhausted||
            vm.status()==VmStatus::next_tick;
        staged_unknown_wait_=false;
        if(vm.pending()){
            const auto opcode=vm.pending()->instruction.instruction.opcode;
            if(opcode==0)staged_dispatch_=sleep_.dispatch_due(tick)||thread_interrupt_;
            else if(opcode==17)staged_dispatch_=interactions_.wait_&&
                (tick>=interactions_.wait_->deadline||interactions_.interrupted_||thread_interrupt_);
            else {staged_dispatch_=true;staged_unknown_wait_=true;}
        }
        if(interactions_.resume_tick_&&tick>=*interactions_.resume_tick_)staged_dispatch_=true;
        if(!interactions_.tick(tick,true)||!sleep_.tick(tick,true))return false;
        staged_tick_=tick;staged_dispatched_=false;
        return true;
    }
    // Called in entity order, before the VM's budgeted work. Enqueueing an
    // immediate item alone never wakes a future Sleep/Idle deadline. Source
    // Notify requires this preflight at the entity's actual dispatch slot
    // (scheduler per_entity); all_before_dispatch cannot preempt a later Notify.
    bool before_run(std::uint64_t tick) {
        if(!staged_tick_||*staged_tick_!=tick){if(!stage_tick(tick))return false;}
        if(staged_dispatched_)return observe();
        staged_dispatched_=true;
        auto& vm=*sleep_.vm_;
        if(interactions_.resume_tick_&&tick>=*interactions_.resume_tick_)
            if(!interactions_.service_wait())return false;
        const bool runnable=vm.status()==VmStatus::running||vm.status()==VmStatus::budget_exhausted;
        // A lower-ID entity may notify us after stage_tick but before our actual
        // per-entity slot. Keep the pending service available for the audit.
        const bool notified_wait=thread_interrupt_&&vm.pending()&&
            ((vm.pending()->instruction.instruction.opcode==0&&sleep_.job_&&
              sleep_.job_->ticket==vm.pending()->ticket)||
             (vm.pending()->instruction.instruction.opcode==17&&interactions_.wait_&&
              interactions_.wait_->ticket==vm.pending()->ticket));
        if(!staged_dispatch_&&!runnable&&!notified_wait)return sleep_.service_due();
        if(!interactions_.validate()||!interactions_.evaluate_queue_priorities())return false;
        const auto first=std::find_if(interactions_.queue_.begin(),interactions_.queue_.end(),
            [](const auto& action){return (action.flags&4U)!=0;});
        const auto prefix=interactions_.suspended_active_.size()+(interactions_.active_?1U:0U);
        const bool candidate=first!=interactions_.queue_.end()&&
            static_cast<std::size_t>(std::distance(interactions_.queue_.begin(),first))>=prefix;
        if(candidate&&(staged_dispatch_||runnable||notified_wait)){
            if(staged_unknown_wait_&&(!immediate_service_||!vm.pending()||vm.stack().empty()||
               !immediate_service_->can_park(*vm.pending(),vm.stack().back()))){
                interactions_.diagnostic_="RunImmediately cannot suspend this pending service";
                return false;
            }
            const auto audit=[this](const VmPending& pending,const VmFrame& frame){
                if(frame.context.caller!=interactions_.actor_id_)return false;
                const auto opcode=pending.instruction.instruction.opcode;
                if(opcode==0)return sleep_.job_&&sleep_.job_->ticket==pending.ticket;
                if(opcode==17)return interactions_.wait_&&interactions_.wait_->ticket==pending.ticket;
                return immediate_service_&&immediate_service_->can_park(pending,frame);
            };
            if(!interactions_.try_run_immediate(audit,[this](std::uint64_t ticket){
                return park_immediate_parent(ticket);
            }))return false;
        }
        const auto reset=interactions_.source_reset_required();
        if(!reset)return false;
        if(*reset){
            if(!source_reset_handler_||!discard_for_source_reset()||!source_reset_handler_(vm,tick)){
                interactions_.diagnostic_="source dead-callee Reset did not complete";return false;
            }
            interactions_.run_generation_=vm.run_generation();
            if(sleep_.actor_)sleep_.actor_->run_generation=vm.run_generation();
            staged_dispatch_=true;staged_unknown_wait_=false;
        }
        if(thread_interrupt_&&vm.pending()){
            const auto ticket=vm.pending()->ticket;
            const auto opcode=vm.pending()->instruction.instruction.opcode;
            if(opcode==0&&sleep_.job_&&sleep_.job_->ticket==ticket){
                if(!sleep_.retry_notified_wait(ticket))return false;
            }else if(opcode==17&&interactions_.wait_&&interactions_.wait_->ticket==ticket){
                if(!interactions_.notify())return false;
            }
        }
        return interactions_.service_wait()&&sleep_.service_due();
    }
    bool advance(std::uint64_t tick) {
        return stage_tick(tick)&&before_run(tick);
    }
    bool observe() {fresh_=false;return interactions_.observe();}
    // Reschedule eligibility and set the source thread bit. Do not reexecute
    // or alter the parent before the actual slot has selected an immediate.
    bool notify() {
        fresh_=false;
        if((sleep_.active()||interactions_.waiting())&&
           (!sleep_.vm_||!sleep_.vm_->pending()||!latch_wait_clock(*sleep_.vm_->pending())))return false;
        thread_interrupt_=true;
        return true;
    }
    Sleep& sleep() {return sleep_;}
    InteractionQueue& interactions() {return interactions_;}
    bool thread_interrupted() const {return thread_interrupt_;}

private:
    std::function<bool(Vm&,std::uint64_t)> source_reset_handler_;
    bool park_immediate_parent(std::uint64_t ticket) {
        if(!sleep_.vm_||sleep_.vm_->immediate_continuations().empty())return false;
        const auto& pending=sleep_.vm_->immediate_continuations().back().parent_pending;
        if((pending?pending->ticket:0)!=ticket)return false;
        if(!schedule_idle_start_){
            if(pending&&(pending->instruction.instruction.opcode==0||pending->instruction.instruction.opcode==17)){
                if(!latch_wait_clock(*pending))return false;
            }else schedule_idle_start_=0;
        }
        if(!pending)return true;
        if(pending->instruction.instruction.opcode==0)return sleep_.park_for_immediate(ticket);
        if(pending->instruction.instruction.opcode==17)return interactions_.park_immediate_parent_wait(ticket);
        return immediate_service_&&immediate_service_->park(ticket);
    }
    bool latch_wait_clock(const VmPending& pending) {
        std::uint64_t start=0;
        if(pending.instruction.instruction.opcode==0){
            if(!sleep_.job_||sleep_.job_->ticket!=pending.ticket)return false;
            start=sleep_.job_->idle_start;
        }else if(pending.instruction.instruction.opcode==17){
            if(!interactions_.wait_||interactions_.wait_->ticket!=pending.ticket)return false;
            const auto& operands=pending.instruction.instruction.operands;
            const auto allow=operands[2]|(static_cast<unsigned>(operands[3])<<8);
            if(allow>1)return false;
            if(allow==0)start=interactions_.idle_start_;
        }else return false;
        if(!schedule_idle_start_)schedule_idle_start_=start;
        return true;
    }
    bool consume_thread_interrupt() {
        const bool value=thread_interrupt_;thread_interrupt_=false;return value;
    }
    Sleep sleep_;
    InteractionQueue interactions_;
    bool thread_interrupt_=false;
    std::optional<ImmediateService> immediate_service_;
    std::optional<std::uint64_t> schedule_idle_start_;
    std::optional<std::uint64_t> staged_tick_;
    bool staged_dispatch_=false,staged_dispatched_=false,staged_unknown_wait_=false;
    bool fresh_=true;
};

} // namespace odh
