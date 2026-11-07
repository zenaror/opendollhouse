#pragma once

#include "odh/entity_thread_controllers.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include "odh/routing.hpp"
#include "odh/session_bundle.hpp"

namespace odh_host {

// Tracks one resident Go Here command and its actual Destination. The injected
// factory owns construction/SetPosition; the object Main owns ordinary lifetime.
// Legacy restored UI markers alone retain their historical cleanup behavior.
class HouseholdGoHereSession {
public:
    HouseholdGoHereSession(odh::World& world,std::int16_t actor,odh::Vm& vm,
                           odh::EntityThreadControllers& controllers,odh::Routing& routing)
        :world_(world),actor_(actor),vm_(vm),queue_(controllers.interactions()),routing_(routing) {}
    HouseholdGoHereSession(const HouseholdGoHereSession&)=delete;
    HouseholdGoHereSession& operator=(const HouseholdGoHereSession&)=delete;
    ~HouseholdGoHereSession(){remove_marker();}

    struct ConstructedDestination {
        std::int16_t id=0;
        std::uint64_t generation=0;
        odh::CodeOwner owner=0;
        std::shared_ptr<const odh::VmProgram> program;
        std::optional<odh::Ttab> interactions;
    };
    struct Services {
        std::function<bool()> source_still_matches;
        std::function<std::optional<ConstructedDestination>(const odh::EntityPosition&,
            const std::function<bool()>&,std::string*)> construct;
        std::function<bool(std::int16_t,std::uint64_t,std::string*)> retire_failed_placement;
    };
    struct CreatedDestinationBinding {
        ConstructedDestination destination;
        odh::SessionCreatedObject origin;
        odh::SessionResourceCodeOwner resource_owner;
    };
    bool request(const odh::EntityPosition& destination,const Services& services,std::int16_t param0=0) {
        if(status_!="not_requested")return false;
        const auto* actor=world_.find(actor_);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->position||!queue_.bound_to(vm_)||vm_.dispatch_active()||
           !services.source_still_matches||!services.construct||!services.source_still_matches()){
            fail("Go Here needs its live actor and ordinary construction capability");return false;
        }
        const auto authority=queue_.snapshot();
        if(authority.actor_id!=actor_||authority.actor_generation!=actor->generation||authority.run_generation!=vm_.run_generation()){
            fail("Go Here resident VM/controller binding is stale");return false;
        }
        const auto queue_bytes=[&](){odh::EntityThreadControllersSnapshot state{};state.interactions=queue_.snapshot();
            return odh::encode_entity_thread_controllers_snapshot(state);};
        const auto initial_queue=queue_bytes();
        const auto actor_generation=actor->generation,run=vm_.run_generation();
        const auto floor=std::get<0>(world_.entity_identity_revision());
        const auto start=*actor->position;
        auto alive=std::make_shared<bool>(true);
        struct End {std::shared_ptr<bool> alive;~End(){*alive=false;}} end{alive};
        const auto current=[&,weak=std::weak_ptr<bool>(alive)](){
            const auto lease=weak.lock();if(!lease||!*lease||!services.source_still_matches())return false;
            const auto* live=world_.find(actor_);
            return live&&live->kind==odh::EntityKind::avatar&&live->generation==actor_generation&&
                live->position&&vm_.run_generation()==run&&queue_bytes()==initial_queue;
        };
        std::string error;std::optional<ConstructedDestination> created;
        try{created=services.construct(destination,current,&error);}catch(const std::exception& e){error=e.what();}
        if(!created||!current()){
            fail(error.empty()?"ordinary Go Here construction or source authority is unavailable":error);return false;
        }
        const auto* object=world_.find(created->id);
        if(!object||object->generation!=created->generation||created->generation<=floor||
           object->kind!=odh::EntityKind::object||object->ui_ghost||object->attachment||
           !object->definition||object->definition->guid!=0x7c4||!created->owner||!created->program||
           vm_.shared_program_for_owner(created->owner)!=created->program){
            fail("Go Here construction receipt does not bind the fresh ordinary object and installed bank");return false;
        }
        if(!object->position){
            if(!services.retire_failed_placement||!services.retire_failed_placement(created->id,created->generation,&error))
                fail(error.empty()?"Go Here rejected placement needs exact ordinary object deletion":error);
            else fail("Go Here placement was rejected; the deleted callee continuation is unsupported");
            return false;
        }
        if(!(*object->position==destination)){
            fail("Go Here factory did not publish the requested position and direction");return false;
        }
        const auto* row=go_here_row(*created,&error);
        if(!row){fail(error);return false;}
        const auto check=row->test?std::optional<odh::RoutineKey>{odh::routine_key(row->test)}:std::nullopt;
        if(!queue_.enqueue_source_user(odh::routine_key(row->action),{actor_,created->id,created->id,created->owner},
            std::vector<std::int16_t>{param0,0,0,0},row->flags,check,2)){
            fail(queue_.diagnostic().empty()?"ordinary Go Here queue admission failed":queue_.diagnostic());return false;
        }
        marker_id_=created->id;marker_generation_=created->generation;ordinary_destination_=true;
        command_uid_=queue_.last_enqueued_uid();
        destination_=destination;start_=start;owner_=created->owner;action_=row->action;
        previous_completion_=queue_.completion()?std::optional<std::uint64_t>{queue_.completion()->uid}:std::nullopt;
        status_="queued";route_status_="pending";diagnostic_.clear();return true;
    }

private:
    const odh::Interaction* go_here_row(const ConstructedDestination& created,std::string* error) const {
        const auto reject=[&](const char* why)->const odh::Interaction*{if(error)*error=why;return nullptr;};
        if(!created.interactions||created.interactions->status!=odh::TtabStatus::decoded||
           !created.interactions->version||*created.interactions->version<4||*created.interactions->version>9||
           (created.interactions->compressed&&*created.interactions->version!=9)||
           created.interactions->declared_count!=created.interactions->interactions.size())
            return reject("Go Here needs its exact decoded interaction table");
        const odh::Interaction* row=nullptr;
        for(const auto& entry:created.interactions->interactions)if(entry.label_index==2){
            if(row)return reject("Go Here semantic interaction number2 is ambiguous");row=&entry;
        }
        if(!row||row->action!=4096)return reject("Go Here semantic interaction number2 has no supported authored action");
        if(!created.program||!created.owner||vm_.shared_program_for_owner(created.owner)!=created.program||
           !vm_.has_routine(odh::routine_key(row->action),created.owner)||
           (row->test&&!vm_.has_routine(odh::routine_key(row->test),created.owner)))
            return reject("Go Here action or nonzero check is absent from its exact installed resource bank");
        return row;
    }

public:
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

