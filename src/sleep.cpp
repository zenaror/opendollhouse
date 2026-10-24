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
PrimitiveOutcome unsupported(const std::string& reason) {
    return {PrimitiveDisposition::unsupported,reason};
}
}
SleepSnapshot Sleep::snapshot() const {
    if(executing_)throw std::logic_error("Sleep callbacks must not capture controller state");
    if(vm_)(void)vm_->snapshot();
    return {actor_,job_,last_tick_,clock_,interrupted_,notify_resume_,diagnostic_};
}
bool Sleep::valid_snapshot(const SleepSnapshot& state) const {
    if(executing_)throw std::logic_error("Sleep callbacks must not restore controller state");
    if(!vm_)return false;
    const auto vm=vm_->snapshot();
    if(!fresh_||state.clock>max_clock||(state.last_tick?*state.last_tick!=state.clock:state.clock!=0))return false;
    if(state.actor){
        const auto* entity=world_.find(state.actor->id);
        if(!entity||state.actor->generation!=entity->generation||state.actor->run_generation!=vm.run_generation)return false;
    }else if(state.job||state.interrupted||state.notify_resume)return false;
    if(state.job){
        const auto& job=*state.job;
        if(!state.actor||state.notify_resume||!job.ticket||job.ticket>vm.ticket_sequence||job.idle_start>state.clock||
           job.deadline<=state.clock||job.deadline>max_clock||vm.status!=VmStatus::waiting||!vm.pending||
           vm.pending->ticket!=job.ticket||vm.pending->instruction.instruction.opcode!=0||
           vm.pending->context.caller!=state.actor->id||vm.stack.empty()||job.argument>=vm.stack.back().arguments.size()||
           job.argument>32767)return false;
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto remaining=vm.stack.back().arguments[job.argument];
        if(index!=job.argument||remaining<0||job.deadline!=job.idle_start+static_cast<std::uint64_t>(remaining)+1)return false;
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==0)return false;
    if(state.notify_resume&&(vm.stack.empty()||vm.stack.back().context.caller!=state.actor->id||
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
    if(executing_ || (vm_ && vm_!=&vm)) return false;
    vm_=&vm;return true;
}
bool Sleep::join_at(std::uint64_t current_tick) {
    if(executing_ || !vm_ || !fresh_ || actor_ || job_ || last_tick_ || current_tick>max_clock)
        return false;
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
void Sleep::random_cycle() {
    if(rng_cycle_) {diagnostic_.clear();rng_cycle_();}
    else diagnostic_="Sleep RNG cycle callback absent; reference RNG cycle not modeled";
}
PrimitiveOutcome Sleep::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(i.opcode!=0) return {};
    if(executing_) return unsupported("Sleep callbacks cannot reenter controller");
    if(!vm_ || dispatch.origin!=vm_) return unsupported("Sleep controller bound to another VM");
    Guard guard(executing_);
    fresh_=false;
    if(actor_ && actor_->run_generation!=vm_->run_generation()) {
        actor_.reset();job_.reset();interrupted_=false;notify_resume_=false;
    }
    if(job_) return unsupported("Sleep request already active");
    const auto* entity=world_.find(frame.context.caller);
    if(!entity) return unsupported("Sleep caller is not registered in World");
    if(actor_ && (actor_->id!=entity->id || actor_->generation!=entity->generation))
        return unsupported("Sleep caller identity changed within VM run");
    const auto index=signed_word(i.operands[0]|(static_cast<std::uint32_t>(i.operands[1])<<8));
    if(index<0 || static_cast<std::size_t>(index)>=frame.arguments.size())
        return unsupported("Sleep signed argument index outside frame");
    const auto remaining=decrement(frame.arguments[static_cast<std::size_t>(index)],1);
    const VmFrameWrite output{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining};
    const bool thread_interrupted=thread_interrupt_&&thread_interrupt_();
    if(!interrupted_&&!thread_interrupted&&remaining>=0&&
       static_cast<std::uint64_t>(remaining)+1>max_clock-clock_)
        return unsupported("Sleep deadline exceeds bounded uint32 scheduler clock");
    actor_=Actor{entity->id,entity->generation,vm_->run_generation()};
    diagnostic_.clear();
    if(notify_resume_) {
        notify_resume_=false;
        return {PrimitiveDisposition::goto_true,{},output};
    }
    if(interrupted_||thread_interrupted) {
        interrupted_=false;return {PrimitiveDisposition::goto_true,{},output};
    }
    if(remaining<0) {
        random_cycle();return {PrimitiveDisposition::goto_true,{},output};
    }
    job_=Job{dispatch.ticket,clock_,clock_+static_cast<std::uint16_t>(remaining)+1,
             static_cast<std::size_t>(index)};
    return {PrimitiveDisposition::wait,{},output};
}
bool Sleep::validate() {
    if(!vm_ || !actor_) return false;
    if(actor_->run_generation!=vm_->run_generation()) {
        job_.reset();actor_.reset();interrupted_=false;notify_resume_=false;
        diagnostic_="Sleep canceled by VM restart";return false;
    }
    const auto* entity=world_.find(actor_->id);
    if(!entity || entity->generation!=actor_->generation) {
        if(job_) vm_->complete(job_->ticket,false);
        job_.reset();interrupted_=false;notify_resume_=false;
        diagnostic_="Sleep canceled by removed or reused caller";return false;
    }
    if(job_ && (vm_->status()!=VmStatus::waiting || !vm_->pending() ||
                vm_->pending()->ticket!=job_->ticket)) {
        job_.reset();interrupted_=false;notify_resume_=false;
        diagnostic_="Sleep canceled by changed pending instruction";return false;
    }
    if(job_ && (vm_->stack().empty() || vm_->stack().back().context.caller!=actor_->id)) return false;
    return true;
}
bool Sleep::evaluate() {
    const auto elapsed=job_->idle_start!=0 && job_->idle_start<clock_ ? clock_-job_->idle_start : 1;
    const auto remaining=decrement(vm_->stack().back().arguments.at(job_->argument),elapsed);
    const auto ticket=job_->ticket;
    const VmFrameWrite output{VmFrameBank::arguments,job_->argument,remaining};
    if(interrupted_ || remaining<0) {
        const bool natural=!interrupted_;
        interrupted_=false;job_.reset();diagnostic_.clear();
        const auto accepted=vm_->complete(ticket,true,output);
        if(accepted && natural) random_cycle();
        return accepted;
    }
    const auto delay=static_cast<std::uint64_t>(remaining)+1;
    if(delay>max_clock-clock_) {
        diagnostic_="Sleep deadline exceeds bounded uint32 scheduler clock";return false;
    }
    if(!vm_->update_pending(ticket,output)) return false;
    job_->idle_start=clock_;job_->deadline=clock_+delay;
    return true;
}
bool Sleep::tick(std::uint64_t now) {
    if(executing_) throw std::logic_error("Sleep ticks must not reenter controller");
    Guard guard(executing_);
    fresh_=false;
    if(now>max_clock || (!last_tick_ && now>1) ||
       (last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1))) return false;
    last_tick_=now;clock_=now;
    if(actor_ && !validate()) return false;
    if(!job_) return true;
    if(now!=job_->deadline) return now<job_->deadline;
    return evaluate();
}
bool Sleep::interrupt() {
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
        const auto* entity=world_.find(vm_->stack().back().context.caller);
        if(!entity) return false;
        actor_=Actor{entity->id,entity->generation,vm_->run_generation()};
    }
    if(!validate()) return false;
    if(vm_->stack().empty() || (vm_->status()!=VmStatus::running &&
       vm_->status()!=VmStatus::budget_exhausted && vm_->status()!=VmStatus::waiting) ||
       vm_->stack().back().context.caller!=actor_->id) return false;
    interrupted_=true;
    return job_ ? evaluate() : true;
}
bool Sleep::notify() {
    if(executing_) throw std::logic_error("Sleep notifications must not reenter controller");
    Guard guard(executing_);
    fresh_=false;
    if(!vm_ || !job_ || !validate()) return false;
    const auto ticket=job_->ticket;
    const auto elapsed=job_->idle_start!=0 && job_->idle_start<clock_ ? clock_-job_->idle_start : 1;
    const auto current=vm_->stack().back().arguments.at(job_->argument);
    const auto remaining=decrement(current,elapsed);
    // Sleep::request normally subtracts one before branching. Add it back here
    // so the resumed instruction publishes exactly the elapsed value.
    const auto retry_value=signed_word(static_cast<std::uint16_t>(remaining)+1u);
    const VmFrameWrite output{VmFrameBank::arguments,job_->argument,retry_value};
    if(!vm_->retry_pending(ticket,output)) return false;
    job_.reset();interrupted_=false;notify_resume_=true;diagnostic_.clear();
    return true;
}
} // namespace odh
