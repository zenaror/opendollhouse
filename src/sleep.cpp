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
bool Sleep::bind(Vm& vm) {
    if(executing_ || (vm_ && vm_!=&vm)) return false;
    vm_=&vm;return true;
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
    if(actor_ && actor_->run_generation!=vm_->run_generation()) {
        actor_.reset();job_.reset();interrupted_=false;
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
    if(!interrupted_ && remaining>=0 && static_cast<std::uint64_t>(remaining)+1>max_clock-clock_)
        return unsupported("Sleep deadline exceeds bounded uint32 scheduler clock");
    actor_=Actor{entity->id,entity->generation,vm_->run_generation()};
    diagnostic_.clear();
    if(interrupted_) {
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
        job_.reset();actor_.reset();interrupted_=false;
        diagnostic_="Sleep canceled by VM restart";return false;
    }
    const auto* entity=world_.find(actor_->id);
    if(!entity || entity->generation!=actor_->generation) {
        if(job_) vm_->complete(job_->ticket,false);
        job_.reset();interrupted_=false;
        diagnostic_="Sleep canceled by removed or reused caller";return false;
    }
    if(job_ && (vm_->status()!=VmStatus::waiting || !vm_->pending() ||
                vm_->pending()->ticket!=job_->ticket)) {
        job_.reset();interrupted_=false;
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
} // namespace odh