    // Reattach command tracking at birth, before an authored routing primitive
    // has created a Routing job. Native queue validation authenticates all other
    // pending/protected actions; no check tree or Main is dispatched here.
    bool restore_created_pending(const CreatedDestinationBinding& binding) {
        if(status_!="not_requested")return false;
        const auto reject=[&](const char* why){diagnostic_=why;return false;};
        const auto* actor=world_.find(actor_);const auto& destination=binding.destination;
        const auto* object=world_.find(destination.id);
        if(!actor||actor->kind!=odh::EntityKind::avatar||!actor->position||!object||
           object->generation!=destination.generation||object->kind!=odh::EntityKind::object||
           object->ui_ghost||object->attachment||!object->position||!object->definition||
           object->definition->guid!=0x7c4||!created_binding_matches(binding))
            return reject("pending Go Here lacks its exact ordinary creation and installed-resource provenance");
        const auto q=queue_.snapshot();const auto vm=vm_.snapshot();
        if(q.actor_id!=actor_||q.actor_generation!=actor->generation||!vm.run_generation||
           q.run_generation!=vm.run_generation||q.interrupted)
            return reject("pending Go Here queue actor or VM run differs");
        odh::InteractionQueue validator(world_,actor_,q.capacity);
        if(!validator.bind(vm_)||!validator.restore_snapshot(q))
            return reject("pending Go Here queue does not match its actual saved VM continuation");
        const odh::InteractionActionSnapshot* command=nullptr;
        for(const auto& action:q.actions){
            if(action.call.context.callee!=destination.id||action.target_generation!=destination.generation||
               action.interaction_number!=std::optional<std::int32_t>{2})continue;
            if(command)return reject("pending Go Here command identity is ambiguous");command=&action;
        }
        std::string error;const auto* row=go_here_row(destination,&error);
        if(!command||!row)return reject("pending Go Here has no unique authored interaction number2");
        const auto& call=command->call;
        if(!command->uid||call.key.scope!=odh::RoutineScope::private_object||call.key.id!=4096||
           call.context.caller!=actor_||call.context.callee!=destination.id||call.context.stack_object!=destination.id||
           call.context.code_owner!=destination.owner||call.return_mode!=odh::VmCallReturnMode::interaction_next_tick||
           command->priority!=50||command->mode!=odh::InteractionQueueMode::normal||command->flags!=row->flags||
           !command->check_permissions||command->argument_mode!=odh::InteractionArgumentMode::source_explicit||
           !call.exact_arguments||call.arguments.size()!=4||call.arguments[1]||call.arguments[2]||call.arguments[3]||
           command->check.has_value()!=(row->test!=0)||
           (command->check&&(command->check->scope!=odh::routine_key(row->test).scope||command->check->id!=row->test)))
            return reject("pending Go Here action metadata differs from its exact source user command");
        std::optional<std::uint64_t> ticket;
        if(q.active&&q.active->uid==command->uid)ticket=q.active->ticket;
        for(const auto& active:q.suspended_active)if(active.uid==command->uid){
            if(ticket)return reject("pending Go Here repeats its protected action identity");ticket=active.ticket;
        }
        std::size_t matches=0;
        for(const auto& frame:vm.stack){
            if(!ticket||frame.invocation_ticket!=*ticket)continue;
            if(frame.key.scope!=call.key.scope||frame.key.id!=call.key.id||
               frame.context.caller!=actor_||frame.context.callee!=destination.id||
               frame.context.stack_object!=destination.id||frame.context.code_owner!=destination.owner||
               frame.return_mode!=call.return_mode||!frame.exact_arguments||frame.arguments.size()!=4)
                return reject("active pre-route Go Here frame differs from its protected queue action");
            ++matches;
        }
        if(ticket&&matches!=1)return reject("active pre-route Go Here lacks its unique actual action frame");
        if(ticket&&routing_.active())return reject("active Go Here already has a route and requires route-aware restoration");
        marker_id_=destination.id;marker_generation_=destination.generation;owner_=destination.owner;action_=4096;
        ordinary_destination_=true;command_uid_=command->uid;destination_=*object->position;start_=*actor->position;
        previous_completion_=q.completion?std::optional<std::uint64_t>{q.completion->uid}:std::nullopt;
        status_="queued";route_status_="pending";diagnostic_.clear();result_.reset();return true;
    }

