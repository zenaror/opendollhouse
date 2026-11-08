#include "odh/interaction.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace odh {
std::optional<std::int16_t> interaction_advertisement(const Ttab* table,std::int32_t number,
    std::uint8_t type,std::uint16_t index){
    if(type>2)return {};
    if(!table)return 0;
    if(table->status!=TtabStatus::decoded&&table->status!=TtabStatus::empty)return {};
    const Interaction* selected=nullptr;std::set<std::uint32_t> keys;
    for(const auto& row:table->interactions){
        if(!keys.insert(row.label_index).second)return {};
        if(row.label_index==static_cast<std::uint32_t>(number))selected=&row;
    }
    if(!selected||index>=selected->motives.size())return 0;
    const auto& entry=selected->motives[index];
    if(type==0)return entry.minimum;
    if(type==1)return entry.delta;
    const auto value=entry.personality_modifier;
    return static_cast<std::int16_t>(value<=32767?std::int32_t(value):std::int32_t(value)-65536);
}

namespace {
// FreeSO7e76 EvaluateCheck92–125/CheckTS1Action931–941 rejects an authored
// ERROR just like a false check. Unknown host/implementation faults stay fatal.
bool source_check_false(const VmSubroutineResult& result){
    return result.started&&(result.status==VmStatus::returned_false||
        (result.status==VmStatus::fault&&result.source_error));
}
bool source_check_completed(const VmSubroutineResult& result){
    return result.started&&(result.status==VmStatus::returned_true||source_check_false(result));
}
struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}};
// Failure-only bounded numeric context. Do not alter target admission or queues.
std::string queued_target_diagnostic(const char* reason,const char* phase,
    std::int16_t actor,std::uint64_t actor_generation,
    const InteractionActionSnapshot& candidate,const Entity* actual,
    const std::deque<InteractionActionSnapshot>& queue,std::size_t protected_count,
    const std::optional<InteractionActiveSnapshot>& active,std::size_t suspended_count) {
    return std::string{reason}+" phase="+phase+
        " actor="+std::to_string(actor)+" actor_gen="+std::to_string(actor_generation)+
        " candidate_uid="+std::to_string(candidate.uid)+
        " target="+std::to_string(candidate.call.context.callee)+
        " expected_gen="+std::to_string(candidate.target_generation)+
        " actual_gen="+(actual?std::to_string(actual->generation):"missing")+
        " actual_dead="+(actual?(actual->dead?"1":"0"):"missing")+
        " refs_bound="+std::to_string(candidate.call.context.entity_references_bound)+
        " bound_callee_gen="+std::to_string(candidate.call.context.callee_generation)+
        " owner="+std::to_string(candidate.call.context.code_owner)+
        " routine="+std::to_string(candidate.call.key.id)+
        " priority="+std::to_string(candidate.priority)+
        " mode="+std::to_string(static_cast<unsigned>(candidate.mode))+
        " queue_size="+std::to_string(queue.size())+
        " protected_count="+std::to_string(protected_count)+
        " next_uid="+(queue.size()>protected_count?std::to_string(queue[protected_count].uid):"none")+
        " active_uid="+(active?std::to_string(active->uid):"none")+
        " active_ticket="+(active?std::to_string(active->ticket):"none")+
        " suspended_count="+std::to_string(suspended_count);
}

