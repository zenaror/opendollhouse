#include "odh/interaction.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace odh {
namespace {
struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}};
constexpr std::uint32_t ttab_run_immediately=1U<<2;
constexpr std::uint32_t tso_push_tail=1U<<30;
constexpr std::uint32_t tso_push_head=1U<<29;
constexpr std::uint32_t tso_skip_permissions=1U<<28;
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
            queue_,active_,wait_,completion_,interrupted_,diagnostic_,profile_,next_queue_uid_};
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
       state.profile>InteractionQueueProfile::ts1_priority||
       (state.last_tick?*state.last_tick!=state.clock:state.clock!=0))return false;
    std::set<std::uint64_t> action_uids;
    for(const auto& action:state.actions){
        const auto& call=action.call;
        const auto* target=world_.find(call.context.callee);
        if(!action.uid||!action_uids.insert(action.uid).second||action.uid>state.sequence||!target||
           action.target_generation!=target->generation||call.context.caller!=actor_id_||
           call.context.stack_object!=call.context.callee||call.arguments.size()>4096||
           call.key.scope!=routine_key(call.key.id).scope||call.return_mode!=VmCallReturnMode::interaction_next_tick||
           action.mode>InteractionQueueMode::idle ||
           (action.check && (action.check->scope!=routine_key(action.check->id).scope ||
                            !vm_->has_routine(*action.check,call.context.code_owner))))
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
    completion_=state.completion;interrupted_=state.interrupted;diagnostic_.swap(state.diagnostic);
    profile_=state.profile;next_queue_uid_=state.next_queue_uid;fresh_=false;
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
bool InteractionQueue::evaluate_queue_priorities() {
    if(!active_||queue_.empty())return true;
    const auto* actor=world_.find(actor_id_);
    if(!actor||actor->kind!=EntityKind::avatar){diagnostic_="interaction priority actor identity stale";return false;}
    if(actor->person_data.size()<=33)return true;

    auto candidate=queue_;
    const auto& active=candidate.front();
    std::int16_t current_priority=actor->person_data[33];
    if(current_priority==2)--current_priority; // TS1 autonomous actions yield to other autonomous actions.
    bool notify_idle=active.priority!=0&&current_priority==0;
    bool interaction_canceled=false;
    for(auto it=std::next(candidate.begin());it!=candidate.end();){
        const auto* target=world_.find(it->call.context.callee);
        if(!target||target->generation!=it->target_generation){
            it=candidate.erase(it);continue;
        }
        if(it->priority>current_priority){
            notify_idle=true;
            interaction_canceled=true;
        }
        ++it;
    }
    candidate.front().notify_idle=notify_idle;
    if(interaction_canceled){
        const auto flags=actor->object_words.find(8);
        if(flags==actor->object_words.end()){
            diagnostic_="queue priority change requires avatar ObjectData[8] flags";return false;
        }
        const auto updated=static_cast<std::uint16_t>(flags->second)|0x0080U;
        if(!world_.declare_object_word(actor_id_,8,word(updated))){
            diagnostic_="queue priority change could not set InteractionCanceled";return false;
        }
    }
    queue_.swap(candidate);
    return true;
}
bool InteractionQueue::enqueue(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
                               bool push_after_active) {
    return enqueue_impl(key,context,args,1,InteractionQueueMode::normal,0,push_after_active,false,false,{});
}
bool InteractionQueue::enqueue_ts1(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,bool push_head,bool push_tail,
        std::optional<RoutineKey> check) {
    flags|=tso_skip_permissions; // TS1 PushInteraction always sets TTABFlags.FSOSkipPermissions.
    return enqueue_impl(key,context,args,priority,mode,flags,push_head,push_tail,true,check);
}
bool InteractionQueue::enqueue_impl(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,bool push_head,bool push_tail,
        bool ts1_request,std::optional<RoutineKey> check) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter enqueue");
    fresh_=false;
    if(!validate())return false;
    const auto* target=world_.find(context.callee);
    if(context.caller!=actor_id_ || !target || context.stack_object!=context.callee ||
       args.size()>4096 || queue_.size()>=capacity_ || sequence_==std::numeric_limits<std::uint64_t>::max() ||
       mode>InteractionQueueMode::idle) {
        diagnostic_="interaction request identity/argument/capacity outside supported queue profile";return false;
    }
    if(vm_->status()==VmStatus::ready || vm_->status()==VmStatus::fault ||
       vm_->status()==VmStatus::returned_true || vm_->status()==VmStatus::returned_false) {
        diagnostic_="interaction requires a live Main run";return false;
    }
    if((flags&ttab_run_immediately)!=0){
        diagnostic_="RunImmediately requires tick-boundary queue injection, which is not implemented";return false;
    }
    if(check && (check->scope!=routine_key(check->id).scope ||
                 !vm_->has_routine(*check,context.code_owner))){
        diagnostic_="interaction check requires its exact installed code owner";return false;
    }
    if(push_head)flags|=tso_push_head;
    if(push_tail)flags|=tso_push_tail;
    const auto new_sequence=sequence_+1;
    Action action{new_sequence,target->generation,VmCall{key,context,args,VmCallReturnMode::interaction_next_tick},
        next_queue_uid_,priority,mode,flags,false,check};
    const bool migrating_legacy=ts1_request&&profile_==InteractionQueueProfile::legacy_fifo;
    const bool use_ts1_profile=profile_==InteractionQueueProfile::ts1_priority||ts1_request;
    const auto protected_prefix=active_?std::size_t{1}:std::size_t{0};
    auto position=queue_.end();
    if(!use_ts1_profile){
        position=push_head?queue_.begin()+static_cast<std::ptrdiff_t>(protected_prefix):queue_.end();
    }else if((flags&tso_push_head)!=0||push_head){
        position=queue_.begin()+static_cast<std::ptrdiff_t>(protected_prefix);
    }else if((flags&tso_push_tail)!=0&&mode!=InteractionQueueMode::parent_exit){
        for(auto it=queue_.begin()+static_cast<std::ptrdiff_t>(protected_prefix);it!=queue_.end();++it)
            if(it->priority<priority){position=it;break;}
    }else{
        bool hit_parent_end=mode!=InteractionQueueMode::parent_idle;
        bool found_pivot=false;
        for(auto it=queue_.end();it!=queue_.begin()+static_cast<std::ptrdiff_t>(protected_prefix);){
            --it;
            if(hit_parent_end&&(priority<=it->priority||it->mode==InteractionQueueMode::parent_exit)){
                position=std::next(it);found_pivot=true;break;
            }
            if(it->mode==InteractionQueueMode::parent_exit)hit_parent_end=true;
        }
        if(!found_pivot&&protected_prefix<queue_.size())
            position=queue_.begin()+static_cast<std::ptrdiff_t>(protected_prefix);
    }
    queue_.insert(position,std::move(action));
    if(!evaluate_queue_priorities()){
        const auto inserted=std::find_if(queue_.begin(),queue_.end(),[&](const Action& queued){
            return queued.uid==new_sequence;
        });
        if(inserted!=queue_.end())queue_.erase(inserted);
        return false;
    }
    sequence_=new_sequence;
    if(migrating_legacy){
        std::uint16_t public_uid=0;
        for(auto& queued:queue_)queued.queue_uid=public_uid++;
        next_queue_uid_=public_uid;
        profile_=InteractionQueueProfile::ts1_priority;
    }else next_queue_uid_=static_cast<std::uint16_t>(next_queue_uid_+1U);
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
        if(!queue_.empty()&&queue_.front().notify_idle){
            const auto* actor=world_.find(actor_id_);
            if(!actor||!actor->object_words.count(8)||
               !world_.declare_object_word(actor_id_,8,word(static_cast<std::uint16_t>(actor->object_words.at(8))|64U)))
                return unsupported("IdleForInput could not publish active-action notification");
            idle_start_=0;return {PrimitiveDisposition::goto_true,{},output};
        }
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
    std::optional<std::int32_t> priority_threshold;
    if(profile_==InteractionQueueProfile::ts1_priority){
        const auto* actor=world_.find(actor_id_);
        if(actor&&actor->person_data.size()>33){
            priority_threshold=actor->person_data[33];
            if(*priority_threshold<=2)--*priority_threshold;
        }
    }
    while(allow==1 && !queue_.empty()) {
        // The check can mutate World and thread registers even when it returns
        // false. Keep a value copy and resolve identities again after it runs.
        const auto candidate=queue_.front();
        if(priority_threshold&&candidate.priority<=*priority_threshold)break;
        if(profile_==InteractionQueueProfile::ts1_priority){
            const auto* actor=world_.find(actor_id_);
            if(actor&&actor->object_words.count(8)){
                auto flags=static_cast<std::uint16_t>(actor->object_words.at(8));
                if(candidate.notify_idle){
                    flags|=0x0080U;
                    world_.declare_object_word(actor_id_,8,word(flags));
                }
                if(candidate.mode==InteractionQueueMode::parent_idle&&(flags&0x0080U)){
                    queue_.pop_front();continue;
                }
            }
        }
        const auto* candidate_target=world_.find(candidate.call.context.callee);
        if(!candidate_target || candidate_target->generation!=candidate.target_generation)
            return unsupported("queued interaction target identity stale");
        if(candidate.check){
            const auto result=vm_->evaluate_check(*candidate.check,candidate.call.context,
                {0,0,0,0},10000,check_primitive_);
            if(!result.started || (result.status!=VmStatus::returned_true &&
                                   result.status!=VmStatus::returned_false))
                return unsupported("interaction check did not return synchronously: "+result.diagnostic);
            if(!validate())return unsupported(diagnostic_);
            if(result.status==VmStatus::returned_false){queue_.pop_front();continue;}
        }
        const auto& action=queue_.front();const auto* target=world_.find(action.call.context.callee);
        if(!target || target->generation!=action.target_generation)
            return unsupported("queued interaction target identity stale");
        if(!vm_->can_accept_call(action.call))
            return unsupported("interaction VM cannot accept the queued action frame");
        const auto uid=action.uid;
        const auto priority=action.priority;
        const auto ticket=dispatch.ticket;
        PrimitiveOutcome outcome{PrimitiveDisposition::call,{},output,action.call};
        outcome.call_commit=[this,uid,priority,ticket]{
            if(const auto* actor=world_.find(actor_id_);actor&&actor->person_data.size()>33&&
               !world_.set_person_data(actor_id_,33,priority))return false;
            if(const auto* actor=world_.find(actor_id_);actor&&actor->object_words.count(8))
                world_.declare_object_word(actor_id_,8,
                    word(static_cast<std::uint16_t>(actor->object_words.at(8))&~0x0080U));
            active_=Active{uid,ticket};idle_start_=0;return true;
        };
        return outcome;
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
    if(const auto* actor=world_.find(actor_id_);actor&&actor->person_data.size()>33&&
       !world_.set_person_data(actor_id_,33,0)){
        diagnostic_="interaction completion could not restore PersonData[33] priority";return false;
    }
    if(queue_.front().mode!=InteractionQueueMode::parent_idle){
        if(const auto* actor=world_.find(actor_id_);actor&&actor->object_words.count(8))
            world_.declare_object_word(actor_id_,8,
                word(static_cast<std::uint16_t>(actor->object_words.at(8))&~0x0080U));
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
    if(!evaluate_queue_priorities())return false;
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
const Interaction* ts1_interaction_by_index(const Ttab& table,std::uint8_t index) {
    if(table.status!=TtabStatus::decoded)return nullptr;
    const Interaction* selected=nullptr;
    for(const auto& row:table.interactions)if(row.label_index==index){
        if(selected)throw FormatError("TTAB interaction index is ambiguous");
        selected=&row;
    }
    return selected;
}
VmPrimitive ts1_push_interaction_primitive(const World& world,Ts1PushInteractionLookup lookup,
        Ts1PushInteractionThread resolve_thread) {
    return [&world,lookup=std::move(lookup),resolve_thread=std::move(resolve_thread)](
        const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome {
        if(instruction.opcode!=13)return {};
        const auto& bytes=instruction.operands;
        constexpr std::uint8_t supported_flags=0x87;
        if(bytes[2]>6||(bytes[3]&static_cast<std::uint8_t>(~supported_flags))!=0)
            return unsupported("PushInteraction priority or operand flags are unsupported");
        const auto& bank=(bytes[3]&2)!=0?frame.locals:frame.arguments;
        if(bytes[1]>=bank.size()||((bytes[3]&1)!=0&&bytes[4]>=frame.locals.size()))
            return unsupported("PushInteraction object/icon index outside its BHAV frame");
        InteractionQueueMode mode=InteractionQueueMode::normal;
        std::int16_t priority=1;
        bool action_tree=false;
        if(dispatch.origin){
            const auto& stack=dispatch.origin->stack();
            action_tree=std::any_of(stack.begin(),stack.end(),[](const VmFrame& item){
                return item.return_mode==VmCallReturnMode::interaction_next_tick;
            });
        }
        switch(bytes[2]){
            case 0:{
                if(action_tree){
                    const auto* caller=world.find(frame.context.caller);
                    if(caller&&caller->kind==EntityKind::avatar&&caller->person_data.size()>33)
                        priority=std::max<std::int16_t>(1,caller->person_data[33]);
                }
                break;
            }
            case 1:priority=100;break;
            case 2:priority=2;break;
            case 3:priority=50;break;
            case 4:priority=40;mode=InteractionQueueMode::parent_idle;break;
            case 5:priority=30;mode=InteractionQueueMode::parent_exit;break;
            case 6:priority=0;mode=InteractionQueueMode::idle;break;
            default:return unsupported("PushInteraction priority code is invalid");
        }
        Ts1PushInteractionRequest request{frame.context.stack_object,bank[bytes[1]],bytes[0],
            (bytes[3]&4)!=0,(bytes[3]&128)!=0,bytes[2],priority,mode,{}};
        if((bytes[3]&1)!=0)request.custom_icon=frame.locals[bytes[4]];
        if(!world.find(request.source))return {PrimitiveDisposition::goto_false,{}};
        const auto* actor=world.find(request.actor);
        if(!actor||actor->kind!=EntityKind::avatar)
            return unsupported("PushInteraction StackObject has no supported resident interaction thread");
        try {
            if(!lookup)return unsupported("PushInteraction action resource resolver is unavailable");
            const auto action=lookup(request);
            if(!action)return {PrimitiveDisposition::goto_false,{}};
            const auto target=resolve_thread?resolve_thread(request.actor):Ts1PushInteractionTarget{};
            if(!target.vm||!target.queue)
                return unsupported("PushInteraction resident interaction thread is unavailable");
            if(!target.vm->has_routine(action->key,action->owner))
                return unsupported("PushInteraction recipient lacks the exact installed action code owner");
            const VmContext context{request.actor,request.source,request.source,action->owner};
            if(!target.queue->enqueue_ts1(action->key,context,{},request.priority,request.mode,
                    action->flags,request.push_head,request.push_tail,action->check))
                return unsupported("PushInteraction resident queue rejected action: "+target.queue->diagnostic());
            return {PrimitiveDisposition::goto_true,{}};
        } catch(const std::exception& error) {
            return unsupported(std::string("PushInteraction action could not be resolved: ")+error.what());
        }
    };
}

}