    // Ordinary factory-created Destinations retain their own Main and history.
    // The caller supplies provenance already authenticated by session restore.
    bool restore_created(const odh::LocalRouteSnapshot& route,const CreatedDestinationBinding& binding) {
        const auto current=routing_.capture_session_route_stack(binding.destination.owner);
        if(!current||current->jobs.size()!=1)return false;
        const auto& actual=current->jobs.front();
        if(route.ticket!=actual.ticket||route.actor!=actual.actor||route.target!=actual.target||
           route.actor_generation!=actual.actor_generation||route.target_generation!=actual.target_generation||
           route.run_generation!=actual.run_generation||!(route.expected==actual.expected)||
           !(route.target_position==actual.target_position)||route.path!=actual.path||route.next!=actual.next)return false;
        return restore_created(*current,binding);
    }
    bool restore_created(const odh::RouteStackSnapshot& routes,const CreatedDestinationBinding& created) {
        odh::SessionDynamicActionOwner binding;
        binding.owner=created.destination.owner;binding.target_id=created.destination.id;
        binding.target_generation=created.destination.generation;binding.target_guid=created.origin.guid;
        binding.root_routine=4096;binding.linked_semiglobal=created.resource_owner.linked_semiglobal;
        return restore_stack(routes,binding,&created);
    }