constexpr std::uint32_t ttab_run_immediately=1U<<2;
constexpr std::uint32_t ttab_must_run=1U<<10;
constexpr std::uint32_t ttab_direct_control=1U<<27;
constexpr std::uint32_t tso_push_tail=1U<<30;
constexpr std::uint32_t tso_push_head=1U<<29;
constexpr std::uint32_t tso_skip_permissions=1U<<28;
std::vector<std::int16_t> eligibility_arguments(const InteractionActionSnapshot& action){
    return {static_cast<std::int16_t>(action.check_context==InteractionCheckContext::autonomous?1:0),0,0,0};
}
std::int16_t word(std::uint64_t v) {
    const auto bits=static_cast<std::uint16_t>(v);
    return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
PrimitiveOutcome unsupported(const std::string& text) {return {PrimitiveDisposition::unsupported,text};}
bool same_context(const VmContext& a,const VmContext& b) {
    return vm_context_equal(a,b);
}
bool same_action_owner_context(const VmContext& a,const VmContext& b){
    // StackObject is mutable inside the action. Caller/Callee keep their body
    // references throughout the accepted queued invocation.
    return a.caller==b.caller&&a.callee==b.callee&&a.code_owner==b.code_owner&&
        a.entity_references_bound==b.entity_references_bound&&
        a.caller_generation==b.caller_generation&&a.callee_generation==b.callee_generation;
}
bool same_icon(const std::optional<InteractionIconOwnerSnapshot>& a,const std::optional<InteractionIconOwnerSnapshot>& b) {
    return bool(a)==bool(b)&&(!a||(a->id==b->id&&a->generation==b->generation));
}
bool same_frame(const VmFrame& a,const VmFrame& b) {
    return a.key.id==b.key.id&&a.key.scope==b.key.scope&&a.pc==b.pc&&same_context(a.context,b.context)&&
        a.arguments==b.arguments&&a.locals==b.locals&&a.return_mode==b.return_mode&&
        a.invocation_ticket==b.invocation_ticket&&a.exact_arguments==b.exact_arguments&&a.action_tree==b.action_tree;
}
}
InteractionPermission ts1_interaction_permission(const Entity* actor,std::uint32_t flags) {
    if(!actor||actor->kind!=EntityKind::avatar)return InteractionPermission::unsupported;
    if(flags&tso_skip_permissions)return InteractionPermission::allowed;
    if(actor->person_data.size()<=65)return InteractionPermission::unsupported;
    const auto& data=actor->person_data;
    const bool dog=(static_cast<std::uint16_t>(data[65])&8U)!=0;
    const bool cat=(static_cast<std::uint16_t>(data[65])&16U)!=0;
    const bool pet=dog||cat;
    constexpr auto cats=1U<<9, dogs=1U<<10;
    if(flags&(cats|dogs)){
        // The pinned TS1 path does not reject humans in this branch.
        if((cat&&!(flags&cats))||(dog&&!(flags&dogs)))return InteractionPermission::denied;
    }else if(pet)return InteractionPermission::denied;
    if(flags&(1U<<7))return InteractionPermission::denied; // debugTrees=false
    if(data[58]<18&&(flags&(1U<<4)))return InteractionPermission::denied;
    if(data[58]>=18&&!pet&&(flags&(1U<<6)))return InteractionPermission::denied;
    if(data[32]==1&&data[34]<2&&!(flags&1U))return InteractionPermission::denied;
    return InteractionPermission::allowed;
}
bool InteractionQueue::source_recovery_remove(std::uint64_t uid,bool allow_active) {
    if(executing_ || !vm_ || vm_->dispatch_active() || vm_->run_generation()!=run_generation_)return false;
    const auto found=std::find_if(queue_.begin(),queue_.end(),[&](const auto& action){return action.uid==uid;});
    if(found==queue_.end())return false;
    const bool current=active_&&active_->uid==uid;
    const auto parent=std::find_if(suspended_active_.begin(),suspended_active_.end(),[&](const auto& action){return action.uid==uid;});
    if((current || parent!=suspended_active_.end())&&!allow_active)return false;
    if(current){active_.reset();wait_.reset();resume_tick_.reset();}
    if(parent!=suspended_active_.end())suspended_active_.erase(parent);
    if(completion_&&completion_->uid==uid)completion_.reset();
    queue_.erase(found);fresh_=false;diagnostic_.clear();return true;
}
bool InteractionQueue::source_recovery_set_active_icon(WorldEntityReference icon) {
    if(!active_)return icon==WorldEntityReference{} && vm_ && !vm_->dispatch_active() && !executing_;
    return source_recovery_set_action_icon(active_->uid,icon);
}
bool InteractionQueue::source_recovery_set_action_icon(std::uint64_t uid,WorldEntityReference icon) {
    if(executing_ || !vm_ || vm_->dispatch_active() || vm_->run_generation()!=run_generation_ ||
       (icon.id==0?icon.generation!=0:icon.id<0||!icon.generation))return false;
    const auto found=std::find_if(queue_.begin(),queue_.end(),[&](const auto& action){return action.uid==uid;});
    if(found==queue_.end())return false;
    found->icon_owner=icon.id?std::optional<InteractionIconOwnerSnapshot>{{icon.id,icon.generation}}:std::nullopt;
    fresh_=false;return true;
}
bool InteractionQueue::source_recovery_rebase() {
    if(executing_ || !vm_ || vm_->dispatch_active())return false;
    const auto* person=world_.find(actor_id_);
    if(!person || person->generation!=actor_generation_)return false;
    run_generation_=vm_->run_generation();wait_.reset();resume_tick_.reset();completion_.reset();
    interrupted_=false;fresh_=false;diagnostic_.clear();return true;
}
InteractionQueueSnapshot InteractionQueue::snapshot() const {
    if(executing_)throw std::logic_error("interaction callbacks must not capture controller state");
    if(vm_)(void)vm_->snapshot(); // Reject capture inside another VM primitive/trace callback too.
    return {actor_id_,capacity_,actor_generation_,run_generation_,sequence_,clock_,idle_start_,last_tick_,resume_tick_,
            queue_,active_,wait_,completion_,interrupted_,diagnostic_,profile_,next_queue_uid_,suspended_active_};
}
bool InteractionQueue::valid_snapshot(const InteractionQueueSnapshot& state) const {
    if(executing_)throw std::logic_error("interaction callbacks must not restore controller state");
    if(!vm_)return false;
    return valid_snapshot_with_vm(state,vm_->snapshot());
}
bool InteractionQueue::can_restore_snapshot(const InteractionQueueSnapshot& state,const VmSnapshot& vm) const {
    if(executing_ || !vm_ || vm_->dispatch_active())return false;
    return valid_snapshot_with_vm(state,vm);
}
bool InteractionQueue::valid_snapshot_with_vm(const InteractionQueueSnapshot& state,const VmSnapshot& vm) const {
    constexpr auto max_clock=std::numeric_limits<std::uint32_t>::max();
    const auto* actor=world_.find(actor_id_);
    if(!fresh_||!actor||actor->kind!=EntityKind::avatar||state.actor_id!=actor_id_||
       state.actor_generation!=actor->generation||state.actor_generation!=actor_generation_||
       state.run_generation!=vm.run_generation||!state.capacity||state.capacity>capacity_||
       state.actions.size()>state.capacity||state.clock>max_clock||state.idle_start>state.clock||
       state.profile>InteractionQueueProfile::ts1_priority||
       (state.last_tick?*state.last_tick!=state.clock:state.clock!=0))return false;
    const auto protected_count=state.suspended_active.size()+(state.active?1U:0U);
    if((!state.active&&!state.suspended_active.empty())||protected_count>state.actions.size()||
       (state.profile==InteractionQueueProfile::legacy_fifo&&!state.suspended_active.empty()))return false;
    std::set<std::uint64_t> action_uids;
    for(const auto& action:state.actions){
        const auto& call=action.call;
        if(call.context.entity_references_bound&&(call.context.caller_generation!=state.actor_generation||
           call.context.callee_generation!=action.target_generation||
           call.context.stack_object_generation!=action.target_generation))return false;
        if((action.label&&!valid_interaction_label(*action.label))||action.check_context>InteractionCheckContext::autonomous||
           (action.check_context==InteractionCheckContext::autonomous&&
            (action.argument_mode!=InteractionArgumentMode::source_explicit||!action.check_permissions||call.arguments.size()!=4))||
           action.argument_mode>InteractionArgumentMode::source_explicit||
           call.exact_arguments!=(action.argument_mode!=InteractionArgumentMode::legacy_metadata)||
           (action.argument_mode==InteractionArgumentMode::source_null&&
            (call.arguments.size()!=4||std::any_of(call.arguments.begin(),call.arguments.end(),[](auto value){return value!=0;})))||
           (action.icon_owner&&(action.icon_owner->id<=0||!action.icon_owner->generation)))return false;
        const auto* target=world_.resolve_reference({call.context.callee,action.target_generation});
        if(!action.uid||!action_uids.insert(action.uid).second||action.uid>state.sequence||!target||
           action.target_generation!=target->generation||call.context.caller!=actor_id_||
           call.context.stack_object!=call.context.callee||call.arguments.size()>4096||
           !routine_key_valid(call.key)||
           call.return_mode!=((action.flags&ttab_run_immediately)?VmCallReturnMode::immediate:
                                                                   VmCallReturnMode::interaction_next_tick)||
           ((action.flags&ttab_run_immediately)&&action.mode!=InteractionQueueMode::idle)||
           action.mode>InteractionQueueMode::idle ||
           (action.check && (!routine_key_valid(*action.check) ||
                            !vm_->has_routine(*action.check,call.context.code_owner))))
            return false;
    }
    std::set<std::uint64_t> active_tickets;
    for(std::size_t n=0;n<state.suspended_active.size();++n){
        const auto& parent=state.suspended_active[n];
        if(!parent.uid||!parent.ticket||parent.ticket>vm.ticket_sequence||
           state.actions[n].uid!=parent.uid||!active_tickets.insert(parent.ticket).second)return false;
    }
    if(state.active){
        if(!state.active->uid||!state.active->ticket||state.active->ticket>vm.ticket_sequence||
           state.actions[state.suspended_active.size()].uid!=state.active->uid||
           !active_tickets.insert(state.active->ticket).second)return false;
    }
    const auto valid_ticket=[&](std::uint64_t ticket){return ticket&&ticket<=vm.ticket_sequence;};
    std::size_t action_frames=0;
    for(const auto& frame:vm.stack){
        if(frame.return_mode!=VmCallReturnMode::interaction_next_tick&&frame.return_mode!=VmCallReturnMode::immediate)continue;
        if(action_frames>=protected_count)return false;
        const auto expected=action_frames<state.suspended_active.size()?
            state.suspended_active[action_frames]:*state.active;
        if(frame.invocation_ticket!=expected.ticket)return false;
        const auto& call=state.actions[action_frames].call;
        if(frame.return_mode!=call.return_mode||frame.exact_arguments!=call.exact_arguments||frame.key.scope!=call.key.scope||frame.key.id!=call.key.id||
           !same_action_owner_context(frame.context,call.context)||
           (call.exact_arguments?frame.arguments.size()!=call.arguments.size():frame.arguments.size()<call.arguments.size()))return false;
        ++action_frames;
    }
    const auto active_mode=state.active?state.actions[state.suspended_active.size()].call.return_mode:
        VmCallReturnMode::ordinary;
    const auto active_owner=state.active?state.actions[state.suspended_active.size()].call.context.code_owner:CodeOwner{0};
    const bool returned_normal=state.active&&active_mode==VmCallReturnMode::interaction_next_tick&&vm.interaction_return&&
        vm.interaction_return->ticket==state.active->ticket&&!state.resume_tick&&
        vm.interaction_return->run_generation==state.run_generation&&vm.interaction_return->code_owner==active_owner;
    const bool returned_immediate=state.active&&active_mode==VmCallReturnMode::immediate&&vm.immediate_return&&
        vm.immediate_return->ticket==state.active->ticket&&!state.resume_tick&&
        vm.immediate_return->run_generation==state.run_generation&&vm.immediate_return->code_owner==active_owner;
    const bool returned_unobserved_child=returned_normal||returned_immediate;
    if(action_frames!=protected_count-(returned_unobserved_child?1U:0U))return false;
    if(state.active){
        if(state.wait&&action_frames==0)return false;
        if(!state.wait&&state.idle_start!=0&&!state.resume_tick&&
           ((vm.status!=VmStatus::running&&vm.status!=VmStatus::budget_exhausted)||
            vm.pending||vm.interaction_return||vm.immediate_return||vm.external_frame_return||
            !vm.suspended_pending.empty()||vm.stack.empty()))return false;
    }else if(!state.suspended_active.empty())return false;
    if(state.wait){
        if((state.resume_tick)||!valid_ticket(state.wait->ticket)||
           (state.active&&!action_frames)||
           state.wait->deadline<=state.clock||state.wait->deadline>max_clock||
           vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=state.wait->ticket||
           vm.pending->context.caller!=actor_id_||vm.pending->instruction.instruction.opcode!=17||vm.stack.empty())return false;
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto allow=static_cast<unsigned>(operands[2]|(static_cast<unsigned>(operands[3])<<8));
        if(index>=vm.stack.back().arguments.size()||index>32767||allow>1)return false;
        const auto remaining=vm.stack.back().arguments[index];
        if(remaining<0)return false;
        const auto legacy_delay=static_cast<std::uint64_t>(remaining)+1;
        const auto current_delay=allow==1?std::uint64_t{1}:legacy_delay;
        // Older Main waits persisted the full countdown even with AllowPush=1.
        // Preserve those deadlines when reopening v1-v4 snapshots, while new
        // waits poll once per tick; action-tree AllowPush=1 also polls per tick.
        if(state.wait->deadline!=state.idle_start+current_delay&&
           !(allow==1&&!state.active&&state.wait->deadline==state.idle_start+legacy_delay))return false;
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==17&&vm.pending->context.caller==actor_id_)
        return false;
    if(state.completion){
        const auto& completion=*state.completion;
        if(!completion.uid||completion.uid>state.sequence||!valid_ticket(completion.ticket)||completion.tick>state.clock||
           action_uids.count(completion.uid))return false;
    }
    if(state.resume_tick){
        if(!state.completion||state.wait||*state.resume_tick>max_clock||
           *state.resume_tick<=state.clock||*state.resume_tick!=state.completion->tick+1||!vm.interaction_return||
           vm.interaction_return->ticket!=state.completion->ticket||vm.interaction_return->result!=state.completion->result)
            return false;
    }
    if(vm.interaction_return){
        const bool matches_unobserved=returned_normal&&
            vm.interaction_return->code_owner==state.actions[state.suspended_active.size()].call.context.code_owner;
        const bool matches_observed=state.completion&&vm.interaction_return->ticket==state.completion->ticket&&
            vm.interaction_return->result==state.completion->result&&
            (!state.resume_tick||*state.resume_tick==state.completion->tick+1);
        if(!matches_unobserved&&!matches_observed)return false;
    }
    if(vm.immediate_return&&!returned_immediate)return false;
    return true;
}
void InteractionQueue::publish_snapshot(InteractionQueueSnapshot&& state) noexcept {
    capacity_=state.capacity;actor_generation_=state.actor_generation;run_generation_=state.run_generation;
    sequence_=state.sequence;clock_=state.clock;idle_start_=state.idle_start;last_tick_=state.last_tick;
    resume_tick_=state.resume_tick;queue_.swap(state.actions);suspended_active_.swap(state.suspended_active);
    active_=state.active;wait_=state.wait;
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
bool InteractionQueue::bind_legacy_references(InteractionQueueSnapshot& state,const World& world){
    auto candidate=state;
    for(auto& action:candidate.actions){
        auto& context=action.call.context;
        if(!world.bind_context_live(context))return false;
        if(context.caller!=candidate.actor_id||context.caller_generation!=candidate.actor_generation||
           context.callee_generation!=action.target_generation||context.stack_object!=context.callee||
           context.stack_object_generation!=action.target_generation)return false;
    }
    state=std::move(candidate);return true;
}
std::vector<WorldEntityReference> InteractionQueue::strong_entity_references() const {
    std::vector<WorldEntityReference> result;
    for(const auto& action:queue_){
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
            const auto ref=vm_context_reference(action.call.context,role);
            if(ref.id>0&&ref.generation)result.push_back(ref);
        }
    }
    return result;
}
std::optional<bool> InteractionQueue::source_reset_required() const {
    if(!vm_||vm_->dispatch_active()||run_generation_!=vm_->run_generation())return {};
    if(vm_->stack().empty()||!vm_->stack().back().action_tree)return false;
    if(queue_.empty()||!active_)return {};
    // Source VMThread.Tick uses Queue[0], not a guessed current frame target.
    const auto& action=queue_.front();
    const WorldEntityReference ref{action.call.context.callee,action.target_generation};
    if(!world_.resolve_reference(ref))return {};
    return world_.reference_is_dead(ref);
}
bool InteractionQueue::upgrade_legacy_action_tree(VmSnapshot& vm,InteractionQueueSnapshot& queue){
    if(vm.action_tree_known)return true;
    auto frames=vm.stack;
    const auto count=queue.suspended_active.size()+(queue.active?1U:0U);
    if(count>queue.actions.size()||(!queue.active&&!queue.suspended_active.empty()))return false;
    std::size_t matched=0;
    bool inherited=false;
    for(auto& frame:frames){
        if(frame.return_mode==VmCallReturnMode::interaction_next_tick||frame.return_mode==VmCallReturnMode::immediate){
            if(matched>=count)return false;
            const auto expected=matched<queue.suspended_active.size()?queue.suspended_active[matched]:*queue.active;
            const auto& action=queue.actions[matched];const auto& call=action.call;
            if(expected.uid!=action.uid||expected.ticket!=frame.invocation_ticket||frame.return_mode!=call.return_mode||
               frame.key.scope!=call.key.scope||frame.key.id!=call.key.id||frame.context.caller!=call.context.caller||
               frame.context.callee!=call.context.callee||frame.context.code_owner!=call.context.code_owner||
               frame.exact_arguments!=call.exact_arguments)return false;
            inherited=true;++matched;
        }
        frame.action_tree=inherited;
    }
    // The innermost accepted action can already have returned to its parent
    // before the queue observes the actual return ticket.
    const bool returned=queue.active&&((vm.interaction_return&&vm.interaction_return->ticket==queue.active->ticket)||
        (vm.immediate_return&&vm.immediate_return->ticket==queue.active->ticket));
    if(matched!=count-(returned?1U:0U))return false;
    vm.stack=std::move(frames);vm.action_tree_known=true;
    for(auto& action:queue.actions)action.call.action_tree_override=true;
    return true;
}
std::optional<bool> InteractionQueue::live_action_tree(const Vm& real_thread) const {
    if(!vm_||vm_!=&real_thread)return {};
    if(real_thread.synchronous_queue_empty())return real_thread.stack().empty()||!real_thread.stack().back().action_tree?
        std::optional<bool>{false}:std::nullopt;
    const auto* actor=world_.find(actor_id_);
    if(!actor||actor->generation!=actor_generation_)return {};
    const auto& stack=vm_->stack();
    if(stack.empty())return false; // Source LastOrDefault short-circuits before ActiveAction.
    if(!active_){
        if(!suspended_active_.empty())return {};
        return stack.back().action_tree?std::optional<bool>{}:std::optional<bool>{false};
    }
    const auto count=suspended_active_.size()+1;
    if(run_generation_!=vm_->run_generation()||queue_.size()<count)return {};
    std::size_t matched=0,last=0;
    for(std::size_t n=0;n<stack.size();++n){
        const auto& entry=stack[n];
        if(entry.return_mode!=VmCallReturnMode::interaction_next_tick&&entry.return_mode!=VmCallReturnMode::immediate)continue;
        if(matched>=count)return {};
        const auto expected=matched<suspended_active_.size()?suspended_active_[matched]:*active_;
        const auto& action=queue_[matched];const auto& call=action.call;
        const auto* target=world_.resolve_reference({call.context.callee,action.target_generation});
        if(action.uid!=expected.uid||entry.invocation_ticket!=expected.ticket||entry.return_mode!=call.return_mode||
           entry.exact_arguments!=call.exact_arguments||
           entry.key.id!=call.key.id||entry.key.scope!=call.key.scope||
           !same_action_owner_context(entry.context,call.context)||!target||target->generation!=action.target_generation)return {};
        ++matched;last=n;
    }
    if(matched!=count)return {};
    // Ordinary source child calls inherit ActionTree. External services can
    // manufacture their own frames; absent an explicit propagation proof they
    // cannot borrow an interaction ancestor's authority.
    for(std::size_t n=last+1;n<stack.size();++n)if(stack[n].return_mode!=VmCallReturnMode::ordinary&&
        stack[n].return_mode!=VmCallReturnMode::external_frame_next_tick)return {};
    return stack.back().action_tree;
}
std::optional<bool> InteractionQueue::interacting_with(const World& expected_world,const Vm& real_thread,
    std::int16_t caller,std::int16_t target) const {
    // Compatibility entry is an explicit fresh numeric lookup. Actual VM
    // callers pass the captured reference overload, including stable nil.
    const auto* current=expected_world.find(target);
    return interacting_with(expected_world,real_thread,caller,WorldEntityReference{target,current?current->generation:0});
}
std::optional<bool> InteractionQueue::interacting_with(const World& expected_world,const Vm& real_thread,
    std::int16_t caller,WorldEntityReference target) const {
    if(&expected_world!=&world_||caller!=actor_id_)return {};
    const auto action_tree=live_action_tree(real_thread);
    if(!action_tree||!*action_tree)return action_tree;
    if(!target.generation)return false; // Stable source-null, even with raw ID.
    if(!world_.resolve_reference(target))return {};
    const auto& action=queue_[suspended_active_.size()];
    return action.call.context.callee==target.id&&action.target_generation==target.generation;
}
std::optional<bool> InteractionQueue::action_tree_for(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch) const {
    if(executing_||!vm_||dispatch.origin!=vm_||dispatch.check_mode||instruction.opcode!=20||
       frame.context.caller!=actor_id_||!vm_->matches_active_primitive(frame,instruction,dispatch))return {};
    return live_action_tree(*vm_);
}
std::optional<InteractionQueue::PreparedIconUpdate> InteractionQueue::prepare_icon_owner_update(
    const VmFrame& frame,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t icon,std::uint64_t generation,const std::optional<VmCall>& child) {
    const bool generic=instruction.opcode==1&&instruction.operands[0]==2;
    std::optional<bool> action;
    if(generic){
        if(executing_||!vm_||dispatch.origin!=vm_||dispatch.check_mode||frame.context.caller!=actor_id_||
           !vm_->matches_active_primitive(frame,instruction,dispatch)||child)return {};
        action=live_action_tree(*vm_);
    }else action=action_tree_for(frame,instruction,dispatch);
    const auto* entity=world_.frame_entity(frame,VmEntityRole::stack_object);
    if(!action||!*action||(!generic&&instruction.operands[2]==0)||!active_||queue_.size()<=suspended_active_.size())return {};
    if(entity){if(icon!=entity->id||!generation||entity->generation!=generation)return {};}
    else if(!generic||icon||generation)return {}; // Source-null mode2 clears IconOwner.
    if(child&&(child->return_mode!=VmCallReturnMode::ordinary||child->context.caller!=actor_id_||!child->exact_arguments))return {};
    PreparedIconUpdate result;
    result.queue_=this;result.vm_=vm_;result.frame_=frame;result.instruction_=instruction;result.dispatch_=dispatch;
    result.child_=child;if(result.child_&&!world_.bind_context_live(result.child_->context))return {};
    result.depth_=vm_->stack().size();result.actor_generation_=actor_generation_;
    result.run_=vm_->run_generation();result.steps_=vm_->steps();result.uid_=active_->uid;result.ticket_=active_->ticket;
    result.queue_sequence_=sequence_;result.clock_=clock_;result.protected_count_=suspended_active_.size()+1;
    if(entity)result.icon_=InteractionIconOwnerSnapshot{icon,generation};
    result.previous_=queue_[suspended_active_.size()].icon_owner;
    return result;
}
PrimitiveOutcome InteractionQueue::set_action_icon_to_stack_object(const VmFrame& frame,const Instruction& instruction,const VmDispatch& dispatch){
    if(instruction.opcode!=1||instruction.operands[0]!=2)return {};
    const auto* icon=world_.frame_entity(frame,VmEntityRole::stack_object);
    auto update=prepare_icon_owner_update(frame,instruction,dispatch,icon?icon->id:0,icon?icon->generation:0);
    if(!update||!update->commit())return unsupported("GenericTS1Call2 lacks its actual active action or source icon authority");
    return {PrimitiveDisposition::goto_true,{}};
}
bool InteractionQueue::PreparedIconUpdate::can_commit() const noexcept {
    if(consumed_||!queue_||!vm_||queue_->vm_!=vm_||queue_->executing_||!vm_->dispatch_active()||
       vm_->run_generation()!=run_||vm_->steps()!=steps_||queue_->run_generation_!=run_||
       queue_->actor_generation_!=actor_generation_||!queue_->active_||queue_->active_->uid!=uid_||
       queue_->sequence_!=queue_sequence_||queue_->clock_!=clock_||queue_->suspended_active_.size()+1!=protected_count_||
       queue_->active_->ticket!=ticket_||queue_->queue_.size()<=queue_->suspended_active_.size())return false;
    const auto* actor=queue_->world_.find(queue_->actor_id_);
    if(!actor||actor->generation!=actor_generation_)return false;
    if(icon_){const auto* icon=queue_->world_.resolve_reference({icon_->id,icon_->generation});if(!icon||icon->generation!=icon_->generation)return false;}
    else if(instruction_.opcode!=1||instruction_.operands[0]!=2||queue_->world_.frame_entity(frame_,VmEntityRole::stack_object))return false;
    const auto& action=queue_->queue_[queue_->suspended_active_.size()];
    if(action.uid!=uid_||!same_icon(previous_,action.icon_owner))return false;
    const auto& stack=vm_->stack();
    if(stack.size()==depth_)return vm_->matches_active_primitive(frame_,instruction_,dispatch_);
    if(!child_||stack.size()!=depth_+1||!depth_||!same_frame(stack[depth_-1],frame_)||
       !vm_->matches_active_call_commit(frame_,instruction_,dispatch_,depth_))return false;
    const auto& child=stack.back();const auto& expected=*child_;
    return child.pc==0&&child.key.id==expected.key.id&&child.key.scope==expected.key.scope&&
        same_context(child.context,expected.context)&&child.return_mode==expected.return_mode&&
        child.invocation_ticket==dispatch_.ticket&&child.exact_arguments==expected.exact_arguments&&
        child.action_tree==expected.action_tree_override.value_or(frame_.action_tree)&&child.arguments==expected.arguments;
}
bool InteractionQueue::PreparedIconUpdate::commit() noexcept {
    if(!can_commit())return false;
    queue_->queue_[queue_->suspended_active_.size()].icon_owner=icon_;
    consumed_=true;return true;
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
        queue_.clear();suspended_active_.clear();active_.reset();wait_.reset();resume_tick_.reset();completion_.reset();
        interrupted_=false;idle_start_=0;run_generation_=vm_->run_generation();
        diagnostic_="VM restart invalidated queued interactions";
        }
    }
    const auto protected_count=suspended_active_.size()+(active_?1U:0U);
    if((!active_&&!suspended_active_.empty())||protected_count>queue_.size()){
        diagnostic_="active interaction prefix identity mismatch";return false;
    }
    for(std::size_t n=0;n<protected_count;++n){
        const auto expected=n<suspended_active_.size()?suspended_active_[n]:*active_;
        if(queue_[n].uid!=expected.uid){diagnostic_="active interaction prefix identity mismatch";return false;}
        const auto* target=world_.resolve_reference({queue_[n].call.context.callee,queue_[n].target_generation});
        if(!target||target->generation!=queue_[n].target_generation){
            diagnostic_="active interaction target identity stale";return false;
        }
    }
    return true;
}
bool InteractionQueue::evaluate_queue_priorities() {
    const auto protected_count=suspended_active_.size()+(active_?1U:0U);
    if(!active_||!protected_count||queue_.size()<protected_count)return true;
    const auto* actor=world_.find(actor_id_);
    if(!actor||actor->kind!=EntityKind::avatar){diagnostic_="interaction priority actor identity stale";return false;}
    if(actor->person_data.size()<=33)return true;

    auto candidate=queue_;
    const auto active_index=protected_count-1;
    const auto& active=candidate[active_index];
    std::int16_t current_priority=actor->person_data[33];
    if(current_priority==2)--current_priority; // TS1 autonomous actions yield to other autonomous actions.
    bool notify_idle=active.priority!=0&&current_priority==0;
    bool interaction_canceled=false;
    for(auto it=candidate.begin()+static_cast<std::ptrdiff_t>(protected_count);it!=candidate.end();){
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
    candidate[active_index].notify_idle=notify_idle;
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
    return enqueue_impl(key,context,args,1,InteractionQueueMode::normal,0,push_after_active,false,false,{},false,{});
}
bool InteractionQueue::enqueue_ts1(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,bool push_head,bool push_tail,
        std::optional<RoutineKey> check,std::optional<std::int32_t> interaction_number) {
    flags|=tso_skip_permissions; // TS1 PushInteraction always sets TTABFlags.FSOSkipPermissions.
    return enqueue_impl(key,context,args,priority,mode,flags,push_head,push_tail,true,check,false,interaction_number);
}
bool InteractionQueue::enqueue_user(RoutineKey key,VmContext context,
        const std::vector<std::int16_t>& args,std::uint32_t flags,std::optional<RoutineKey> check,
        std::optional<std::int32_t> interaction_number) {
    return enqueue_impl(key,context,args,50,InteractionQueueMode::normal,flags,false,false,true,check,true,interaction_number);
}
bool InteractionQueue::enqueue_source_ts1(RoutineKey key,VmContext context,
        const std::optional<std::vector<std::int16_t>>& source_args,std::int16_t priority,
        InteractionQueueMode mode,std::uint32_t flags,bool push_head,bool push_tail,
        std::optional<RoutineKey> check,std::optional<std::int32_t> interaction_number,
        std::optional<InteractionIconOwnerSnapshot> icon_owner,std::optional<InteractionLabel> label) {
    const std::vector<std::int16_t> null_args(4,0);const auto& args=source_args?*source_args:null_args;
    return enqueue_impl(key,context,args,priority,mode,flags|tso_skip_permissions,push_head,push_tail,true,check,false,
        interaction_number,source_args?InteractionArgumentMode::source_explicit:InteractionArgumentMode::source_null,InteractionCheckContext::default_zero,icon_owner,std::move(label));
}
bool InteractionQueue::enqueue_source_user(RoutineKey key,VmContext context,
        const std::optional<std::vector<std::int16_t>>& source_args,std::uint32_t flags,
        std::optional<RoutineKey> check,std::optional<std::int32_t> interaction_number,std::optional<InteractionLabel> label) {
    const std::vector<std::int16_t> null_args(4,0);const auto& args=source_args?*source_args:null_args;
    return enqueue_impl(key,context,args,50,InteractionQueueMode::normal,flags,false,false,true,check,true,
        interaction_number,source_args?InteractionArgumentMode::source_explicit:InteractionArgumentMode::source_null,InteractionCheckContext::default_zero,{},std::move(label));
}
bool InteractionQueue::enqueue_source_autonomous(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
    std::uint32_t flags,std::optional<RoutineKey> check,std::optional<std::int32_t> number,std::optional<InteractionLabel> label){
    if(args.size()!=4){diagnostic_="autonomous source action requires exactly four arguments";return false;}
    return enqueue_impl(key,context,args,2,InteractionQueueMode::normal,flags,false,false,true,check,true,
        number,InteractionArgumentMode::source_explicit,InteractionCheckContext::autonomous,{},std::move(label));
}
bool InteractionQueue::first_higher_priority(std::int16_t priority,std::optional<InteractionActionSnapshot>& result){
    if(executing_||!vm_)return false;
    result.reset();
    if(vm_->synchronous_queue_empty())return true;
    if(!validate())return false;
    for(const auto& action:queue_)if(action.priority>priority){
        const auto* target=world_.resolve_reference({action.call.context.callee,action.target_generation});
        if(!target||target->generation!=action.target_generation){diagnostic_="autonomy queue shortcut target is stale";return false;}
        result=action;break;
    }
    return true;
}
bool InteractionQueue::active_actions(std::vector<InteractionActionSnapshot>& result){
    result.clear();const auto* actor=world_.find(actor_id_);
    if(!vm_||!actor||actor->generation!=actor_generation_||vm_->run_generation()!=run_generation_){
        diagnostic_="active-user queue actor identity stale";return false;}
    if(vm_->synchronous_queue_empty()){
        if(vm_->named_stack_inherits_parent_queue_index()&&(active_||!suspended_active_.empty())){
            diagnostic_="named empty queue retains an out-of-range source ActiveQueueBlock";return false;
        }
        return true;
    }
    const auto count=suspended_active_.size()+(active_?1U:0U);
    if(count>queue_.size()){diagnostic_="active-user protected queue prefix invalid";return false;}
    for(std::size_t n=0;n<count;++n){const auto& action=queue_[n];
        const auto expected=n<suspended_active_.size()?suspended_active_[n]:*active_;
        const auto* target=world_.resolve_reference({action.call.context.callee,action.target_generation});
        if(action.uid!=expected.uid||!target||target->generation!=action.target_generation){
            diagnostic_="active-user action target identity stale";return false;}
    }
    // GetUsers consults Queue[0..ActiveQueueBlock], including during CheckAction;
    // temporary check stacks do not replace these authoritative queue entries.
    result.assign(queue_.begin(),queue_.begin()+static_cast<std::ptrdiff_t>(count));return true;
}

std::optional<std::int16_t> InteractionQueue::active_advertisement(const Vm& thread,
    std::uint8_t type,std::uint16_t index,const AdvertisementLookup& lookup) const {
    if(type>2||!vm_||vm_!=&thread)return {};
    const auto* actor=world_.find(actor_id_);
    if(!actor||actor->generation!=actor_generation_||thread.run_generation()!=run_generation_)return {};
    // Source RunInMyStack clears Queue but preserves ActiveQueueBlock.
    // Missing overrides fault for an old active block; never borrow its TTAB.
    if(thread.synchronous_queue_empty())return thread.named_stack_inherits_parent_queue_index()&&
        (active_||!suspended_active_.empty())?std::nullopt:std::optional<std::int16_t>{0};
    if(!active_)return suspended_active_.empty()?std::optional<std::int16_t>{0}:std::nullopt;
    const auto count=suspended_active_.size()+1;
    if(count>queue_.size())return {};
    for(std::size_t n=0;n<count;++n){
        const auto& action=queue_[n];
        const auto expected=n<suspended_active_.size()?suspended_active_[n]:*active_;
        if(action.uid!=expected.uid||!world_.resolve_reference({action.call.context.callee,action.target_generation}))return {};
    }
    const auto& action=queue_[count-1];
    if(!action.interaction_number||!lookup)return {};
    // Preserve all source bits, including the global-action high bit. The
    // Callee's own TreeTable decides whether that exact key exists.
    return lookup({action.call.context.callee,action.target_generation},*action.interaction_number,type,index);
}

InteractionCancelResult InteractionQueue::cancel(std::uint64_t native_uid) {
    if(executing_){diagnostic_="interaction cancellation cannot reenter the queue";
        return InteractionCancelResult::unsupported;}
    fresh_=false;
    if(!validate())return InteractionCancelResult::unsupported;
    const auto selected_it=std::find_if(queue_.begin(),queue_.end(),[&](const Action& action){
        return action.uid==native_uid;
    });
    if(selected_it==queue_.end()){diagnostic_.clear();return InteractionCancelResult::missing;}
    if(profile_!=InteractionQueueProfile::ts1_priority){
        diagnostic_="TS1 action cancellation is unsupported for legacy FIFO queues";
        return InteractionCancelResult::unsupported;
    }
    const auto selected=*selected_it;
    const auto selected_index=static_cast<std::size_t>(std::distance(queue_.begin(),selected_it));
    const auto protected_count=suspended_active_.size()+(active_?std::size_t{1}:std::size_t{0});
    const auto* target=world_.find(selected.call.context.callee);
    if(!target||target->generation!=selected.target_generation){
        diagnostic_="cancelled interaction target identity is stale";
        return InteractionCancelResult::unsupported;
    }
    const bool must_run=(selected.flags&ttab_must_run)!=0;
    const bool direct_control=(selected.flags&ttab_direct_control)!=0;
    const bool removable=!must_run&&selected_index>=protected_count&&
        (selected.mode==InteractionQueueMode::normal||direct_control);
    if(removable&&!queue_skipped_handler_){
        diagnostic_="removing a TS1 action requires a synchronous queue-skipped handler";
        return InteractionCancelResult::unsupported;
    }

    std::vector<std::uint64_t> remove_parent_idle;
    std::vector<std::uint64_t> notify_parent_exit;
    if(selected.mode==InteractionQueueMode::parent_idle){
        for(std::size_t n=selected_index+1;n<queue_.size();++n){
            if(queue_[n].mode==InteractionQueueMode::parent_idle){
                if(n<protected_count){
                    diagnostic_="ParentIdle cancellation would remove a protected descendant";
                    return InteractionCancelResult::unsupported;
                }
                remove_parent_idle.push_back(queue_[n].uid);
            }else if(queue_[n].mode==InteractionQueueMode::parent_exit){
                notify_parent_exit.push_back(queue_[n].uid);
            }
        }
    }

    const auto* actor=world_.find(actor_id_);
    if(!removable&&(!actor||actor->generation!=actor_generation_||
       actor->kind!=EntityKind::avatar||actor->person_data.size()<=33||
       !actor->object_words.count(8))){
        diagnostic_="retained action cancellation requires avatar PersonData[33] and ObjectData[8]";
        return InteractionCancelResult::unsupported;
    }
    if(!removable){
        const auto flags=static_cast<std::uint16_t>(actor->object_words.at(8))|0x0080U;
        if(!world_.set_person_data(actor_id_,33,0)||
           !world_.declare_object_word(actor_id_,8,word(flags))){
            diagnostic_="retained action cancellation could not publish priority and cancel flag";
            return InteractionCancelResult::unsupported;
        }
    }

    for(const auto uid:notify_parent_exit){
        const auto it=std::find_if(queue_.begin(),queue_.end(),[uid](const Action& action){
            return action.uid==uid;
        });
        if(it!=queue_.end()){it->notify_idle=true;it->priority=0;}
    }
    for(const auto uid:remove_parent_idle){
        const auto it=std::find_if(queue_.begin(),queue_.end(),[uid](const Action& action){
            return action.uid==uid;
        });
        if(it!=queue_.end())queue_.erase(it);
    }
    const auto current=std::find_if(queue_.begin(),queue_.end(),[native_uid](const Action& action){
        return action.uid==native_uid;
    });
    if(current==queue_.end()){
        diagnostic_="cancelled action disappeared while applying its ParentIdle chain";
        return InteractionCancelResult::unsupported;
    }
    current->notify_idle=true;
    if(!removable){diagnostic_.clear();return InteractionCancelResult::notified;}

    const auto skipped=*current;
    queue_.erase(current);
    try{
        Guard guard(executing_);
        if(!queue_skipped_handler_(skipped)){
            diagnostic_="queue-skipped handler did not complete EntryPoint 4 synchronously";
            return InteractionCancelResult::unsupported;
        }
    }catch(const std::exception& error){
        diagnostic_=std::string("queue-skipped handler failed after action removal: ")+error.what();
        return InteractionCancelResult::unsupported;
    }catch(...){
        diagnostic_="queue-skipped handler failed after action removal";
        return InteractionCancelResult::unsupported;
    }
    diagnostic_.clear();return InteractionCancelResult::removed;
}
bool InteractionQueue::enqueue_impl(RoutineKey key,VmContext context,const std::vector<std::int16_t>& args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,bool push_head,bool push_tail,
        bool ts1_request,std::optional<RoutineKey> check,bool check_permissions,
        std::optional<std::int32_t> interaction_number,InteractionArgumentMode argument_mode,InteractionCheckContext check_context,
        std::optional<InteractionIconOwnerSnapshot> icon_owner,std::optional<InteractionLabel> label) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter enqueue");
    fresh_=false;
    if(!validate())return false;
    if(!world_.bind_context_live(context)){diagnostic_="interaction reference binding failed";return false;}
    const auto* target=world_.resolve_reference(vm_context_reference(context,VmEntityRole::callee));
    if(context.caller!=actor_id_ || context.caller_generation!=actor_generation_ || !target || !world_.is_live_reference(vm_context_reference(context,VmEntityRole::callee)) || context.stack_object!=context.callee || context.stack_object_generation!=context.callee_generation ||
       args.size()>4096 || queue_.size()>=capacity_ || sequence_==std::numeric_limits<std::uint64_t>::max() ||
       mode>InteractionQueueMode::idle) {
        diagnostic_="interaction request identity/argument/capacity outside supported queue profile";return false;
    }
    if(vm_->status()==VmStatus::ready || vm_->status()==VmStatus::fault ||
       vm_->status()==VmStatus::returned_true || vm_->status()==VmStatus::returned_false) {
        diagnostic_="interaction requires a live Main run";return false;
    }
    if(check && (!routine_key_valid(*check) ||
                 !vm_->has_routine(*check,context.code_owner))){
        diagnostic_="interaction check requires its exact installed code owner";return false;
    }
    if(icon_owner&&!world_.is_live_reference({icon_owner->id,icon_owner->generation})) {
        diagnostic_="interaction icon identity is absent or stale";return false;
    }
    if(push_head)flags|=tso_push_head;
    if(push_tail)flags|=tso_push_tail;
    const auto new_sequence=sequence_+1;
    Action action{new_sequence,target->generation,VmCall{key,context,args,VmCallReturnMode::interaction_next_tick},
        next_queue_uid_,priority,mode,flags,false,check,check_permissions};
    action.call.action_tree_override=true;
    action.icon_owner=icon_owner;
    // Missing or oversized display text cannot change a script’s enqueue result.
    if(label&&valid_interaction_label(*label))action.label=std::move(label);
    action.interaction_number=interaction_number;action.argument_mode=argument_mode;action.check_context=check_context;
    action.call.exact_arguments=argument_mode!=InteractionArgumentMode::legacy_metadata;
    const bool migrating_legacy=ts1_request&&profile_==InteractionQueueProfile::legacy_fifo;
    if(flags&ttab_run_immediately){
        // EnqueueAction's early return: append, hide, consume identity. It does
        // not reorder/cancel other actions, evaluate priority, or wake the VM.
        action.mode=InteractionQueueMode::idle;
        action.call.return_mode=VmCallReturnMode::immediate;
        auto candidate=queue_;
        candidate.push_back(std::move(action));
        if(migrating_legacy){
            std::uint16_t public_uid=0;
            for(auto& queued:candidate)queued.queue_uid=public_uid++;
            next_queue_uid_=public_uid;profile_=InteractionQueueProfile::ts1_priority;
        }else next_queue_uid_=static_cast<std::uint16_t>(next_queue_uid_+1U);
        queue_.swap(candidate);sequence_=new_sequence;diagnostic_.clear();return true;
    }
    const bool use_ts1_profile=profile_==InteractionQueueProfile::ts1_priority||ts1_request;
    const auto protected_prefix=suspended_active_.size()+(active_?std::size_t{1}:std::size_t{0});
    auto position=queue_.end();
    bool cancel_followups=false;
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
                position=std::next(it);found_pivot=true;
                cancel_followups=priority<=it->priority;
                break;
            }
            if(it->mode==InteractionQueueMode::parent_exit)hit_parent_end=true;
        }
        if(!found_pivot&&protected_prefix<queue_.size())
            position=queue_.begin()+static_cast<std::ptrdiff_t>(protected_prefix);
    }
    // The source repeatedly cancels one queue index. A retained MustRun or
    // parent-mode item makes that loop unbounded. Visit this original suffix in
    // order once instead: ParentIdle can remove later UIDs and retained items
    // stay signalled while the pass continues. New callback work is not part of
    // this pass (reentry into the same queue remains unsupported).
    std::vector<std::uint64_t> followups;
    if(cancel_followups)
        for(auto it=position;it!=queue_.end();++it)followups.push_back(it->uid);
    const auto target_generation=action.target_generation;
    queue_.insert(position,std::move(action));
    const auto remove_new_request=[&]{
        const auto inserted=std::find_if(queue_.begin(),queue_.end(),[&](const Action& queued){
            return queued.uid==new_sequence;
        });
        if(inserted!=queue_.end())queue_.erase(inserted);
    };
    const auto publish_identity=[&]{
        sequence_=new_sequence;
        if(migrating_legacy){
            std::uint16_t public_uid=0;
            for(auto& queued:queue_)queued.queue_uid=public_uid++;
            next_queue_uid_=public_uid;
            profile_=InteractionQueueProfile::ts1_priority;
        }else next_queue_uid_=static_cast<std::uint16_t>(next_queue_uid_+1U);
    };
    // Callbacks observe the accepted source identity. Once cancellation effects
    // begin, neither UID consumption nor those effects can be rolled back.
    const bool published_before_effects=!followups.empty();
    if(published_before_effects)publish_identity();
    for(const auto uid:followups){
        if(!contains_uid(uid))continue; // Removed by a preceding ParentIdle chain.
        if(cancel(uid)==InteractionCancelResult::unsupported){
            const auto reason=diagnostic_;
            remove_new_request();
            diagnostic_="automatic followup cancellation failed after queue publication; new request removed, earlier effects retained: "+reason;
            return false;
        }
    }
    const auto* current_target=world_.find(context.callee);
    if(published_before_effects&&(!validate()||!contains_uid(new_sequence)||
       !current_target||current_target->generation!=target_generation)){
        const auto reason=diagnostic_.empty()?std::string("new request identity changed during queue-skipped callbacks"):diagnostic_;
        remove_new_request();
        diagnostic_="automatic followup cancellation invalidated the new request; earlier effects retained: "+reason;
        return false;
    }
    if(!evaluate_queue_priorities()){
        remove_new_request();
        if(published_before_effects)
            diagnostic_="automatic followup priority evaluation failed after queue publication; new request removed, earlier effects retained: "+diagnostic_;
        return false;
    }
    if(!published_before_effects)publish_identity();
    diagnostic_.clear();return true;
}
VmPrimitive InteractionQueue::primitive() {
    return [this](const auto& f,const auto& i,const auto&,const auto& d){return request(f,i,d);};
}
PrimitiveOutcome InteractionQueue::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(i.opcode!=17&&i.opcode!=30)return {};
    const bool gosub=i.opcode==30;
    if(executing_ || !vm_ || dispatch.origin!=vm_)return unsupported("interaction controller identity/reentry rejected");
    if(vm_->synchronous_queue_empty()){
        if(!vm_->matches_active_query(frame,i,dispatch))return unsupported("named empty-queue service requires its actual dispatch");
        if(gosub){
            if(vm_->named_stack_inherits_parent_queue_index()&&(active_||!suspended_active_.empty()))
                return unsupported("named empty queue retains an out-of-range source ActiveQueueBlock");
            return {PrimitiveDisposition::goto_false,{}};
        }
        const auto index=word(i.operands[0]|(static_cast<std::uint64_t>(i.operands[1])<<8));
        const auto allow=static_cast<unsigned>(i.operands[2]|(static_cast<unsigned>(i.operands[3])<<8));
        if(index<0||static_cast<std::size_t>(index)>=frame.arguments.size()||allow>1)
            return unsupported("named IdleForInput argument/AllowPush outside explicit profile");
        const auto shared=schedule_idle_clock_read_?schedule_idle_clock_read_():std::nullopt;
        if(shared&&(*shared>clock_||!schedule_idle_clock_write_))
            return unsupported("named IdleForInput ScheduleIdleStart is outside its actual Thread clock");
        const auto start=shared?*shared:idle_start_;
        const auto elapsed=start!=0&&start<clock_?clock_-start:1;
        const auto remaining=word(static_cast<std::uint16_t>(frame.arguments[static_cast<std::size_t>(index)])-elapsed);
        if(shared)schedule_idle_clock_write_(0);else idle_start_=0;
        // Temporary Args disappear with this synchronous child on failure;
        // the real Thread's preceding clock effect remains source-visible.
        if(frame.action_tree)return unsupported("named IdleForInput ActionTree accesses its empty source queue");
        const bool thread_interrupted=thread_interrupt_&&thread_interrupt_();
        if(interrupted_||thread_interrupted||remaining<0){
            interrupted_=false;
            return {PrimitiveDisposition::goto_true,{},VmFrameWrite{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining}};
        }
        if(allow==0){if(shared)schedule_idle_clock_write_(clock_);else idle_start_=clock_;}
        return unsupported("named IdleForInput requires an external next/future scheduler publication");
    }
    Guard guard(executing_);
    fresh_=false;
    if(!validate())return unsupported(diagnostic_);
    if(frame.context.caller!=actor_id_)
        return unsupported("IdleForInput Caller is not this avatar");
    if(gosub){
        if(!vm_->matches_active_primitive(frame,i,dispatch))return unsupported("GosubFoundAction requires its real active instruction");
        if(active_){
            const auto at=suspended_active_.size();
            if(at>=queue_.size()||!vm_->set_active_primitive_stack_object(frame,i,dispatch,queue_[at].call.context.callee))
                return unsupported("GosubFoundAction could not publish active Callee before AttemptPush");
        }
    }
    const auto index=gosub?std::int16_t{0}:word(i.operands[0]|(static_cast<std::uint64_t>(i.operands[1])<<8));
    const auto allow=gosub?1U:static_cast<unsigned>(i.operands[2]|(static_cast<unsigned>(i.operands[3])<<8));
    if(!gosub&&(index<0 || static_cast<std::size_t>(index)>=frame.arguments.size() || allow>1))
        return unsupported("IdleForInput argument/AllowPush outside explicit profile");
    const auto shared_idle_start=!gosub&&schedule_idle_clock_read_?schedule_idle_clock_read_():std::nullopt;
    if(shared_idle_start&&(*shared_idle_start>clock_||!schedule_idle_clock_write_))
        return unsupported("IdleForInput shared ScheduleIdleStart is outside its clock");
    const auto start=shared_idle_start?*shared_idle_start:idle_start_;
    const auto elapsed=start!=0&&start<clock_?clock_-start:1;
    if(shared_idle_start)schedule_idle_clock_write_(0);
    const auto& stack=dispatch.origin->stack();
    const auto protected_count=suspended_active_.size()+(active_?std::size_t{1}:std::size_t{0});
    const auto active_index=protected_count?protected_count-1:0;
    const auto source_tree=live_action_tree(*dispatch.origin);
    if(!source_tree)return unsupported("IdleForInput ActionTree ancestry is unknown");
    const bool action_tree=*source_tree;
    if(action_tree){
        if(gosub)return {PrimitiveDisposition::goto_false,{}};
        if(wait_||resume_tick_)
            return unsupported("action-tree IdleForInput has no matching active interaction");
        const auto remaining=word(static_cast<std::uint16_t>(frame.arguments[static_cast<std::size_t>(index)])-elapsed);
        const VmFrameWrite output{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining};
        if(allow==1){
            if(profile_!=InteractionQueueProfile::ts1_priority)
                return unsupported("nested AllowPush requires the TS1 priority queue profile");
            std::optional<std::int32_t> priority_threshold;
            if(profile_==InteractionQueueProfile::ts1_priority){
                const auto* actor=world_.find(actor_id_);
                if(actor&&actor->person_data.size()>33){
                    priority_threshold=actor->person_data[33];
                    if(*priority_threshold<=2)--*priority_threshold;
                }
            }
            while(queue_.size()>protected_count){
                const auto candidate=queue_[protected_count];
                if(priority_threshold&&candidate.priority<=*priority_threshold)break;
                if(profile_==InteractionQueueProfile::ts1_priority){
                    const auto* actor=world_.find(actor_id_);
                    if(actor&&actor->object_words.count(8)){
                        auto flags=static_cast<std::uint16_t>(actor->object_words.at(8));
                        if(candidate.notify_idle){
                            flags|=0x0080U;
                            if(!world_.declare_object_word(actor_id_,8,word(flags)))
                                return unsupported("nested action notification could not set InteractionCanceled");
                        }
                        if(candidate.mode==InteractionQueueMode::parent_idle&&(flags&0x0080U)){
                            queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(protected_count));continue;
                        }
                    }
                }
                // C# queued actions retain their exact Callee body after deletion.
                // Admit only that generation; the existing ActionTree/Queue[0]
                // dead-callee lifecycle decides whether source Reset is required.
                const auto* candidate_target=world_.resolve_reference({candidate.call.context.callee,candidate.target_generation});
                if(!candidate_target||candidate_target->generation!=candidate.target_generation){
                    return unsupported(queued_target_diagnostic("nested queued interaction target identity stale","nested/pre-check",
                        actor_id_,actor_generation_,candidate,candidate_target,queue_,protected_count,active_,suspended_active_.size()));
                }
                if(candidate.check_permissions){
                    const auto permission=ts1_interaction_permission(world_.find(actor_id_),candidate.flags);
                    if(permission==InteractionPermission::unsupported)
                        return unsupported("interaction permissions require complete avatar person data");
                    if(permission==InteractionPermission::denied){
                        queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(protected_count));continue;
                    }
                }
                if(candidate.check){
                    const auto result=vm_->evaluate_check(*candidate.check,candidate.call.context,
                        eligibility_arguments(candidate),10000,check_primitive_,candidate.argument_mode!=InteractionArgumentMode::legacy_metadata,check_primitive_factory_);
                    if(!source_check_completed(result))
                        return unsupported("nested interaction check did not return synchronously: "+result.diagnostic);
                    if(!validate())return unsupported(diagnostic_);
                    if(source_check_false(result)){
                        queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(protected_count));continue;
                    }
                }
                const auto* target=world_.resolve_reference({candidate.call.context.callee,candidate.target_generation});
                if(!target||target->generation!=candidate.target_generation||
                   queue_.size()<=protected_count||queue_[protected_count].uid!=candidate.uid)
                    return unsupported(queued_target_diagnostic("nested interaction target identity changed during eligibility check","nested/post-check",
                        actor_id_,actor_generation_,candidate,target,queue_,protected_count,active_,suspended_active_.size()));
                if(!vm_->can_accept_call(candidate.call))
                    return unsupported("interaction VM cannot accept the nested action frame");
                // Allocate the suspended-parent slot before the VM publishes
                // the child frame or the commit callback changes PersonData.
                suspended_active_.reserve(suspended_active_.size()+1);
                const auto uid=candidate.uid;const auto priority=candidate.priority;
                const auto parent=*active_;
                const auto ticket=dispatch.ticket;
                PrimitiveOutcome outcome{PrimitiveDisposition::call,{},output,candidate.call};
                outcome.call_commit=[this,uid,priority,ticket,parent]{
                    if(!active_||active_->uid!=parent.uid||active_->ticket!=parent.ticket||
                       queue_.size()<=suspended_active_.size()+1||
                       queue_[suspended_active_.size()+1].uid!=uid)return false;
                    suspended_active_.push_back(parent);
                    if(const auto* actor=world_.find(actor_id_);actor&&actor->person_data.size()>33&&
                       !world_.set_person_data(actor_id_,33,priority)){
                        suspended_active_.pop_back();return false;
                    }
                    if(const auto* actor=world_.find(actor_id_);actor&&actor->object_words.count(8)&&
                       !world_.declare_object_word(actor_id_,8,
                           word(static_cast<std::uint16_t>(actor->object_words.at(8))&~0x0080U))){
                        if(const auto* current=world_.find(actor_id_);current&&current->person_data.size()>33)
                            world_.set_person_data(actor_id_,33,
                                queue_[suspended_active_.size()-1].priority);
                        suspended_active_.pop_back();return false;
                    }
                    active_=Active{uid,ticket};idle_start_=0;return true;
                };
                return outcome;
            }
        }
        if(queue_.size()>active_index&&queue_[active_index].notify_idle){
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
        const auto delay=allow==1?std::uint64_t{1}:static_cast<std::uint64_t>(remaining)+1;
        if(delay>std::numeric_limits<std::uint32_t>::max()-clock_)
            return unsupported("action-tree IdleForInput deadline exceeds uint32 clock");
        wait_=Wait{dispatch.ticket,clock_+delay};idle_start_=clock_;
        if(shared_idle_start&&allow==0)schedule_idle_clock_write_(clock_);
        return {PrimitiveDisposition::wait,{},output};
    }
    if(frame.return_mode!=VmCallReturnMode::ordinary||active_||wait_)
        return unsupported("Main IdleForInput has an active interaction or idle wait");
    const auto remaining=gosub?std::int16_t{0}:word(static_cast<std::uint16_t>(frame.arguments[static_cast<std::size_t>(index)])-elapsed);
    const std::optional<VmFrameWrite> output=gosub?std::nullopt:
        std::optional<VmFrameWrite>{{VmFrameBank::arguments,static_cast<std::size_t>(index),remaining}};
    std::optional<std::int32_t> priority_threshold;
    if(profile_==InteractionQueueProfile::ts1_priority){
        const auto* actor=world_.find(actor_id_);
        if(actor&&actor->person_data.size()>33){
            priority_threshold=actor->person_data[33];
            if(*priority_threshold<=2)--*priority_threshold;
        }
    }
    while(allow==1 && queue_.size()>protected_count) {
        // The check can mutate World and thread registers even when it returns
        // false. Keep a value copy and resolve identities again after it runs.
        const auto candidate=queue_[protected_count];
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
                    queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(protected_count));continue;
                }
            }
        }
        const auto* candidate_target=world_.resolve_reference({candidate.call.context.callee,candidate.target_generation});
        if(!candidate_target || candidate_target->generation!=candidate.target_generation)
            return unsupported(queued_target_diagnostic("queued interaction target identity stale","main/pre-check",
                actor_id_,actor_generation_,candidate,candidate_target,queue_,protected_count,active_,suspended_active_.size()));
        if(candidate.check_permissions){
            const auto permission=ts1_interaction_permission(world_.find(actor_id_),candidate.flags);
            if(permission==InteractionPermission::unsupported)
                return unsupported("interaction permissions require complete avatar person data");
            if(permission==InteractionPermission::denied){
                queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(protected_count));continue;
            }
        }
        if(candidate.check){
            const auto result=vm_->evaluate_check(*candidate.check,candidate.call.context,
                eligibility_arguments(candidate),10000,check_primitive_,candidate.argument_mode!=InteractionArgumentMode::legacy_metadata,check_primitive_factory_);
            if(!source_check_completed(result))
                return unsupported("interaction check did not return synchronously: "+result.diagnostic);
            if(!validate())return unsupported(diagnostic_);
            if(source_check_false(result)){
                queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(protected_count));continue;
            }
        }
        if(queue_.size()<=protected_count||queue_[protected_count].uid!=candidate.uid)
            return unsupported("queued action identity changed during eligibility check");
        const auto& action=queue_[protected_count];const auto* target=world_.resolve_reference({action.call.context.callee,action.target_generation});
        if(!target || target->generation!=action.target_generation)
            return unsupported(queued_target_diagnostic("queued interaction target identity stale","main/post-check",
                actor_id_,actor_generation_,action,target,queue_,protected_count,active_,suspended_active_.size()));
        if(!vm_->can_accept_call(action.call))
            return unsupported("interaction VM cannot accept the queued action frame");
        const auto uid=action.uid;
        const auto priority=action.priority;
        const auto ticket=dispatch.ticket;
        PrimitiveOutcome outcome{PrimitiveDisposition::call,{},output,action.call};
        outcome.call_commit=[this,uid,priority,ticket,protected_count]{
            if(active_||!suspended_active_.empty()||queue_.size()<=protected_count||
               queue_[protected_count].uid!=uid)return false;
            if(const auto* actor=world_.find(actor_id_);actor&&actor->person_data.size()>33&&
               !world_.set_person_data(actor_id_,33,priority))return false;
            if(const auto* actor=world_.find(actor_id_);actor&&actor->object_words.count(8))
                world_.declare_object_word(actor_id_,8,
                    word(static_cast<std::uint16_t>(actor->object_words.at(8))&~0x0080U));
            active_=Active{uid,ticket};idle_start_=0;return true;
        };
        return outcome;
    }
    if(gosub)return {PrimitiveDisposition::goto_false,{}};
    if(queue_.size()>protected_count) {
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
    const auto delay=allow==1?std::uint64_t{1}:static_cast<std::uint64_t>(remaining)+1;
    if(delay>std::numeric_limits<std::uint32_t>::max()-clock_)
        return unsupported("IdleForInput deadline exceeds uint32 clock");
    wait_=Wait{dispatch.ticket,clock_+delay};idle_start_=clock_;
    if(shared_idle_start&&allow==0)schedule_idle_clock_write_(clock_);
    return {PrimitiveDisposition::wait,{},output};
}
bool InteractionQueue::has_immediate() const {
    return std::any_of(queue_.begin(),queue_.end(),[](const Action& action){
        return (action.flags&ttab_run_immediately)!=0;
    });
}
bool InteractionQueue::park_immediate_parent_wait(std::uint64_t ticket) {
    if(executing_&&!immediate_parking_)return false;
    if(!vm_||!ticket||!wait_||wait_->ticket!=ticket||
       vm_->run_generation()!=run_generation_||vm_->immediate_continuations().empty())return false;
    const auto* actor=world_.find(actor_id_);
    const auto& continuation=vm_->immediate_continuations().back();
    if(!actor||actor->generation!=actor_generation_||!continuation.parent_pending||
       continuation.parent_pending->ticket!=ticket||
       continuation.parent_pending->context.caller!=actor_id_||
       continuation.parent_pending->instruction.instruction.opcode!=17||!continuation.parent_depth||
       continuation.parent_depth>=vm_->stack().size())return false;
    const auto& operands=continuation.parent_pending->instruction.instruction.operands;
    const auto index=word(operands[0]|(static_cast<std::uint64_t>(operands[1])<<8));
    const auto allow=static_cast<unsigned>(operands[2]|(static_cast<unsigned>(operands[3])<<8));
    const auto& frame=vm_->stack()[continuation.parent_depth-1];
    if(index<0||static_cast<std::size_t>(index)>=frame.arguments.size()||allow>1||
       frame.arguments[static_cast<std::size_t>(index)]<0)return false;
    wait_.reset();return true;
}
bool InteractionQueue::retry_immediate_parent(std::uint64_t ticket) {
    if(executing_&&!immediate_resuming_)return false;
    if(!vm_||!ticket||wait_||vm_->run_generation()!=run_generation_||
       vm_->status()!=VmStatus::waiting||!vm_->pending()||vm_->pending()->ticket!=ticket||
       vm_->pending()->context.caller!=actor_id_||
       vm_->pending()->instruction.instruction.opcode!=17||vm_->stack().empty())return false;
    const auto* actor=world_.find(actor_id_);
    if(!actor||actor->generation!=actor_generation_)return false;
    const auto& operands=vm_->pending()->instruction.instruction.operands;
    const auto index=word(operands[0]|(static_cast<std::uint64_t>(operands[1])<<8));
    const auto allow=static_cast<unsigned>(operands[2]|(static_cast<unsigned>(operands[3])<<8));
    if(index<0||static_cast<std::size_t>(index)>=vm_->stack().back().arguments.size()||allow>1||
       vm_->stack().back().arguments[static_cast<std::size_t>(index)]<0)return false;
    return vm_->retry_pending(ticket);
}
bool InteractionQueue::try_run_immediate(const ImmediatePendingAudit& audit,
        const std::function<bool(std::uint64_t)>& park_after_injection) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter immediate injection");
    fresh_=false;
    Guard guard(executing_);
    if(!validate())return false;
    while(true){
        const auto found=std::find_if(queue_.begin(),queue_.end(),[](const Action& action){
            return (action.flags&ttab_run_immediately)!=0;
        });
        if(found==queue_.end()){diagnostic_.clear();return true;}
        const auto index=static_cast<std::size_t>(std::distance(queue_.begin(),found));
        const auto protected_count=suspended_active_.size()+(active_?std::size_t{1}:std::size_t{0});
        // Source scans the entire queue. An active immediate protects the first
        // match and blocks a later immediate from preempting it.
        if(index<protected_count){diagnostic_.clear();return true;}
        const auto action=*found;
        const auto* target=world_.find(action.call.context.callee);
        if(!target||target->generation!=action.target_generation){
            queue_.erase(found);continue; // Source CheckAction rejects dead targets.
        }
        auto call=action.call;call.return_mode=VmCallReturnMode::ordinary;
        // Reject unsafe parent services/caps before a Check can publish effects.
        if(!vm_->can_inject_immediate(call,audit)){
            diagnostic_="immediate interaction VM parent/cap/audit is unsupported";return false;
        }
        if(vm_->pending()&&!park_after_injection){
            diagnostic_="immediate interaction requires a host parent-service park handler";return false;
        }
        if(action.check_permissions){
            const auto permission=ts1_interaction_permission(world_.find(actor_id_),action.flags);
            if(permission==InteractionPermission::unsupported){
                diagnostic_="immediate interaction permissions require complete avatar person data";return false;
            }
            if(permission==InteractionPermission::denied){queue_.erase(found);continue;}
        }
        if(action.check){
            const auto result=vm_->evaluate_check_at_safe_point(*action.check,action.call.context,
                eligibility_arguments(action),10000,check_primitive_,action.argument_mode!=InteractionArgumentMode::legacy_metadata,check_primitive_factory_);
            if(!source_check_completed(result)){
                diagnostic_="immediate interaction check did not return synchronously: "+result.diagnostic;return false;
            }
            if(!validate())return false;
            if(source_check_false(result)){
                queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(index));continue;
            }
        }
        target=world_.find(action.call.context.callee);
        if(!target||target->generation!=action.target_generation||index>=queue_.size()||queue_[index].uid!=action.uid){
            diagnostic_="immediate interaction identity changed during eligibility check";return false;
        }
        // Allocate every queue mutation before publishing the VM child. The
        // parked controller is released only by the explicit host callback.
        auto candidate=queue_;
        candidate.erase(candidate.begin()+static_cast<std::ptrdiff_t>(index));
        candidate.insert(candidate.begin()+static_cast<std::ptrdiff_t>(protected_count),action);
        auto parents=suspended_active_;
        if(active_)parents.push_back(*active_);
        const auto ticket=vm_->inject_immediate(call,audit);
        if(!ticket){diagnostic_="immediate interaction VM injection rejected after eligibility check";return false;}
        const auto& continuation=vm_->immediate_continuations().back();
        const auto parent_ticket=continuation.parent_pending?continuation.parent_pending->ticket:0;
        try{
            Guard parking(immediate_parking_);
            if((parent_ticket&&!park_after_injection)||
               (park_after_injection&&!park_after_injection(parent_ticket))){
                diagnostic_="immediate interaction published but host could not park its parent service";return false;
            }
        }catch(...){
            diagnostic_="immediate interaction published but host parent park callback failed";return false;
        }
        if(wait_){diagnostic_="immediate interaction published with an unparked idle wait";return false;}
        queue_.swap(candidate);suspended_active_.swap(parents);
        active_=Active{action.uid,*ticket};idle_start_=0;
        if(const auto* actor=world_.find(actor_id_);actor&&actor->person_data.size()>33&&
           !world_.set_person_data(actor_id_,33,action.priority)){
            diagnostic_="immediate interaction published but priority write failed";return false;
        }
        diagnostic_.clear();return true;
    }
}
bool InteractionQueue::observe() {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter observe");
    fresh_=false;
    Guard guard(executing_);
    if(!validate())return false;
    const bool immediate=vm_->immediate_return().has_value();
    const auto normal=vm_->interaction_return();
    const auto urgent=vm_->immediate_return();
    if(!normal&&!urgent)return true;
    if(normal&&urgent){diagnostic_="multiple interaction return channels";return false;}
    const auto ticket=immediate?urgent->ticket:normal->ticket;
    const auto result_value=immediate?urgent->result:normal->result;
    const auto owner=immediate?urgent->code_owner:normal->code_owner;
    const auto run=immediate?urgent->run_generation:normal->run_generation;
    if(!immediate&&completion_&&completion_->ticket==ticket)return true;
    const auto active_index=suspended_active_.size();
    if(!active_||queue_.size()<=active_index||active_->uid!=queue_[active_index].uid||
       active_->ticket!=ticket||owner!=queue_[active_index].call.context.code_owner||run!=run_generation_||
       (queue_[active_index].call.return_mode==VmCallReturnMode::immediate)!=immediate) {
        diagnostic_="interaction completion identity mismatch";return false;
    }
    if(!immediate&&clock_==std::numeric_limits<std::uint32_t>::max()){
        diagnostic_="interaction resume exceeds uint32 clock";return false;
    }
    std::uint64_t parent_ticket=0;
    if(immediate){
        if(vm_->immediate_continuations().empty()||vm_->immediate_continuations().back().ticket!=ticket){
            diagnostic_="immediate completion lacks its exact parent continuation";return false;
        }
        const auto& parent=vm_->immediate_continuations().back().parent_pending;
        parent_ticket=parent?parent->ticket:0;
        if(parent_ticket&&!immediate_parent_resume_){
            diagnostic_="immediate completion needs its host parent-service resume handler";return false;
        }
    }
    const bool outermost=suspended_active_.empty();
    if(outermost){
        const auto* actor=world_.find(actor_id_);
        if(actor&&actor->person_data.size()>71&&!world_.set_person_data(actor_id_,71,0)){
            diagnostic_="interaction completion could not clear NonInterruptable";return false;
        }
    }
    if(outermost&&!world_.clear_motive_changes(actor_id_)){
        diagnostic_="interaction completion could not clear avatar motive changes";return false;
    }
    const auto restored_priority=outermost?std::int16_t{0}:
        queue_[active_index-1].priority;
    if(!outermost){
        const auto* actor=world_.find(actor_id_);
        if(actor&&actor->person_data.size()>33){
            auto threshold=restored_priority;
            if(threshold==2)--threshold;
            bool higher_pending=false;
            for(std::size_t n=active_index+1;n<queue_.size();++n){
                const auto* target=world_.resolve_reference({queue_[n].call.context.callee,queue_[n].target_generation});
                if(target&&target->generation==queue_[n].target_generation&&
                   queue_[n].priority>threshold){higher_pending=true;break;}
            }
            if(higher_pending&&!actor->object_words.count(8)){
                diagnostic_="queue priority change requires avatar ObjectData[8] flags";return false;
            }
        }
    }
    if(const auto* actor=world_.find(actor_id_);actor&&actor->person_data.size()>33&&
       !world_.set_person_data(actor_id_,33,restored_priority)){
        diagnostic_="interaction completion could not restore parent PersonData[33] priority";return false;
    }
    if(queue_[active_index].mode!=InteractionQueueMode::parent_idle){
        if(const auto* actor=world_.find(actor_id_);actor&&actor->object_words.count(8))
            world_.declare_object_word(actor_id_,8,
                word(static_cast<std::uint16_t>(actor->object_words.at(8))&~0x0080U));
    }
    completion_=InteractionCompletion{active_->uid,ticket,clock_,result_value};
    queue_.erase(queue_.begin()+static_cast<std::ptrdiff_t>(active_index));
    if(!suspended_active_.empty()){
        active_=suspended_active_.back();suspended_active_.pop_back();
    }else active_.reset();
    idle_start_=immediate?0:clock_;
    if(!immediate)resume_tick_=clock_+1;
    if(!evaluate_queue_priorities())return false;
    if(immediate){
        if(!vm_->resume_immediate(ticket)){
            diagnostic_="immediate completion could not acknowledge its VM continuation";return false;
        }
        try{
            Guard resuming(immediate_resuming_);
            if(immediate_parent_resume_&&!immediate_parent_resume_(parent_ticket)){
                diagnostic_="immediate completion restored VM but host parent-service retry failed";return false;
            }
        }catch(...){
            diagnostic_="immediate completion restored VM but host parent-service retry threw";return false;
        }
    }
    diagnostic_.clear();return true;
}
bool InteractionQueue::tick(std::uint64_t now,bool defer_wait) {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter tick");
    if(now>std::numeric_limits<std::uint32_t>::max() || (!last_tick_&&now>1) ||
       (last_tick_&&(now<=*last_tick_ || now-*last_tick_!=1)))return false;
    if(!observe())return false;
    last_tick_=now;clock_=now;
    return defer_wait||service_wait();
}
bool InteractionQueue::service_wait() {
    if(executing_)throw std::logic_error("interaction callbacks must not reenter wait service");
    Guard guard(executing_);
    if(!validate()||!evaluate_queue_priorities())return false;
    if(resume_tick_&&clock_>=*resume_tick_){
        if(!completion_||!vm_->resume_interaction(completion_->ticket))return false;
        resume_tick_.reset();idle_start_=0;
    }
    // EnqueueAction only marks QueueDirty. Neither ordinary nor immediate
    // arrival wakes a scheduled Sleep/IdleForInput before its source deadline.
    if(wait_&&(clock_>=wait_->deadline||interrupted_)){
        const auto ticket=wait_->ticket;
        if(!vm_->retry_pending(ticket)){diagnostic_="idle wait ticket stale";return false;}
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
    Ts1PushInteractionContextLookup contextual;
    if(lookup)contextual=[lookup=std::move(lookup)](const auto& request,const auto&,const auto&,const auto&){
        return lookup(request);
    };
    return ts1_push_interaction_primitive(world,std::move(contextual),std::move(resolve_thread));
}
VmPrimitive ts1_push_interaction_primitive(const World& world,Ts1PushInteractionContextLookup lookup,
        Ts1PushInteractionThread resolve_thread) {
    return [&world,lookup=std::move(lookup),resolve_thread=std::move(resolve_thread)](
        const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome {
        if(instruction.opcode!=13)return {};
        const auto& bytes=instruction.operands;
        // The source reads only bits 0,1,2,7; remaining bits have no effect.
        const auto& bank=(bytes[3]&2)!=0?frame.locals:frame.arguments;
        if(bytes[1]>=bank.size()||((bytes[3]&1)!=0&&bytes[4]>=frame.locals.size()))
            return unsupported("PushInteraction object/icon index outside its BHAV frame");
        InteractionQueueMode mode=InteractionQueueMode::normal;
        std::int16_t priority=1;
        switch(bytes[2]){
            case 0:{
                if(frame.action_tree){
                    const auto* caller=world.frame_entity(frame,VmEntityRole::caller);
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
            default:priority=0;break; // Source switch leaves unknown codes at zero/Normal.
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
            const auto action=lookup(request,frame,instruction,dispatch);
            if(!action)return {PrimitiveDisposition::goto_false,{}};
            const auto target=resolve_thread?resolve_thread(request.actor):Ts1PushInteractionTarget{};
            if(!target.vm||!target.queue)
                return unsupported("PushInteraction resident interaction thread is unavailable");
            if(!target.vm->has_routine(action->key,action->owner))
                return unsupported("PushInteraction recipient lacks the exact installed action code owner");
            const auto* icon=world.find(request.custom_icon.value_or(request.source));
            const auto icon_owner=icon?std::optional<InteractionIconOwnerSnapshot>{InteractionIconOwnerSnapshot{icon->id,icon->generation}}:std::nullopt;
            const VmContext context{request.actor,request.source,request.source,action->owner};
            if(!target.queue->enqueue_source_ts1(action->key,context,std::nullopt,request.priority,request.mode,
                    action->flags,request.push_head,request.push_tail,action->check,request.interaction,icon_owner,action->label))
                return unsupported("PushInteraction resident queue rejected action: "+target.queue->diagnostic());
            return {PrimitiveDisposition::goto_true,{}};
        } catch(const std::exception& error) {
            return unsupported(std::string("PushInteraction action could not be resolved: ")+error.what());
        }
    };
}

}
