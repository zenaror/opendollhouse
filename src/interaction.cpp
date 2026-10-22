#include "odh/interaction.hpp"
#include <limits>

namespace odh {
namespace {
struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}};
std::int16_t word(std::uint64_t v) {
    const auto bits=static_cast<std::uint16_t>(v);
    return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
PrimitiveOutcome unsupported(const std::string& text) {return {PrimitiveDisposition::unsupported,text};}
}
bool InteractionQueue::bind(Vm& vm) {
    if(executing_ || (vm_&&vm_!=&vm) || !capacity_)return false;
    const auto* actor=world_.find(actor_id_);
    if(!actor || actor->kind!=EntityKind::avatar)return false;
    if(vm_ && actor_generation_!=actor->generation)return false;
    if(vm_)return true;
    vm_=&vm;actor_generation_=actor->generation;run_generation_=vm.run_generation();return true;
}
bool InteractionQueue::validate() {
    if(!vm_){diagnostic_="interaction queue is not bound";return false;}
    const auto* actor=world_.find(actor_id_);
    if(!actor || actor->generation!=actor_generation_){diagnostic_="interaction actor identity stale";return false;}
    if(run_generation_!=vm_->run_generation()) {
        queue_.clear();active_.reset();wait_.reset();resume_tick_.reset();completion_.reset();
        interrupted_=false;idle_start_=0;run_generation_=vm_->run_generation();
        diagnostic_="VM restart invalidated queued interactions";
    }
    if(active_) {
        if(queue_.empty() || active_->uid!=queue_.front().uid){diagnostic_="active FIFO identity mismatch";return false;}
        const auto* target=world_.find(queue_.front().call.context.callee);
        if(!target || target->generation!=queue_.front().target_generation){diagnostic_="active interaction target identity stale";return false;}
    }
    return true;
}
bool InteractionQueue::enqueue(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter enqueue");
    if(!validate())return false;
    const auto* target=world_.find(context.callee);
    if(context.caller!=actor_id_ || !target || context.stack_object!=context.callee ||
       args.size()>4096 || queue_.size()>=capacity_ || sequence_==std::numeric_limits<std::uint64_t>::max()) {
        diagnostic_="interaction request identity/argument/capacity outside FIFO profile";return false;
    }
    if(vm_->status()==VmStatus::ready || vm_->status()==VmStatus::fault ||
       vm_->status()==VmStatus::returned_true || vm_->status()==VmStatus::returned_false) {
        diagnostic_="interaction requires a live Main run";return false;
    }
    queue_.push_back(Action{++sequence_,target->generation,VmCall{key,context,args,VmCallReturnMode::interaction_next_tick}});
    diagnostic_.clear();return true;
}
VmPrimitive InteractionQueue::primitive() {
    return [this](const auto& f,const auto& i,const auto&,const auto& d){return request(f,i,d);};
}
PrimitiveOutcome InteractionQueue::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(i.opcode!=17)return {};
    if(executing_ || !vm_ || dispatch.origin!=vm_)return unsupported("interaction controller identity/reentry rejected");
    Guard guard(executing_);
    if(!validate())return unsupported(diagnostic_);
    if(frame.context.caller!=actor_id_ || active_ || wait_)
        return unsupported("IdleForInput requires Main caller with no active interaction or idle wait");
    const auto index=word(i.operands[0]|(static_cast<std::uint64_t>(i.operands[1])<<8));
    const auto allow=static_cast<unsigned>(i.operands[2]|(static_cast<unsigned>(i.operands[3])<<8));
    if(index<0 || static_cast<std::size_t>(index)>=frame.arguments.size() || allow>1)
        return unsupported("IdleForInput argument/AllowPush outside explicit profile");
    const auto elapsed=idle_start_!=0&&idle_start_<clock_?clock_-idle_start_:1;
    const auto remaining=word(static_cast<std::uint16_t>(frame.arguments[static_cast<std::size_t>(index)])-elapsed);
    const VmFrameWrite output{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining};
    if(allow==1 && !queue_.empty()) {
        const auto& action=queue_.front();const auto* target=world_.find(action.call.context.callee);
        if(!target || target->generation!=action.target_generation)
            return unsupported("queued interaction target identity stale");
        active_=Active{action.uid,dispatch.ticket};idle_start_=0;
        return {PrimitiveDisposition::call,{},output,action.call};
    }
    if(!queue_.empty()) {
        const auto* actor=world_.find(actor_id_);const auto flags=actor->object_words.find(8);
        if(flags==actor->object_words.end())return unsupported("IdleForInput notification requires explicit Flags word8");
        if(!world_.declare_object_word(actor_id_,8,word(static_cast<std::uint16_t>(flags->second)|64U)))
            return unsupported("IdleForInput notification failed");
        idle_start_=0;return {PrimitiveDisposition::goto_true,{},output};
    }
    if(interrupted_ || remaining<0) {
        interrupted_=false;idle_start_=0;return {PrimitiveDisposition::goto_true,{},output};
    }
    const auto delay=allow==1?1:static_cast<std::uint64_t>(remaining)+1;
    if(delay>std::numeric_limits<std::uint32_t>::max()-clock_)
        return unsupported("IdleForInput deadline exceeds uint32 clock");
    wait_=Wait{dispatch.ticket,clock_+delay};idle_start_=clock_;
    return {PrimitiveDisposition::wait,{},output};
}
bool InteractionQueue::observe() {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter observe");
    Guard guard(executing_);
    if(!validate())return false;
    const auto& result=vm_->interaction_return();
    if(!result)return true;
    if(completion_ && completion_->ticket==result->ticket)return true;
    if(!active_ || queue_.empty() || active_->uid!=queue_.front().uid || active_->ticket!=result->ticket ||
       result->run_generation!=run_generation_) {
        diagnostic_="interaction completion identity mismatch";return false;
    }
    if(clock_==std::numeric_limits<std::uint32_t>::max()){diagnostic_="interaction resume exceeds uint32 clock";return false;}
    completion_=InteractionCompletion{active_->uid,result->ticket,clock_,result->result};
    queue_.pop_front();active_.reset();idle_start_=clock_;
    resume_tick_=clock_+1;diagnostic_.clear();return true;
}
bool InteractionQueue::tick(std::uint64_t now) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter tick");
    if(now>std::numeric_limits<std::uint32_t>::max() || (!last_tick_&&now>1) ||
       (last_tick_&&(now<=*last_tick_ || now-*last_tick_!=1)))return false;
    if(!observe())return false;
    Guard guard(executing_);
    last_tick_=now;clock_=now;
    if(resume_tick_ && now>=*resume_tick_) {
        if(!completion_ || !vm_->resume_interaction(completion_->ticket))return false;
        resume_tick_.reset();
    }
    if(wait_ && (now>=wait_->deadline || (!queue_.empty()) || interrupted_)) {
        const auto ticket=wait_->ticket;
        if(!vm_->retry_pending(ticket)) {diagnostic_="idle wait ticket stale";return false;}
        wait_.reset();
    }
    return true;
}
bool InteractionQueue::interrupt() {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter interrupt");
    if(!validate() || active_ || (vm_->status()!=VmStatus::running && vm_->status()!=VmStatus::budget_exhausted &&
       vm_->status()!=VmStatus::waiting && vm_->status()!=VmStatus::yielded))return false;
    interrupted_=true;return true;
}
bool InteractionQueue::notify() {
    if(executing_)throw std::logic_error("interaction notifications must not reenter controller");
    if(!validate()||!wait_||vm_->status()!=VmStatus::waiting||!vm_->pending()||
       vm_->pending()->ticket!=wait_->ticket)return false;
    Guard guard(executing_);
    const auto ticket=wait_->ticket;
    interrupted_=true;
    if(!vm_->retry_pending(ticket)) {
        interrupted_=false;diagnostic_="idle notification could not resume its pending ticket";return false;
    }
    wait_.reset();diagnostic_.clear();return true;
}
}