    // Core Routing must already own this restored stack. The oldest job is
    // the Destination action; its expected pose can precede a nested door's
    // movement. Authenticate its suspended pending at its actual frame depth,
    // rather than treating the currently executing child as the root action.
    bool restore(const odh::RouteStackSnapshot& routes,
                 const odh::SessionDynamicActionOwner& binding) {
        return restore_stack(routes,binding,nullptr);
    }
private:
    bool created_binding_matches(const CreatedDestinationBinding& binding) const {
        const auto& object=binding.destination;const auto& origin=binding.origin;const auto& resource=binding.resource_owner;
        std::string error;
        return object.id>0&&object.generation&&origin.id==object.id&&origin.generation==object.generation&&
            origin.guid==0x7c4&&!origin.source.empty()&&resource.owner==object.owner&&
            resource.resource_guid==origin.guid&&resource.source==origin.source&&resource.asset==origin.asset&&
            resource.definition_chunk_id==origin.definition_chunk_id&&resource.tree_table_id&&
            resource.root_routine&&object.program&&vm_.shared_program_for_owner(object.owner)==object.program&&
            vm_.has_routine(odh::routine_key(resource.root_routine),object.owner)&&go_here_row(object,&error);
    }
    bool restore_stack(const odh::RouteStackSnapshot& routes,const odh::SessionDynamicActionOwner& binding,
                       const CreatedDestinationBinding* created) {
        if(status_!="not_requested")return false;
        const auto reject=[&](){diagnostic_="restored resident Go Here stack, marker, owner, VM continuation or active action does not match";return false;};
        if(routes.jobs.empty())return reject(); // A portal cache alone is not Go Here.
        const auto same_context=[](const odh::VmContext& a,const odh::VmContext& b){
            return a.caller==b.caller&&a.callee==b.callee&&a.stack_object==b.stack_object&&a.code_owner==b.code_owner;
        };
        const auto same_key=[](const odh::RoutineKey& a,const odh::RoutineKey& b){return a.scope==b.scope&&a.id==b.id;};
        const auto same_frame=[&](const odh::VmFrame& a,const odh::VmFrame& b){
            return same_key(a.key,b.key)&&a.pc==b.pc&&same_context(a.context,b.context)&&a.arguments==b.arguments&&
                a.locals==b.locals&&a.return_mode==b.return_mode&&a.invocation_ticket==b.invocation_ticket&&a.exact_arguments==b.exact_arguments;
        };
        const auto same_pending=[&](const odh::VmPending& a,const odh::VmPending& b){
            const auto& x=a.instruction;const auto& y=b.instruction;
            return a.ticket==b.ticket&&same_context(a.context,b.context)&&x.step==y.step&&x.depth==y.depth&&
                same_key(x.key,y.key)&&x.pc==y.pc&&x.code_owner==y.code_owner&&
                x.instruction.opcode==y.instruction.opcode&&x.instruction.on_true==y.instruction.on_true&&
                x.instruction.on_false==y.instruction.on_false&&x.instruction.operands==y.instruction.operands;
        };
        const auto current=routing_.capture_session_route_stack(binding.owner);
        if(!current||current->jobs.size()!=routes.jobs.size()||current->last_tick!=routes.last_tick)return reject();
        for(std::size_t n=0;n<routes.jobs.size();++n){
            const auto& a=routes.jobs[n];const auto& b=current->jobs[n];
            if(a.ticket!=b.ticket||a.actor!=b.actor||a.target!=b.target||a.actor_generation!=b.actor_generation||
               a.target_generation!=b.target_generation||a.run_generation!=b.run_generation||
               !(a.expected==b.expected)||!(a.target_position==b.target_position)||
               !same_pending(a.pending,b.pending)||!same_frame(a.route_frame,b.route_frame))return reject();
        }
        const auto& root=routes.jobs.front();
        const auto* actor=world_.find(actor_);const auto* marker=world_.find(root.target);
        const auto q=queue_.snapshot();const auto vm=vm_.snapshot();
        const auto active=std::find_if(q.actions.begin(),q.actions.end(),[&](const auto& action){
            return q.active&&action.uid==q.active->uid;
        });
        const auto context_matches=[&](const odh::VmContext& c){
            return c.caller==actor_&&c.callee==root.target&&c.stack_object==root.target&&c.code_owner==binding.owner;
        };
        const odh::VmPending* pending=nullptr;
        if(vm.pending&&vm.pending->ticket==root.ticket)pending=&*vm.pending;
        for(const auto& item:vm.suspended_pending)if(item.ticket==root.ticket){if(pending)return reject();pending=&item;}
        if(!actor||actor->kind!=odh::EntityKind::avatar||actor->generation!=root.actor_generation||!actor->position||
           root.actor!=actor_||root.target<=0||root.target==actor_||!marker||marker->generation!=root.target_generation||
           marker->kind!=odh::EntityKind::object||!marker->definition||marker->definition->guid!=0x7c4||
           (!created&&(!marker->ui_ghost||marker->static_footprint))||
           (created&&(marker->ui_ghost||!created_binding_matches(*created)))||
           !marker->position||!(*marker->position==root.target_position)||
           !binding.owner||binding.target_id!=root.target||binding.target_generation!=root.target_generation||
           binding.target_guid!=0x7c4||binding.root_routine!=4096||(!created&&binding.linked_semiglobal)||!root.ticket||
           !root.call_failure_trees||root.entry_flags!=0||root.run_generation!=vm.run_generation||
           !pending||!same_pending(*pending,root.pending)||!pending->instruction.depth||
           pending->instruction.depth>vm.stack.size()||!same_frame(vm.stack[pending->instruction.depth-1],root.route_frame)||
           pending->instruction.instruction.opcode!=27||
           pending->instruction.instruction.operands!=std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0}||
           !context_matches(pending->context)||!context_matches(root.route_frame.context)||
           q.actor_id!=actor_||q.actor_generation!=root.actor_generation||q.run_generation!=root.run_generation||
           (!created&&q.actions.size()!=1)||!q.active||q.wait||q.interrupted||!q.suspended_active.empty()||
           !q.active->uid||active==q.actions.end()||!q.active->ticket||
           !same_key(active->call.key,odh::routine_key(4096))||
           !context_matches(active->call.context)||active->target_generation!=root.target_generation)
            return reject();
        const auto& call=active->call;
        if(created){
            std::string error;const auto* row=go_here_row(created->destination,&error);
            const auto& action=*active;
            if(!row||action.interaction_number!=std::optional<std::int32_t>{2}||action.priority!=50||
               action.mode!=odh::InteractionQueueMode::normal||action.flags!=row->flags||!action.check_permissions||
               action.argument_mode!=odh::InteractionArgumentMode::source_explicit||!call.exact_arguments||
               call.arguments.size()!=4||call.arguments[1]||call.arguments[2]||call.arguments[3]||
               action.check.has_value()!=(row->test!=0)||
               (action.check&&!same_key(*action.check,odh::routine_key(row->test))))return reject();
        }
        std::size_t action_frames=0;
        for(std::size_t n=0;n<pending->instruction.depth;++n){const auto& frame=vm.stack[n];
            if(frame.invocation_ticket!=q.active->ticket)continue;
            if(!same_key(frame.key,call.key)||!same_context(frame.context,call.context)||
               frame.return_mode!=call.return_mode||frame.exact_arguments!=call.exact_arguments||
               call.return_mode!=odh::VmCallReturnMode::interaction_next_tick)return reject();
            ++action_frames;
        }
        if(action_frames!=1)return reject();
        ordinary_destination_=created!=nullptr;
        command_uid_=created?std::optional<std::uint64_t>{active->uid}:std::nullopt;
        marker_id_=root.target;marker_generation_=root.target_generation;
        destination_=root.target_position;start_=actor->position;owner_=binding.owner;action_=binding.root_routine;
        previous_completion_=q.completion?std::optional<std::uint64_t>{q.completion->uid}:std::nullopt;
        status_="queued";route_status_="pending";diagnostic_.clear();result_.reset();return true;
    }

