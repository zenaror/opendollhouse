#include "odh/interaction.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace odh {
namespace {
struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}};
std::int16_t word(std::uint64_t v) {
    const auto bits=static_cast<std::uint16_t>(v);
    return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
PrimitiveOutcome unsupported(const std::string& text) {return {PrimitiveDisposition::unsupported,text};}
}
InteractionQueueSnapshot InteractionQueue::snapshot() const {
    if(executing_)throw std::logic_error("interaction callbacks must not capture controller state");
    if(vm_)(void)vm_->snapshot(); // Reject capture inside another VM primitive/trace callback too.
    return {actor_id_,capacity_,actor_generation_,run_generation_,sequence_,clock_,idle_start_,last_tick_,resume_tick_,
            queue_,active_,wait_,completion_,interrupted_,diagnostic_};
}
bool InteractionQueue::valid_snapshot(const InteractionQueueSnapshot& state) const {
    if(executing_)throw std::logic_error("interaction callbacks must not restore controller state");
    if(!vm_)return false;
    const auto vm=vm_->snapshot();
    constexpr auto max_clock=std::numeric_limits<std::uint32_t>::max();
    const auto* actor=world_.find(actor_id_);
    if(!fresh_||!actor||actor->kind!=EntityKind::avatar||state.actor_id!=actor_id_||
       state.actor_generation!=actor->generation||state.actor_generation!=actor_generation_||
       state.run_generation!=vm.run_generation||!state.capacity||state.capacity>capacity_||
       state.actions.size()>state.capacity||state.clock>max_clock||state.idle_start>state.clock||
       (state.last_tick?*state.last_tick!=state.clock:state.clock!=0))return false;
    std::set<std::uint64_t> action_uids;
    for(const auto& action:state.actions){
        const auto& call=action.call;
        const auto* target=world_.find(call.context.callee);
        if(!action.uid||!action_uids.insert(action.uid).second||action.uid>state.sequence||!target||
           action.target_generation!=target->generation||call.context.caller!=actor_id_||
           call.context.stack_object!=call.context.callee||call.arguments.size()>4096||
           call.key.scope!=routine_key(call.key.id).scope||call.return_mode!=VmCallReturnMode::interaction_next_tick)
            return false;
    }
    const auto valid_ticket=[&](std::uint64_t ticket){return ticket&&ticket<=vm.ticket_sequence;};
    bool action_frame=false;
    for(const auto& frame:vm.stack){
        if(frame.return_mode!=VmCallReturnMode::interaction_next_tick)continue;
        if(!state.active||state.actions.empty()||frame.invocation_ticket!=state.active->ticket)return false;
        const auto& call=state.actions.front().call;
        if(frame.key.scope!=call.key.scope||frame.key.id!=call.key.id||frame.context.caller!=call.context.caller||
           frame.context.callee!=call.context.callee||frame.context.code_owner!=call.context.code_owner||
           frame.arguments.size()<call.arguments.size())return false;
        action_frame=true;
    }
    if(state.active){
        if(state.actions.empty()||state.active->uid!=state.actions.front().uid||
           !valid_ticket(state.active->ticket)||state.resume_tick||
           (state.wait?!action_frame:state.idle_start!=0))return false;
        if(vm.interaction_return){
            if(action_frame||vm.interaction_return->ticket!=state.active->ticket||
               vm.interaction_return->code_owner!=state.actions.front().call.context.code_owner)return false;
        }else if(!action_frame&&vm.status!=VmStatus::fault)return false;
    }
    if(state.wait){
        if((state.resume_tick)||!valid_ticket(state.wait->ticket)||
           (state.active&&!action_frame)||
           state.wait->deadline<=state.clock||state.wait->deadline>max_clock||
           vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=state.wait->ticket||
           vm.pending->context.caller!=actor_id_||vm.pending->instruction.instruction.opcode!=17||vm.stack.empty())return false;
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto allow=static_cast<unsigned>(operands[2]|(static_cast<unsigned>(operands[3])<<8));
        if(index>=vm.stack.back().arguments.size()||index>32767||allow>1)return false;
        const auto remaining=vm.stack.back().arguments[index];
        if(remaining<0||state.wait->deadline!=state.idle_start+(allow==1?1:static_cast<std::uint64_t>(remaining)+1))return false;
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==17&&vm.pending->context.caller==actor_id_)
        return false;
    if(state.completion){
        const auto& completion=*state.completion;
        if(!completion.uid||completion.uid>state.sequence||!valid_ticket(completion.ticket)||completion.tick>state.clock||
           action_uids.count(completion.uid))return false;
    }
    if(state.resume_tick){
        if(!state.completion||state.active||state.wait||*state.resume_tick>max_clock||
           *state.resume_tick<=state.clock||*state.resume_tick!=state.completion->tick+1||!vm.interaction_return||
           vm.interaction_return->ticket!=state.completion->ticket||vm.interaction_return->result!=state.completion->result)
            return false;
    }
    if(vm.interaction_return&&!state.active&&!state.resume_tick)return false;
    return true;
}
void InteractionQueue::publish_snapshot(InteractionQueueSnapshot&& state) noexcept {
    capacity_=state.capacity;actor_generation_=state.actor_generation;run_generation_=state.run_generation;
    sequence_=state.sequence;clock_=state.clock;idle_start_=state.idle_start;last_tick_=state.last_tick;
    resume_tick_=state.resume_tick;queue_.swap(state.actions);active_=state.active;wait_=state.wait;
    completion_=state.completion;interrupted_=state.interrupted;diagnostic_.swap(state.diagnostic);fresh_=false;
}
bool InteractionQueue::restore_snapshot(const InteractionQueueSnapshot& state) {
    if(!valid_snapshot(state))return false;
    auto candidate=state;
    publish_snapshot(std::move(candidate));
    return true;
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
        // The host may bind its empty queue before starting the saved-world
        // Main routine. Adopting that first run is safe because no action or
        // event belongs to the previous generation.
        if(queue_.empty()&&!active_&&!wait_&&!completion_){
            run_generation_=vm_->run_generation();diagnostic_.clear();
        }else{
        queue_.clear();active_.reset();wait_.reset();resume_tick_.reset();completion_.reset();
        interrupted_=false;idle_start_=0;run_generation_=vm_->run_generation();
        diagnostic_="VM restart invalidated queued interactions";
        }
    }
    if(active_) {
        if(queue_.empty() || active_->uid!=queue_.front().uid){diagnostic_="active FIFO identity mismatch";return false;}
        const auto* target=world_.find(queue_.front().call.context.callee);
        if(!target || target->generation!=queue_.front().target_generation){diagnostic_="active interaction target identity stale";return false;}
    }
    return true;
}
bool InteractionQueue::enqueue(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
                               bool push_after_active) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter enqueue");
    fresh_=false;
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
    Action action{++sequence_,target->generation,VmCall{key,context,args,VmCallReturnMode::interaction_next_tick}};
    if(push_after_active&&active_&&!queue_.empty())queue_.insert(queue_.begin()+1,std::move(action));
    else queue_.push_back(std::move(action));
    diagnostic_.clear();return true;
}
VmPrimitive InteractionQueue::primitive() {
    return [this](const auto& f,const auto& i,const auto&,const auto& d){return request(f,i,d);};
}
PrimitiveOutcome InteractionQueue::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(i.opcode!=17)return {};
    if(executing_ || !vm_ || dispatch.origin!=vm_)return unsupported("interaction controller identity/reentry rejected");
    Guard guard(executing_);
    fresh_=false;
    if(!validate())return unsupported(diagnostic_);
    if(frame.context.caller!=actor_id_)
        return unsupported("IdleForInput Caller is not this avatar");
    const auto index=word(i.operands[0]|(static_cast<std::uint64_t>(i.operands[1])<<8));
    const auto allow=static_cast<unsigned>(i.operands[2]|(static_cast<unsigned>(i.operands[3])<<8));
    if(index<0 || static_cast<std::size_t>(index)>=frame.arguments.size() || allow>1)
        return unsupported("IdleForInput argument/AllowPush outside explicit profile");
    const auto& stack=dispatch.origin->stack();
    const bool action_tree=active_&&!queue_.empty()&&
        std::any_of(stack.begin(),stack.end(),[&](const VmFrame& action_frame){
            const auto& call=queue_.front().call;
            return action_frame.return_mode==VmCallReturnMode::interaction_next_tick&&
                action_frame.invocation_ticket==active_->ticket&&
                action_frame.key.scope==call.key.scope&&action_frame.key.id==call.key.id&&
                action_frame.context.caller==call.context.caller&&
                action_frame.context.callee==call.context.callee&&
                action_frame.context.stack_object==call.context.stack_object&&
                action_frame.context.code_owner==call.context.code_owner;
        });
    if(action_tree){
        if(wait_||resume_tick_)
            return unsupported("action-tree IdleForInput has no matching active interaction");
        // TS1's action-tree wrapper waits in the current interaction. AllowPush
        // can hand control back to the main thread and is a separate profile.
        if(allow!=0)return unsupported("action-tree IdleForInput AllowPush is outside this profile");
        const auto elapsed=idle_start_!=0&&idle_start_<clock_?clock_-idle_start_:1;
        const auto remaining=word(static_cast<std::uint16_t>(frame.arguments[static_cast<std::size_t>(index)])-elapsed);
        const VmFrameWrite output{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining};
        const bool thread_interrupted=thread_interrupt_&&thread_interrupt_();
        if(interrupted_||thread_interrupted||remaining<0){
            interrupted_=false;idle_start_=0;return {PrimitiveDisposition::goto_true,{},output};
        }
        const auto delay=static_cast<std::uint64_t>(remaining)+1;
        if(delay>std::numeric_limits<std::uint32_t>::max()-clock_)
            return unsupported("action-tree IdleForInput deadline exceeds uint32 clock");
        wait_=Wait{dispatch.ticket,clock_+delay};idle_start_=clock_;
        return {PrimitiveDisposition::wait,{},output};
    }
    if(frame.return_mode!=VmCallReturnMode::ordinary||active_||wait_)
        return unsupported("Main IdleForInput has an active interaction or idle wait");
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
    const bool thread_interrupted=thread_interrupt_&&thread_interrupt_();
    if(interrupted_||thread_interrupted||remaining<0) {
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
    fresh_=false;
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
    if(!world_.clear_motive_changes(actor_id_)){
        diagnostic_="interaction completion could not clear avatar motive changes";return false;
    }
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
    if(wait_ && (now>=wait_->deadline || (!active_&&!queue_.empty()) || interrupted_)) {
        const auto ticket=wait_->ticket;
        if(!vm_->retry_pending(ticket)) {diagnostic_="idle wait ticket stale";return false;}
        wait_.reset();
    }
    return true;
}
bool InteractionQueue::interrupt() {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter interrupt");
    fresh_=false;
    if(!validate() || active_ || (vm_->status()!=VmStatus::running && vm_->status()!=VmStatus::budget_exhausted &&
       vm_->status()!=VmStatus::waiting && vm_->status()!=VmStatus::yielded))return false;
    interrupted_=true;return true;
}
bool InteractionQueue::notify() {
    if(executing_)throw std::logic_error("interaction notifications must not reenter controller");
    fresh_=false;
    if(!validate()||!wait_||vm_->status()!=VmStatus::waiting||!vm_->pending()||
       vm_->pending()->ticket!=wait_->ticket)return false;
    Guard guard(executing_);
    const auto ticket=wait_->ticket;
    // EntityThreadControllers owns the shared SimAntics Thread.Interrupt bit
    // when this controller is bound through that host. Standalone tests retain
    // the local equivalent.
    if(!thread_interrupt_)interrupted_=true;
    if(!vm_->retry_pending(ticket)) {
        interrupted_=false;diagnostic_="idle notification could not resume its pending ticket";return false;
    }
    wait_.reset();diagnostic_.clear();return true;
}
}
