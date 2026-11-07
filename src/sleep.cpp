#include "odh/sleep.hpp"
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
constexpr auto max_clock=std::numeric_limits<std::uint32_t>::max();
struct Guard {
    bool& flag;
    explicit Guard(bool& value):flag(value){flag=true;}
    ~Guard(){flag=false;}
};
bool caller_matches(const VmContext& context,const SleepActorSnapshot& actor){
    return context.caller==actor.id&&(!context.entity_references_bound||context.caller_generation==actor.generation);
}
std::int16_t signed_word(std::uint64_t bits) {
    const auto low=static_cast<std::uint32_t>(bits&65535);
    return static_cast<std::int16_t>(low<32768 ? static_cast<std::int32_t>(low) :
                                                static_cast<std::int32_t>(low)-65536);
}
std::int16_t decrement(std::int16_t value,std::uint64_t elapsed) {
    // Both the elapsed conversion and compound assignment wrap to 16 bits in
    // the reference. Unsigned arithmetic avoids C++ signed overflow/conversion.
    return signed_word(static_cast<std::uint16_t>(value)-
                       static_cast<std::uint32_t>(static_cast<std::uint16_t>(signed_word(elapsed))));
}
std::uint64_t elapsed_since(std::uint64_t idle_start,std::uint64_t now) {
    return idle_start!=0&&idle_start<now?now-idle_start:1;
}
PrimitiveOutcome unsupported(const std::string& reason) {
    return {PrimitiveDisposition::unsupported,reason};
}
}
SleepSnapshot Sleep::snapshot() const {
    if(source_recovery_run_)throw std::logic_error("Sleep source recovery must finish before snapshot");
    if(executing_)throw std::logic_error("Sleep callbacks must not capture controller state");
    if(vm_)(void)vm_->snapshot();
    return {actor_,job_,last_tick_,clock_,interrupted_,notify_resume_,diagnostic_};
}
bool Sleep::valid_snapshot(const SleepSnapshot& state) const {
    if(executing_)throw std::logic_error("Sleep callbacks must not restore controller state");
    if(!vm_)return false;
    const auto vm=vm_->snapshot();
    if(source_recovery_run_||!fresh_||state.clock>max_clock||(state.last_tick?*state.last_tick!=state.clock:state.clock!=0))return false;
    if(state.actor){
        const auto* entity=world_.resolve_reference({state.actor->id,state.actor->generation});
        if(!entity||state.actor->generation!=entity->generation||state.actor->run_generation!=vm.run_generation)return false;
    }else if(state.job||state.interrupted||state.notify_resume)return false;
    if(state.job){
        const auto& job=*state.job;
        if(!state.actor||state.notify_resume||!job.ticket||job.ticket>vm.ticket_sequence||job.idle_start>state.clock||
           job.deadline<=state.clock||job.deadline>max_clock||vm.status!=VmStatus::waiting||!vm.pending||
           vm.pending->ticket!=job.ticket||vm.pending->instruction.instruction.opcode!=0||
           !caller_matches(vm.pending->context,*state.actor)||vm.stack.empty()||
           vm.pending->instruction.depth!=vm.stack.size()||!vm_context_equal(vm.pending->context,vm.stack.back().context)||job.argument>=vm.stack.back().arguments.size()||
           job.argument>32767)return false;
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto remaining=vm.stack.back().arguments[job.argument];
        if(index!=job.argument||remaining<0||job.deadline!=job.idle_start+static_cast<std::uint64_t>(remaining)+1)return false;
        if(job.next_evaluation && (*job.next_evaluation<=state.clock ||
            *job.next_evaluation>=job.deadline || *job.next_evaluation!=job.idle_start+1))return false;
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==0)return false;
    if(state.notify_resume&&(vm.stack.empty()||!caller_matches(vm.stack.back().context,*state.actor)||
       (vm.status!=VmStatus::running&&vm.status!=VmStatus::budget_exhausted)))return false;
    return true;
}
void Sleep::publish_snapshot(SleepSnapshot&& state) noexcept {
    actor_=state.actor;job_=state.job;last_tick_=state.last_tick;clock_=state.clock;
    interrupted_=state.interrupted;notify_resume_=state.notify_resume;diagnostic_.swap(state.diagnostic);fresh_=false;
}
bool Sleep::restore_snapshot(const SleepSnapshot& state) {
    if(!valid_snapshot(state))return false;
    auto candidate=state;
    publish_snapshot(std::move(candidate));
    return true;
}
bool Sleep::bind(Vm& vm) {
    if(executing_ || source_recovery_run_ || (vm_ && vm_!=&vm)) return false;
    vm_=&vm;return true;
}
bool Sleep::join_at(std::uint64_t current_tick,std::optional<std::int16_t> actor) {
    if(executing_ || !vm_ || vm_->dispatch_active() || !fresh_ || actor_ || job_ || last_tick_ || current_tick>max_clock)
        return false;
    std::optional<Actor> next_actor;
    if(actor){
        const auto state=vm_->snapshot();
        const auto* entity=world_.find(*actor);
        if(*actor<=0||!entity||entity->ui_ghost||state.status!=VmStatus::running||!state.run_generation||
           state.steps||state.stack.size()!=1||state.stack.front().pc||state.stack.front().context.caller!=*actor||
           state.pending||!state.suspended_pending.empty()||state.interaction_return||state.external_frame_return||
           state.stack.front().return_mode!=VmCallReturnMode::ordinary)return false;
        next_actor=Actor{*actor,entity->generation,state.run_generation};
    }
    actor_=next_actor;
    last_tick_=current_tick;
    clock_=current_tick;
    fresh_=false;
    diagnostic_.clear();
    return true;
}
VmPrimitive Sleep::primitive() {
    return [this](const auto& frame,const auto& instruction,const auto&,const VmDispatch& dispatch) {
        return request(frame,instruction,dispatch);
    };
}
bool Sleep::discard_for_source_recovery(WorldEntityReference expected){
    if(executing_||!vm_||vm_->dispatch_active()||!expected.generation||!world_.is_live_reference(expected))return false;
    const auto* body=world_.resolve_reference(expected);
    if(!body||body->dead||body->ui_ghost)return false;
    const auto run=vm_->run_generation();
    if(!run||(source_recovery_run_&&*source_recovery_run_!=run))return false;
    if(actor_){
        if(actor_->id!=expected.id||actor_->generation!=expected.generation||actor_->run_generation!=run)return false;
    }else{
        if(vm_->stack().empty())return false;
        const auto& root=vm_->stack().front();
        if(!root.context.entity_references_bound||vm_context_reference(root.context,VmEntityRole::caller)!=expected)return false;
        const auto* caller=world_.frame_entity(root,VmEntityRole::caller);
        if(!caller||caller->id!=expected.id||caller->generation!=expected.generation)return false;
        actor_=Actor{expected.id,expected.generation,run};
    }
    source_recovery_run_=run;
    fresh_=false;
    job_.reset();interrupted_=false;notify_resume_=false;diagnostic_.clear();
    return true;
}
bool Sleep::source_recovery_detached(WorldEntityReference expected,const Vm& thread) const{
    if(executing_||vm_!=&thread||thread.dispatch_active()||!source_recovery_run_||!actor_||job_||interrupted_||notify_resume_||
       actor_->id!=expected.id||actor_->generation!=expected.generation||actor_->run_generation!=*source_recovery_run_||
       thread.run_generation()<*source_recovery_run_||!world_.is_live_reference(expected))return false;
    const auto* body=world_.resolve_reference(expected);return body&&!body->dead&&!body->ui_ghost;
}
bool Sleep::rebase_after_source_recovery(WorldEntityReference expected){
    if(executing_||!vm_||vm_->dispatch_active()||!source_recovery_run_||!actor_||job_||interrupted_||notify_resume_||
       actor_->id!=expected.id||actor_->generation!=expected.generation||
       actor_->run_generation!=*source_recovery_run_||!world_.is_live_reference(expected))return false;
    const auto* body=world_.resolve_reference(expected);
    if(!body||body->dead||body->ui_ghost||vm_->run_generation()<=*source_recovery_run_||vm_->steps()||vm_->pending()||
       !vm_->suspended_pending().empty()||vm_->interaction_return()||vm_->external_frame_return()||
       !vm_->immediate_continuations().empty()||vm_->immediate_return())return false;
    const auto& stack=vm_->stack();
    if(stack.empty()){
        if(vm_->status()!=VmStatus::returned_false)return false;
    }else{
        if(stack.size()!=1||stack.front().pc||stack.front().return_mode!=VmCallReturnMode::ordinary||vm_->status()!=VmStatus::running)return false;
        if(!stack.front().context.entity_references_bound||vm_context_reference(stack.front().context,VmEntityRole::caller)!=expected)return false;
        const auto* caller=world_.frame_entity(stack.front(),VmEntityRole::caller);
        if(!caller||caller->id!=expected.id||caller->generation!=expected.generation)return false;
    }
    actor_->run_generation=vm_->run_generation();source_recovery_run_.reset();
    return true;
}
void Sleep::random_cycle() {
    if(rng_cycle_) {diagnostic_.clear();rng_cycle_();}
    else diagnostic_="Sleep RNG cycle callback absent; reference RNG cycle not modeled";
}
PrimitiveOutcome Sleep::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(source_recovery_run_)return unsupported("Sleep is suspended for source recovery");
    if(i.opcode!=0) return {};
    if(executing_) return unsupported("Sleep callbacks cannot reenter controller");
    if(!vm_ || dispatch.origin!=vm_) return unsupported("Sleep controller bound to another VM");
    Guard guard(executing_);
    fresh_=false;
    if(actor_ && actor_->run_generation!=vm_->run_generation()) {
        actor_.reset();job_.reset();interrupted_=false;notify_resume_=false;
    }
    if(job_) return unsupported("Sleep request already active");
    const auto* entity=world_.frame_entity(frame,VmEntityRole::caller);
    if(!entity) return unsupported("Sleep caller is not registered in World");
    if(actor_ && (actor_->id!=entity->id || actor_->generation!=entity->generation))
        return unsupported("Sleep caller identity changed within VM run");
    const auto index=signed_word(i.operands[0]|(static_cast<std::uint32_t>(i.operands[1])<<8));
    if(index<0 || static_cast<std::size_t>(index)>=frame.arguments.size())
        return unsupported("Sleep signed argument index outside frame");
    const auto shared_idle_start=schedule_idle_clock_read_?schedule_idle_clock_read_():std::nullopt;
    const auto elapsed=shared_idle_start?elapsed_since(*shared_idle_start,clock_):std::uint64_t{1};
    const auto remaining=decrement(frame.arguments[static_cast<std::size_t>(index)],elapsed);
    const VmFrameWrite output{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining};
    const bool thread_interrupted=thread_interrupt_&&thread_interrupt_();
    if(!interrupted_&&!thread_interrupted&&remaining>=0&&
       static_cast<std::uint64_t>(remaining)+1>max_clock-clock_)
        return unsupported("Sleep deadline exceeds bounded uint32 scheduler clock");
    actor_=Actor{entity->id,entity->generation,vm_->run_generation()};
    evaluated_this_tick_=true;
    diagnostic_.clear();
    if(notify_resume_) {
        notify_resume_=false;
        if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(0);
        return {PrimitiveDisposition::goto_true,{},output};
    }
    if(interrupted_||thread_interrupted) {
        interrupted_=false;
        if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(0);
        return {PrimitiveDisposition::goto_true,{},output};
    }
    if(remaining<0) {
        if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(0);
        const WorldEntityReference caller{entity->id,entity->generation};
        random_cycle();return {world_.reference_is_dead(caller)?
            PrimitiveDisposition::goto_true_next_tick:PrimitiveDisposition::goto_true,{},output};
    }
    job_=Job{dispatch.ticket,clock_,clock_+static_cast<std::uint16_t>(remaining)+1,
             static_cast<std::size_t>(index)};
    if(remaining>0 && run_every_frame_ && run_every_frame_())job_->next_evaluation=clock_+1;
    if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(clock_);
    return {PrimitiveDisposition::wait,{},output};
}
bool Sleep::validate() {
    if(!vm_ || !actor_) return false;
    if(actor_->run_generation!=vm_->run_generation()) {
        job_.reset();actor_.reset();interrupted_=false;notify_resume_=false;
        diagnostic_="Sleep canceled by VM restart";return false;
    }
    const auto* entity=world_.resolve_reference({actor_->id,actor_->generation});
    if(!entity || entity->generation!=actor_->generation) {
        diagnostic_="Sleep caller body reference is missing";return false;
    }
    if(job_ && (vm_->status()!=VmStatus::waiting || !vm_->pending() ||
                vm_->pending()->ticket!=job_->ticket)) {
        job_.reset();interrupted_=false;notify_resume_=false;
        diagnostic_="Sleep canceled by changed pending instruction";return false;
    }
    if(job_ && (vm_->stack().empty()||!caller_matches(vm_->stack().back().context,*actor_)||
       !caller_matches(vm_->pending()->context,*actor_)||
       vm_->pending()->instruction.depth!=vm_->stack().size()||
       !vm_context_equal(vm_->pending()->context,vm_->stack().back().context))){
        diagnostic_="Sleep pending Caller reference changed";return false;
    }
    return true;
}
bool Sleep::evaluate() {
    evaluated_this_tick_=true;
    const auto shared_idle_start=schedule_idle_clock_read_?schedule_idle_clock_read_():std::nullopt;
    const auto elapsed=shared_idle_start?elapsed_since(*shared_idle_start,clock_):elapsed_since(job_->idle_start,clock_);
    const auto remaining=decrement(vm_->stack().back().arguments.at(job_->argument),elapsed);
    const auto ticket=job_->ticket;
    const VmFrameWrite output{VmFrameBank::arguments,job_->argument,remaining};
    if(interrupted_ || remaining<0) {
        const bool natural=!interrupted_;
        interrupted_=false;job_.reset();diagnostic_.clear();
        const bool dead_caller=natural&&world_.reference_is_dead({actor_->id,actor_->generation});
        const auto accepted=dead_caller?vm_->complete_next_tick(ticket,true,output):vm_->complete(ticket,true,output);
        if(accepted) {
            if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(0);
            if(natural) random_cycle();
        }
        return accepted;
    }
    const auto delay=static_cast<std::uint64_t>(remaining)+1;
    if(delay>max_clock-clock_) {
        diagnostic_="Sleep deadline exceeds bounded uint32 scheduler clock";return false;
    }
    const auto next=delay>1 && run_every_frame_ && run_every_frame_()
        ? std::optional<std::uint64_t>(clock_+1) : std::nullopt;
    if(!vm_->update_pending(ticket,output)) return false;
    job_->idle_start=clock_;job_->deadline=clock_+delay;
    job_->next_evaluation=next;
    if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(clock_);
    return true;
}
bool Sleep::tick(std::uint64_t now,bool defer_evaluation) {
    if(source_recovery_run_)return false;
    if(executing_) throw std::logic_error("Sleep ticks must not reenter controller");
    Guard guard(executing_);
    evaluated_this_tick_=false;
    fresh_=false;
    if(now>max_clock || (!last_tick_ && now>1) ||
       (last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1))) return false;
    last_tick_=now;clock_=now;
    if(actor_ && !validate()) return false;
    if(!job_) return true;
    const auto next=job_->next_evaluation.value_or(job_->deadline);
    if(now!=next) return now<next;
    if(defer_evaluation)return true;
    return evaluate();
}
bool Sleep::service_due() {
    if(source_recovery_run_)return false;
    if(executing_)throw std::logic_error("Sleep service must not reenter controller");
    Guard guard(executing_);
    evaluated_this_tick_=false;fresh_=false;
    if(actor_&&!validate())return false;
    if(!job_)return true;
    if(!actor_)return false;
    const auto next=job_->next_evaluation.value_or(job_->deadline);
    if(clock_<next)return true;
    if(clock_>next)return false;
    return evaluate();
}
bool Sleep::dispatch_due(std::uint64_t now) const {
    if(!job_||now>max_clock||now<clock_)return false;
    const auto next=job_->next_evaluation.value_or(job_->deadline);
    return next<=max_clock&&now==next;
}
bool Sleep::park_for_immediate(std::uint64_t parent_ticket) {
    if(source_recovery_run_)return false;
    if(executing_||!vm_||!actor_||!job_||job_->ticket!=parent_ticket)return false;
    const auto& continuations=vm_->immediate_continuations();
    if(continuations.empty()||vm_->immediate_return()||
       (vm_->status()!=VmStatus::running&&vm_->status()!=VmStatus::budget_exhausted&&
        vm_->status()!=VmStatus::waiting))return false;
    const auto& continuation=continuations.back();
    if(continuation.parent_status!=VmStatus::waiting||!continuation.parent_pending||
       continuation.parent_pending->ticket!=parent_ticket||
       continuation.parent_pending->instruction.instruction.opcode!=0||
       continuation.parent_depth==0||continuation.parent_depth>=vm_->stack().size()||
       actor_->run_generation!=vm_->run_generation())return false;
    const auto& pending=*continuation.parent_pending;
    const auto& parent=vm_->stack()[continuation.parent_depth-1];
    const auto* entity=world_.resolve_reference({actor_->id,actor_->generation});
    const auto operand=pending.instruction.instruction.operands[0]|
        (static_cast<std::uint32_t>(pending.instruction.instruction.operands[1])<<8);
    const auto argument=signed_word(operand);
    if(!entity||entity->generation!=actor_->generation||!caller_matches(pending.context,*actor_)||
       !caller_matches(parent.context,*actor_)||pending.instruction.depth!=continuation.parent_depth||
       pending.instruction.key.scope!=parent.key.scope||pending.instruction.key.id!=parent.key.id||
       pending.instruction.pc!=parent.pc||pending.instruction.code_owner!=parent.context.code_owner||
       !vm_context_equal(pending.context,parent.context)||
       argument<0||static_cast<std::size_t>(argument)!=job_->argument||
       static_cast<std::size_t>(argument)>=parent.arguments.size())return false;
    job_.reset();return true;
}
bool Sleep::retry_immediate_parent(std::uint64_t parent_ticket) {
    if(source_recovery_run_)return false;
    if(executing_||!vm_||!actor_||job_||vm_->status()!=VmStatus::waiting||!vm_->pending()||
       vm_->pending()->ticket!=parent_ticket||vm_->pending()->instruction.instruction.opcode!=0||
       actor_->run_generation!=vm_->run_generation()||vm_->stack().empty())return false;
    const auto* entity=world_.resolve_reference({actor_->id,actor_->generation});
    const auto& frame=vm_->stack().back();const auto& pending=*vm_->pending();
    const auto operand=pending.instruction.instruction.operands[0]|
        (static_cast<std::uint32_t>(pending.instruction.instruction.operands[1])<<8);
    const auto argument=signed_word(operand);
    if(!entity||entity->generation!=actor_->generation||!caller_matches(frame.context,*actor_)||
       !caller_matches(pending.context,*actor_)||pending.instruction.depth!=vm_->stack().size()||
       pending.instruction.key.scope!=frame.key.scope||pending.instruction.key.id!=frame.key.id||
       pending.instruction.pc!=frame.pc||pending.instruction.code_owner!=frame.context.code_owner||
       !vm_context_equal(pending.context,frame.context)||
       argument<0||static_cast<std::size_t>(argument)>=frame.arguments.size())return false;
    return vm_->retry_pending(parent_ticket);
}
bool Sleep::retry_notified_wait(std::uint64_t ticket) {
    if(source_recovery_run_)return false;
    if(executing_||!vm_||!ticket||!actor_||!job_||job_->ticket!=ticket||
       actor_->run_generation!=vm_->run_generation()||vm_->status()!=VmStatus::waiting||
       !vm_->pending()||vm_->pending()->ticket!=ticket||
       vm_->pending()->instruction.instruction.opcode!=0||vm_->stack().empty())return false;
    const auto* entity=world_.resolve_reference({actor_->id,actor_->generation});
    const auto& frame=vm_->stack().back();const auto& pending=*vm_->pending();
    const auto operand=pending.instruction.instruction.operands[0]|
        (static_cast<std::uint32_t>(pending.instruction.instruction.operands[1])<<8);
    const auto argument=signed_word(operand);
    if(!entity||entity->generation!=actor_->generation||!caller_matches(frame.context,*actor_)||
       !caller_matches(pending.context,*actor_)||pending.instruction.depth!=vm_->stack().size()||
       pending.instruction.key.scope!=frame.key.scope||pending.instruction.key.id!=frame.key.id||
       pending.instruction.pc!=frame.pc||pending.instruction.code_owner!=frame.context.code_owner||
       !vm_context_equal(pending.context,frame.context)||argument<0||
       static_cast<std::size_t>(argument)!=job_->argument||
       static_cast<std::size_t>(argument)>=frame.arguments.size()||frame.arguments[static_cast<std::size_t>(argument)]<0)
        return false;
    if(!vm_->retry_pending(ticket))return false;
    job_.reset();fresh_=false;return true;
}
bool Sleep::interrupt() {
    if(source_recovery_run_)return false;
    if(executing_) throw std::logic_error("Sleep interrupts must not reenter controller");
    Guard guard(executing_);
    fresh_=false;
    if(!vm_) return false;
    if(actor_ && actor_->run_generation!=vm_->run_generation()) {
        job_.reset();actor_.reset();interrupted_=false;
    }
    if(!actor_) {
        if(vm_->stack().empty() || (vm_->status()!=VmStatus::running &&
            vm_->status()!=VmStatus::budget_exhausted && vm_->status()!=VmStatus::waiting)) return false;
        const auto* entity=world_.frame_entity(vm_->stack().back(),VmEntityRole::caller);
        if(!entity) return false;
        actor_=Actor{entity->id,entity->generation,vm_->run_generation()};
    }
    if(!validate()) return false;
    if(vm_->stack().empty() || (vm_->status()!=VmStatus::running &&
       vm_->status()!=VmStatus::budget_exhausted && vm_->status()!=VmStatus::waiting) ||
       !caller_matches(vm_->stack().back().context,*actor_)) return false;
    interrupted_=true;
    return job_ ? evaluate() : true;
}
bool Sleep::notify() {
    if(source_recovery_run_)return false;
    if(executing_) throw std::logic_error("Sleep notifications must not reenter controller");
    Guard guard(executing_);
    fresh_=false;
    if(!vm_ || !job_ || !validate()) return false;
    const auto ticket=job_->ticket;
    const auto shared_idle_start=schedule_idle_clock_read_?schedule_idle_clock_read_():std::nullopt;
    const auto elapsed=shared_idle_start?elapsed_since(*shared_idle_start,clock_):elapsed_since(job_->idle_start,clock_);
    const auto current=vm_->stack().back().arguments.at(job_->argument);
    const auto remaining=decrement(current,elapsed);
    // Sleep::request normally subtracts one before branching. Add it back here
    // so the resumed instruction publishes exactly the elapsed value.
    const auto retry_value=signed_word(static_cast<std::uint16_t>(remaining)+1u);
    const VmFrameWrite output{VmFrameBank::arguments,job_->argument,retry_value};
    if(!vm_->retry_pending(ticket,output)) return false;
    job_.reset();interrupted_=false;notify_resume_=true;diagnostic_.clear();
    if(shared_idle_start&&schedule_idle_clock_write_)schedule_idle_clock_write_(0);
    return true;
}
} // namespace odh