public:
    void observe(bool host_failed=false,const std::string& host_diagnostic={}) {
        if(status_!="queued")return;
        if(queue_.completion()&&(ordinary_destination_?
            (command_uid_&&queue_.completion()->uid==*command_uid_):
            (!previous_completion_||queue_.completion()->uid!=*previous_completion_))){
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
    std::optional<std::uint64_t> command_uid()const noexcept{return command_uid_;}
    std::optional<bool> result()const noexcept{return result_;}
    std::optional<odh::EntityPosition> destination()const noexcept{return destination_;}
    std::optional<odh::EntityPosition> start()const noexcept{return start_;}
    bool marker_alive()const noexcept{
        const auto* marker=marker_id_?world_.find(*marker_id_):nullptr;
        return marker&&marker_generation_&&marker->generation==*marker_generation_;
    }

private:
    void remove_marker()noexcept{
        if(ordinary_destination_||!marker_id_||!marker_generation_)return;
        const auto* marker=world_.find(*marker_id_);
        if(marker&&marker->generation==*marker_generation_)world_.remove(*marker_id_);
    }
    bool ordinary_destination_=false;
    odh::World& world_;std::int16_t actor_;odh::Vm& vm_;
    odh::InteractionQueue& queue_;odh::Routing& routing_;
    std::string status_="not_requested",route_status_="not_requested",diagnostic_;
    std::optional<std::int16_t> marker_id_;std::optional<std::uint64_t> marker_generation_;
    std::optional<std::uint64_t> previous_completion_,command_uid_;
    std::optional<odh::CodeOwner> owner_;std::optional<std::uint16_t> action_;
    std::optional<odh::EntityPosition> destination_,start_;std::optional<bool> result_;
};

} // namespace odh_host
