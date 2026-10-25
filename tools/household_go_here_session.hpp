#pragma once

#include "odh/entity_thread_controllers.hpp"
#include "odh/routing.hpp"
#include "odh/session_bundle.hpp"

namespace odh_host {

// One temporary Destination action owned by one household resident VM.
// Spatial validation and route-grid refresh remain host callbacks on that
// resident's Routing controller.
class HouseholdGoHereSession {
public:
    HouseholdGoHereSession(odh::World& world,std::int16_t actor,odh::Vm& vm,
                           odh::EntityThreadControllers& controllers,odh::Routing& routing)
        :world_(world),actor_(actor),vm_(vm),queue_(controllers.interactions()),routing_(routing) {}
    HouseholdGoHereSession(const HouseholdGoHereSession&)=delete;
    HouseholdGoHereSession& operator=(const HouseholdGoHereSession&)=delete;
    ~HouseholdGoHereSession(){remove_marker();}

    bool request(const odh::EntityPosition& destination,odh::CodeOwner owner,
                 odh::RoutineSet routines,std::uint16_t action=4096) {
        if(status_!="not_requested")return false;
        const auto* actor=world_.find(actor_);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->position||
           queue_.size()!=0||!owner||!routines.count(odh::routine_key(action))) {
            fail("resident Go Here request identity, queue, or action is unavailable");return false;
        }
        std::int16_t candidate=0;
        for(std::int32_t id=32767;id>0;--id)
            if(!world_.find(static_cast<std::int16_t>(id))){candidate=static_cast<std::int16_t>(id);break;}
        if(!candidate){fail("no free World identity is available for the resident Destination marker");return false;}
        if(!world_.create(candidate,odh::EntityKind::object)||!world_.define_type(candidate,0x7c4)){
            if(world_.find(candidate))world_.remove(candidate);
            fail("resident Destination marker construction failed");return false;
        }
        bool marker_ready=true;
        for(std::int16_t word=0;word<=78;++word){
            if(word==1||word==2||word==3||word==11||word==67)continue;
            const auto value=word==8?static_cast<std::int16_t>(256):std::int16_t{0};
            if(!world_.declare_object_word(candidate,word,value)){marker_ready=false;break;}
        }
        marker_ready=marker_ready&&world_.set_position(candidate,destination)&&
            world_.set_static_footprint(candidate,false)&&world_.set_ui_ghost(candidate,true);
        if(!marker_ready){world_.remove(candidate);fail("resident Destination marker initialization failed");return false;}
        if(!vm_.install_owner(owner,std::move(routines))){
            world_.remove(candidate);fail("resident VM rejected its Go Here code owner");return false;
        }
        const auto* marker=world_.find(candidate);
        if(!marker||!queue_.enqueue(odh::routine_key(action),{actor_,candidate,candidate,owner},{0,0,0,0})){
            world_.remove(candidate);fail(queue_.diagnostic().empty()?"resident Go Here action enqueue failed":queue_.diagnostic());
            return false;
        }
        marker_id_=candidate;marker_generation_=marker->generation;
        destination_=destination;start_=actor->position;owner_=owner;action_=action;
        previous_completion_=queue_.completion()?std::optional<std::uint64_t>{queue_.completion()->uid}:std::nullopt;
        status_="queued";route_status_="pending";diagnostic_.clear();return true;
    }

