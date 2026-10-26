#pragma once
#include "odh/interaction.hpp"
#include <iterator>
#include <limits>
#include <map>
#include <set>

namespace odh {

struct UserInteractionIdentity {
    std::int16_t actor=0;
    std::uint64_t actor_generation=0,run_generation=0,uid=0;
    bool operator==(const UserInteractionIdentity& other) const {
        return actor==other.actor&&actor_generation==other.actor_generation&&
            run_generation==other.run_generation&&uid==other.uid;
    }
};
enum class UserInteractionState : std::uint8_t {
    pending,active,cancel_requested,completed_true,completed_false,cancelled,dropped,invalidated
};
inline const char* user_interaction_state_name(UserInteractionState state) {
    switch(state){
    case UserInteractionState::pending:return "pending";
    case UserInteractionState::active:return "active";
    case UserInteractionState::cancel_requested:return "cancel_requested";
    case UserInteractionState::completed_true:return "completed_true";
    case UserInteractionState::completed_false:return "completed_false";
    case UserInteractionState::cancelled:return "cancelled";
    case UserInteractionState::dropped:return "dropped";
    case UserInteractionState::invalidated:return "invalidated";
    }
    return "invalid";
}
inline bool user_interaction_terminal(UserInteractionState state) {
    return state!=UserInteractionState::pending&&state!=UserInteractionState::active&&
        state!=UserInteractionState::cancel_requested;
}
struct UserInteractionRequest {
    UserInteractionIdentity identity;
    InteractionActionSnapshot action{};
    UserInteractionState state=UserInteractionState::pending;
    std::optional<bool> result;
    bool cancellation_requested=false;
    std::optional<std::uint64_t> invocation_ticket;
    std::size_t queue_position=std::numeric_limits<std::size_t>::max();
};

// Presentation-only observer. The native queue is authoritative; this model
// neither changes simulation state nor invents completion for disappeared work.
// Rebuild after load by observing current queues: only check_permissions actions
// are known player requests. Terminal history is deliberately not save data.
// Observe at external safe points after controller observation, at least once
// per tick. Clear on an in-process rewind/lot replacement with reused epochs.
class UserInteractionTracker {
public:
    explicit UserInteractionTracker(std::size_t live_limit=256,std::size_t terminal_limit=64)
        :live_limit_(live_limit),terminal_limit_(terminal_limit) {
        if(!live_limit||live_limit>4096||terminal_limit>4096)
            throw std::invalid_argument("user interaction tracker limits outside 1..4096 live / 0..4096 terminal");
    }
    // References/pointers are valid until the next successful mutation or clear.
    const std::deque<UserInteractionRequest>& records() const {return records_;}
    const std::string& diagnostic() const {return diagnostic_;}
    void clear(){records_.clear();epochs_.clear();diagnostic_.clear();}
    const UserInteractionRequest* find(const UserInteractionIdentity& identity) const {
        for(const auto& request:records_)if(request.identity==identity)return &request;
        return nullptr;
    }
    // Convenience for a currently observed actor. Never falls back to another
    // actor generation or Main run that happened to use the same native UID.
    const UserInteractionRequest* current(std::int16_t actor,std::uint64_t uid) const {
        const auto epoch=epochs_.find(actor);
        if(epoch==epochs_.end())return nullptr;
        return find({actor,epoch->second.actor_generation,epoch->second.run_generation,uid});
    }
    // Current outstanding requests in native queue order, not discovery order.
    std::vector<const UserInteractionRequest*> pending(std::int16_t actor) const {
        std::vector<const UserInteractionRequest*> result;
        const auto epoch=epochs_.find(actor);if(epoch==epochs_.end())return result;
        for(const auto& request:records_)
            if(request.identity.actor==actor&&request.identity.actor_generation==epoch->second.actor_generation&&
               request.identity.run_generation==epoch->second.run_generation&&!user_interaction_terminal(request.state))
                result.push_back(&request);
        std::sort(result.begin(),result.end(),[](const auto* a,const auto* b){return a->queue_position<b->queue_position;});
        return result;
    }
    // Feed only the actual result of cancel() for this full identity. A signal
    // is not a false completion; only an accepted removal proves cancellation.
    bool note_cancel(const UserInteractionIdentity& identity,InteractionCancelResult result) {
        auto next=records_;
        auto item=std::find_if(next.begin(),next.end(),[&](const auto& request){return request.identity==identity;});
        if(item==next.end()||user_interaction_terminal(item->state)||
           (result!=InteractionCancelResult::removed&&result!=InteractionCancelResult::notified))return false;
        item->cancellation_requested=true;
        item->state=result==InteractionCancelResult::removed?UserInteractionState::cancelled:UserInteractionState::cancel_requested;
        if(result==InteractionCancelResult::removed){
            item->queue_position=absent_position;
            auto finished=std::move(*item);next.erase(item);next.push_back(std::move(finished));
        }
        trim(next);records_.swap(next);prune_epochs();diagnostic_.clear();return true;
    }
    // Invalid input/capacity leaves all records/epochs unchanged. Allocation
    // failures throw without publication. No pending request is evicted to fit.
    bool observe(const InteractionQueueSnapshot& queue) {
        const auto reject=[&](const char* why){diagnostic_=why;return false;};
        if(queue.actor_id<=0||!queue.actor_generation||!queue.run_generation||
           queue.actions.size()>queue.capacity||queue.suspended_active.size()+(queue.active?1U:0U)>queue.actions.size()||
           (!queue.active&&!queue.suspended_active.empty()))return reject("invalid player queue actor or protected prefix");
        const auto epoch=epochs_.find(queue.actor_id);
        if(epoch!=epochs_.end()&&epoch->second.actor_generation==queue.actor_generation&&
           epoch->second.run_generation==queue.run_generation&&queue.clock<epoch->second.clock)
            return reject("player queue clock moved backward; reset tracker before loading an earlier session");
        std::set<std::uint64_t> present;
        std::map<std::uint64_t,std::uint64_t> active;
        for(std::size_t n=0;n<queue.suspended_active.size();++n){
            const auto& parent=queue.suspended_active[n];
            if(!parent.ticket||parent.uid!=queue.actions[n].uid||!active.emplace(parent.uid,parent.ticket).second)
                return reject("invalid protected player queue parent");
        }
        if(queue.active&&(!queue.active->ticket||queue.active->uid!=queue.actions[queue.suspended_active.size()].uid||
            !active.emplace(queue.active->uid,queue.active->ticket).second))return reject("invalid active player queue identity");
        for(const auto& action:queue.actions){
            if(!action.uid||action.uid>queue.sequence||!present.insert(action.uid).second)
                return reject("invalid or duplicate native player queue identity");
            if(action.check_permissions&&(!action.target_generation||action.call.context.caller!=queue.actor_id||
               action.call.context.callee<=0||action.call.context.stack_object!=action.call.context.callee||
               action.call.key.scope!=routine_key(action.call.key.id).scope||
               action.call.return_mode!=VmCallReturnMode::interaction_next_tick))
                return reject("invalid player request target or routine metadata");
        }
        if(queue.completion&&(!queue.completion->uid||queue.completion->uid>queue.sequence||
           !queue.completion->ticket||queue.completion->tick>queue.clock||present.count(queue.completion->uid)))
            return reject("invalid player completion metadata");
        auto next=records_;auto epochs=epochs_;
        std::vector<UserInteractionIdentity> terminal_transitions;
        for(auto& request:next){
            if(request.identity.actor!=queue.actor_id||user_interaction_terminal(request.state))continue;
            if(request.identity.actor_generation!=queue.actor_generation||request.identity.run_generation!=queue.run_generation){
                request.state=UserInteractionState::invalidated;request.result.reset();request.queue_position=absent_position;
                terminal_transitions.push_back(request.identity);continue;
            }
            // Completion is stronger than target liveness: cleanup may already
            // have removed the callee. Other/child completions never match UID.
            if(queue.completion&&queue.completion->uid==request.identity.uid){
                if(request.invocation_ticket&&*request.invocation_ticket!=queue.completion->ticket)
                    return reject("player completion invocation ticket changed");
                request.result=queue.completion->result;
                request.state=*request.result?UserInteractionState::completed_true:UserInteractionState::completed_false;
                request.queue_position=absent_position;
            }else if(!present.count(request.identity.uid)){
                request.state=UserInteractionState::dropped;request.result.reset();request.queue_position=absent_position;
            }
            if(user_interaction_terminal(request.state))terminal_transitions.push_back(request.identity);
        }
        for(std::size_t position=0;position<queue.actions.size();++position){
            const auto& action=queue.actions[position];
            const UserInteractionIdentity identity{queue.actor_id,queue.actor_generation,queue.run_generation,action.uid};
            auto item=std::find_if(next.begin(),next.end(),[&](const auto& request){return request.identity==identity;});
            if(!action.check_permissions){
                if(item!=next.end())return reject("known player request lost its player origin metadata");
                continue;
            }
            if(item==next.end()){
                next.push_back({identity,action});item=std::prev(next.end());
            }else if(user_interaction_terminal(item->state)||!same_call(item->action,action)){
                return reject("native player identity reappeared or changed its invocation metadata");
            }
            item->action=action;item->queue_position=position;
            const auto executing=active.find(action.uid);
            if(executing!=active.end()){
                if(item->invocation_ticket&&*item->invocation_ticket!=executing->second)
                    return reject("player action invocation ticket changed");
                item->invocation_ticket=executing->second;
            }
            item->state=item->cancellation_requested?UserInteractionState::cancel_requested:
                executing!=active.end()?UserInteractionState::active:UserInteractionState::pending;
        }
        const auto live=std::count_if(next.begin(),next.end(),[](const auto& request){return !user_interaction_terminal(request.state);});
        if(static_cast<std::size_t>(live)>live_limit_)return reject("player request tracker live capacity exceeded");
        // Pending discovery age is unrelated to when it finishes. Fresh
        // terminal transitions must survive a full history long enough for
        // presentation adapters (for example Stand) to consume that result.
        move_terminals_to_back(next,terminal_transitions);
        trim(next);
        if(std::any_of(next.begin(),next.end(),[&](const auto& request){return request.identity.actor==queue.actor_id;}))
            epochs.insert_or_assign(queue.actor_id,Epoch{queue.actor_generation,queue.run_generation,queue.clock});
        records_.swap(next);epochs_.swap(epochs);prune_epochs();diagnostic_.clear();return true;
    }
private:
    static constexpr auto absent_position=std::numeric_limits<std::size_t>::max();
    struct Epoch {std::uint64_t actor_generation,run_generation,clock;};
    std::size_t live_limit_,terminal_limit_;
    std::deque<UserInteractionRequest> records_;
    std::map<std::int16_t,Epoch> epochs_;
    std::string diagnostic_;
    static bool same_call(const InteractionActionSnapshot& a,const InteractionActionSnapshot& b){
        const auto& x=a.call;const auto& y=b.call;
        return a.target_generation==b.target_generation&&x.key.scope==y.key.scope&&x.key.id==y.key.id&&
            x.context.caller==y.context.caller&&x.context.callee==y.context.callee&&
            x.context.stack_object==y.context.stack_object&&x.context.code_owner==y.context.code_owner&&
            x.return_mode==y.return_mode&&x.arguments==y.arguments;
    }
    static void move_terminals_to_back(std::deque<UserInteractionRequest>& records,
        const std::vector<UserInteractionIdentity>& transitions){
        for(const auto& identity:transitions){
            const auto item=std::find_if(records.begin(),records.end(),[&](const auto& request){return request.identity==identity;});
            if(item==records.end())continue;
            auto finished=std::move(*item);records.erase(item);records.push_back(std::move(finished));
        }
    }
    void trim(std::deque<UserInteractionRequest>& records) const {
        auto count=static_cast<std::size_t>(std::count_if(records.begin(),records.end(),[](const auto& request){return user_interaction_terminal(request.state);}));
        for(auto it=records.begin();it!=records.end()&&count>terminal_limit_;){
            if(user_interaction_terminal(it->state)){it=records.erase(it);--count;}else ++it;
        }
    }
    void prune_epochs(){
        for(auto it=epochs_.begin();it!=epochs_.end();)
            if(std::none_of(records_.begin(),records_.end(),[&](const auto& request){return request.identity.actor==it->first;}))it=epochs_.erase(it);
            else ++it;
    }
};
} // namespace odh