    // Rebind host-side Go Here bookkeeping to a route, marker, VM and action
    // queue which have already been restored from a session. This is strictly
    // observational: every check completes before this object takes ownership
    // of the marker, so rejection leaves the restored World and VM untouched.
    bool restore(const odh::LocalRouteSnapshot& route,
                 const odh::SessionDynamicActionOwner& binding) {
        if(status_!="not_requested")return false;
        const auto* actor=world_.find(actor_);
        const auto* marker=world_.find(route.target);
        const auto& q=queue_.snapshot();
        const auto& pending=vm_.pending();
        const auto& stack=vm_.stack();
        const auto context_matches=[&](const odh::VmContext& c) {
            return c.caller==actor_&&c.callee==route.target&&c.stack_object==route.target&&
                c.code_owner==binding.owner;
        };
        const bool valid=actor&&actor->kind==odh::EntityKind::avatar&&actor->generation==route.actor_generation&&
           actor->position&&*actor->position==route.expected&&route.actor==actor_&&
           route.target>0&&route.target!=actor_&&marker&&marker->generation==route.target_generation&&
           marker->kind==odh::EntityKind::object&&marker->definition&&marker->definition->guid==0x7c4&&
           marker->ui_ghost&&!marker->static_footprint&&marker->position&&
           *marker->position==route.target_position&&binding.owner!=0&&binding.target_id==route.target&&
           binding.target_generation==route.target_generation&&binding.target_guid==0x7c4&&
           binding.root_routine==4096&&!binding.linked_semiglobal&&route.ticket!=0&&
           route.call_failure_trees&&route.entry_flags==0&&route.same_room_verified&&routing_.active()&&
           route.run_generation==vm_.run_generation()&&
           vm_.status()==odh::VmStatus::waiting&&pending&&pending->ticket==route.ticket&&
           pending->instruction.instruction.opcode==27&&
           pending->instruction.instruction.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0}&&
           context_matches(pending->context)&&!stack.empty()&&context_matches(stack.back().context)&&
           q.actor_id==actor_&&q.actions.size()==1&&q.active&&!q.wait&&!q.interrupted&&
           q.actions.front().uid==q.active->uid&&q.active->ticket!=0&&
           q.actions.front().call.key.id==binding.root_routine&&
           q.actions.front().call.key.scope==odh::routine_key(binding.root_routine).scope&&
           context_matches(q.actions.front().call.context)&&
           q.actions.front().target_generation==route.target_generation;
        if(!valid){diagnostic_="restored resident Go Here route, marker, owner, VM ticket or active action does not match";return false;}
        marker_id_=route.target;marker_generation_=route.target_generation;
        destination_=route.target_position;start_=actor->position;owner_=binding.owner;
        action_=binding.root_routine;
        previous_completion_=q.completion?std::optional<std::uint64_t>{q.completion->uid}:std::nullopt;
        status_="queued";route_status_="pending";diagnostic_.clear();result_.reset();
        return true;
    }

    void observe(bool host_failed=false,const std::string& host_diagnostic={}) {
        if(status_!="queued")return;
        if(queue_.completion()&&(!previous_completion_||queue_.completion()->uid!=*previous_completion_)){
            result_=queue_.completion()->result;status_=*result_?"returned_true":"returned_false";
            const auto& route_diagnostic=routing_.diagnostic();
            route_status_=route_diagnostic.empty()?"arrived":
                route_diagnostic=="route interrupted by player"?"cancelled":"failed";
            diagnostic_.clear();remove_marker();
        }else if(vm_.status()==odh::VmStatus::fault){
            status_="vm_fault";diagnostic_=vm_.diagnostic();remove_marker();
        }else if(host_failed){
            status_="host_failed";diagnostic_=host_diagnostic;remove_marker();
        }
    }
    void fail(const std::string& reason){status_="unsupported";diagnostic_=reason;remove_marker();}
    void finish_incomplete(const std::string& status,const std::string& diagnostic) {
        if(status_!="queued")return;
        status_=status;route_status_=status=="window_closed"?"interrupted":"incomplete";
        diagnostic_=diagnostic;remove_marker();
    }
    const std::string& status()const noexcept{return status_;}
    const std::string& route_status()const noexcept{return route_status_;}
    const std::string& diagnostic()const noexcept{return diagnostic_;}
    std::optional<std::int16_t> marker_id()const noexcept{return marker_id_;}
    std::optional<std::uint64_t> marker_generation()const noexcept{return marker_generation_;}
    std::optional<odh::CodeOwner> owner()const noexcept{return owner_;}
    std::optional<bool> result()const noexcept{return result_;}
    std::optional<odh::EntityPosition> destination()const noexcept{return destination_;}
    std::optional<odh::EntityPosition> start()const noexcept{return start_;}
    bool marker_alive()const noexcept{
        const auto* marker=marker_id_?world_.find(*marker_id_):nullptr;
        return marker&&marker_generation_&&marker->generation==*marker_generation_;
    }

private:
    void remove_marker()noexcept{
        if(!marker_id_||!marker_generation_)return;
        const auto* marker=world_.find(*marker_id_);
        if(marker&&marker->generation==*marker_generation_)world_.remove(*marker_id_);
    }
    odh::World& world_;std::int16_t actor_;odh::Vm& vm_;
    odh::InteractionQueue& queue_;odh::Routing& routing_;
    std::string status_="not_requested",route_status_="not_requested",diagnostic_;
    std::optional<std::int16_t> marker_id_;std::optional<std::uint64_t> marker_generation_;
    std::optional<std::uint64_t> previous_completion_;
    std::optional<odh::CodeOwner> owner_;std::optional<std::uint16_t> action_;
    std::optional<odh::EntityPosition> destination_,start_;std::optional<bool> result_;
};

} // namespace odh_host
