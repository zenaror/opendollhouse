#include "odh/avatar_containment.hpp"
#include "odh/avatar_person_data.hpp"
#include "odh/single_resident_session_restore.hpp"
#include "odh/seated_session_continuation.hpp"
#include <algorithm>

namespace odh {
namespace {
bool same_definition(const std::optional<EntityType>& a,const std::optional<EntityType>& b) {
    return a.has_value()==b.has_value()&&(!a||(a->guid==b->guid&&a->master_guid==b->master_guid));
}
bool same_entry_points(const std::optional<std::vector<EntryPoint>>& a,
                       const std::optional<std::vector<EntryPoint>>& b) {
    if(a.has_value()!=b.has_value())return false;
    return !a||(a->size()==b->size()&&std::equal(a->begin(),a->end(),b->begin(),
        [](const auto& x,const auto& y){return x.condition==y.condition&&x.action==y.action;}));
}
bool same_banks(const std::optional<SavedObjectBanks>& a,const std::optional<SavedObjectBanks>& b) {
    return a.has_value()==b.has_value()&&(!a||(a->object_data==b->object_data&&a->extras==b->extras));
}
// The bundle decoder certifies full stack/frame links before these profile
// checks. Cache-only stacks deliberately grant no execution permissions.
const LocalRouteSnapshot* root_route(const SessionRuntimeEntity& state) {
    if(state.route)return &*state.route;
    return state.route_stack&&!state.route_stack->jobs.empty()?&state.route_stack->jobs.front():nullptr;
}
bool has_route(const SessionRuntimeEntity& state) {return root_route(state)!=nullptr;}
bool route_walking_started(const SessionRuntimeEntity& state) {
    if(state.route)return state.route->walking_started;
    return state.route_stack&&std::any_of(state.route_stack->jobs.begin(),state.route_stack->jobs.end(),
        [](const auto& job){return job.walking_started;});
}
bool owns_route_pending(const SessionRuntimeEntity& state,const VmPending& pending) {
    return state.route_stack&&std::any_of(state.route_stack->jobs.begin(),state.route_stack->jobs.end(),
        [&](const auto& job){return job.ticket==pending.ticket&&job.pending.ticket==pending.ticket&&
            job.pending.instruction.depth==pending.instruction.depth;});
}
bool owns_route_external(const SessionRuntimeEntity& state,std::uint64_t ticket,CodeOwner owner) {
    return state.route_stack&&std::any_of(state.route_stack->jobs.begin(),state.route_stack->jobs.end(),
        [&](const auto& job){return job.external_frame_active&&job.ticket==ticket&&job.external_frame_owner==owner;});
}
void validate_baseline(const SessionBundle& bundle,const WorldSnapshot& original) {
    if(bundle.world.attribute_policy!=original.attribute_policy)
        throw FormatError("session World attribute policy differs from the original lot");
    const Entity* route_marker=nullptr;
    for(const auto& runtime:bundle.runtime_entities)if(const auto* route=root_route(runtime)){
        for(const auto& owner:runtime.dynamic_action_owners){
            if(owner.target_id!=route->target||owner.target_guid!=0x7c4||owner.root_routine!=4096)continue;
            const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
                [&](const auto& entity){return entity.id==owner.target_id;});
            if(route_marker||found==bundle.world.entities.end()||found->kind!=EntityKind::object||
               !found->definition||found->definition->guid!=0x7c4||found->generation!=owner.target_generation||
               !found->ui_ghost||found->static_footprint||!found->position||
               !(*found->position==route->target_position))
                throw FormatError("saved Go Here route marker is not the exact temporary Destination object");
            route_marker=&*found;
        }
    }
    if(bundle.world.entities.size()!=original.entities.size()+(route_marker?1u:0u))
        throw FormatError("session entity roster differs from the original lot and optional Go Here marker");
    for(const auto& b:original.entities) {
        const auto found_entity=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& entity){return entity.id==b.id;});
        if(found_entity==bundle.world.entities.end())throw FormatError("session entity roster lost an original lot entity");
        const auto& a=*found_entity;
        const auto route=std::find_if(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
            [&](const auto& runtime){return runtime.id==a.id&&has_route(runtime);});
        const bool route_actor=route!=bundle.runtime_entities.end();
        const bool route_footprint_change=route_actor&&b.static_footprint&&
            !a.static_footprint&&route_walking_started(*route);
        std::string changed;
        const auto mark=[&](const char* name){if(!changed.empty())changed+=',';changed+=name;};
        if(a.id!=b.id)mark("id");
        if(a.kind!=b.kind)mark("kind");
        if(a.generation!=b.generation)mark("generation");
        if(!same_definition(a.definition,b.definition))mark("definition");
        if(a.definition_words!=b.definition_words)mark("definition_words");
        if(!same_entry_points(a.entry_points,b.entry_points))mark("entry_points");
        if(!same_banks(a.saved_object_banks,b.saved_object_banks))mark("saved_object_banks");
        if(a.unattached_slot!=b.unattached_slot)mark("unattached_slot");
        if(!compatible_containment_extent(a,b))mark("slot_count");
        // TS1 attribute banks grow on demand after an actor's first successful
        // write. Snapshot decoding and World::publish_snapshot enforce bounds.
        if(!compatible_person_data_extent(a,b))mark("person_data_count");
        if(a.motives.size()!=b.motives.size())mark("motive_count");
        if(a.ui_ghost!=b.ui_ghost)mark("ghost_flag_changed");
        if(a.static_footprint!=b.static_footprint&&!route_footprint_change)mark("footprint_flag_changed");
        if(a.ui_ghost)mark("ghost_entity");
        if(!a.static_footprint&&!route_actor)mark("dynamic_footprint_entity");
        if(!changed.empty())
            throw FormatError("session World metadata rejected for entity "+std::to_string(b.id)+": "+changed);
    }
    for(const auto& entity:bundle.world.entities){
        const auto original_entity=std::find_if(original.entities.begin(),original.entities.end(),
            [&](const auto& value){return value.id==entity.id;});
        if(original_entity==original.entities.end()&&(&entity!=route_marker))
            throw FormatError("session added an entity other than its exact active Go Here marker");
    }
    const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& entity){return entity.id==bundle.host.runtime_avatar_id;});
    if(actor==bundle.world.entities.end()||!actor->definition||
       actor->definition->guid!=bundle.host.resident_guid||
       (!bundle.seated_continuation&&(actor->attachment||
       (!actor->person_data.empty()&&actor->person_data[0]!=0))))
        throw FormatError("session resident identity or seated-action binding is unsupported");
}
void validate_owner(const VmContext& context,std::int16_t actor,
                    const std::vector<SessionDynamicActionOwner>& owners,bool local_route) {
    if(context.code_owner==static_cast<CodeOwner>(actor))return;
    if(local_route&&std::any_of(owners.begin(),owners.end(),[&](const auto& owner){
        return owner.owner==context.code_owner;
    }))return;
    throw FormatError("session requires an unserialized dynamic code owner");
}
void validate_runtime(const SessionBundle& bundle) {
    if(bundle.seated_continuation){validate_seated_session_continuation(bundle);return;}
    if(bundle.runtime_entities.size()!=1||bundle.runtime_entities[0].id!=bundle.host.runtime_avatar_id||
       bundle.scheduler.faulted)
        throw FormatError("restore supports exactly one nonfaulted resident thread");
    const auto& state=bundle.runtime_entities[0];
    const bool local_route=has_route(state);
    if(state.vm.status==VmStatus::fault||state.vm.status==VmStatus::dormant)
        throw FormatError("session VM is faulted or dormant");
    for(const auto& frame:state.vm.stack) {
        validate_owner(frame.context,state.id,state.dynamic_action_owners,local_route);
        if(frame.return_mode==VmCallReturnMode::external_frame_next_tick&&
           !owns_route_external(state,frame.invocation_ticket,frame.context.code_owner))
            throw FormatError("session external portal frame has no owning route stack");
    }
    const auto pending=[&](const VmPending& wait){
        validate_owner(wait.context,state.id,state.dynamic_action_owners,local_route);
        const auto& instruction=wait.instruction.instruction;
        const bool saved_go_here=state.route&&state.route->call_failure_trees&&instruction.opcode==27&&
            instruction.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0};
        const bool saved_look=state.route&&state.route->look_towards&&instruction.opcode==22&&
            instruction.operands[0]>=2&&instruction.operands[0]<=5&&
            state.route->look_towards->mode==instruction.operands[0]&&
            state.route->ticket==wait.ticket&&state.vm.pending&&state.vm.pending->ticket==wait.ticket;
        const bool saved_dialog=bundle.dialog&&bundle.dialog->thread_owner.id==state.id&&
            state.vm.pending&&state.vm.pending->ticket==wait.ticket&&
            dialog_matches_vm_snapshot(*bundle.dialog,state.vm);
        if(wait.instruction.instruction.opcode!=0&&wait.instruction.instruction.opcode!=17&&
           wait.instruction.instruction.opcode!=44&&
           !(instruction.opcode==45&&state.route.has_value())&&!saved_go_here&&!saved_look&&!saved_dialog&&
           !owns_route_pending(state,wait))
            throw FormatError("session pending route or external service is unsupported");
    };
    if(state.vm.pending)pending(*state.vm.pending);
    for(const auto& wait:state.vm.suspended_pending)pending(wait);
    if(state.vm.external_frame_return&&
       !owns_route_external(state,state.vm.external_frame_return->ticket,state.vm.external_frame_return->code_owner))
        throw FormatError("session external portal return has no owning route stack");
    if(state.vm.interaction_return)validate_owner({state.id,state.id,state.id,
        state.vm.interaction_return->code_owner},state.id,state.dynamic_action_owners,local_route);
    for(const auto& action:state.controllers.interactions.actions) {
        validate_owner(action.call.context,state.id,state.dynamic_action_owners,local_route);
        if(action.call.return_mode==VmCallReturnMode::external_frame_next_tick)
            throw FormatError("session queued portal action is unsupported");
    }
    const auto playback_key=[&](const PlaybackResourceKey& key){
        validate_owner({key.caller,key.callee,key.stack_object,key.code_owner},state.id,
            state.dynamic_action_owners,local_route);
    };
    if(state.playback.state)playback_key(state.playback.state->resource);
    if(state.playback.carry)playback_key(*state.playback.carry);
}
}

SingleResidentSessionRestore::SingleResidentSessionRestore(SessionBundle bundle,
    const LotContext& lot,const Ts1BootstrapGlobals& bootstrap,
    std::optional<std::int16_t> family,WorldLimits world_limits,RelationshipLimits relationship_limits)
    :bundle_(std::move(bundle)),world_(std::make_unique<World>(world_limits)),relationships_(relationship_limits),
     lot_(lot),bootstrap_(bootstrap),family_global_(family) {}

std::unique_ptr<SingleResidentSessionRestore> SingleResidentSessionRestore::prepare(
    const std::vector<std::uint8_t>& archive,const SessionResourceIdentity& resources,
    const SessionHostState& selection,const WorldSnapshot& original,const LotContext& globals,
    const Ts1BootstrapGlobals& bootstrap,std::optional<std::int16_t> family,SessionBundleLimits limits) {
    auto bundle=decode_session_bundle(archive,resources,limits);
    if(bundle.travel)throw FormatError("legacy single-resident restore cannot consume travel state; use the explicit household travel adapter");
    if(!bundle.npc_avatars.empty())throw FormatError("NPC avatars require the household restore profile");
    if(bundle.generic_execution)throw FormatError("generic execution requires the household restore profile");
    if(!bundle.household_roster.empty())throw FormatError("legacy single-resident restore cannot load a household session");
    const auto& host=bundle.host;
    if(host.neighborhood_mode!=selection.neighborhood_mode||host.house_number!=selection.house_number||
       host.family_chunk_id!=selection.family_chunk_id||host.family_number!=selection.family_number||
       host.resident_guid!=selection.resident_guid||host.resident_neighbor_id!=selection.resident_neighbor_id||
       host.runtime_avatar_id!=selection.runtime_avatar_id)
        throw FormatError("session selection differs from the explicitly selected lot and resident");
    validate_baseline(bundle,original);validate_runtime(bundle);
    std::optional<Ts1Clock> simulation_clock;
    if(bundle.simulation){simulation_clock.emplace();
        if(!simulation_clock->restore(bundle.simulation->clock))throw FormatError("invalid restored clock");}
    // RGLB is the source raw bank, not a lease on active family/lot.
    // BHAV SetGlobalValue(9/10) never performs family or lot activation.
    if(!bundle.runtime_globals){
        if(family&&*family!=static_cast<std::int16_t>(selection.family_chunk_id))
            throw FormatError("host family global does not match selected household");
        if(globals.read(10)!=std::optional<std::int16_t>{static_cast<std::int16_t>(selection.house_number)})
            throw FormatError("host lot global does not match selected house");
        // Require the exact set implemented by the host, with the same precedence
        // as scope 6 reads. No unsupported or missing global silently becomes zero.
        std::vector<std::uint16_t> expected;
        for(std::int16_t index=0;index<38;++index)
            if((simulation_clock&&simulation_clock->global(index))||(index==9&&family)||bootstrap.read(index)||globals.read(index))
                expected.push_back(static_cast<std::uint16_t>(index));
        if(expected.size()!=host.global_values.size())throw FormatError("session global roster differs from host");
        for(std::size_t n=0;n<expected.size();++n)
            if(expected[n]!=host.global_values[n].index)throw FormatError("session global is unsupported or missing");
        if(family){
            const auto saved_family=std::find_if(host.global_values.begin(),host.global_values.end(),
                [](const auto& entry){return entry.index==9;});
            if(saved_family==host.global_values.end()||saved_family->value!=*family)
                throw FormatError("session family global differs from selected household");
        }
        const auto saved_lot=std::find_if(host.global_values.begin(),host.global_values.end(),
            [](const auto& entry){return entry.index==10;});
        if(saved_lot==host.global_values.end()||saved_lot->value!=*globals.read(10))
            throw FormatError("session lot global differs from selected house");
    }

    auto result=std::unique_ptr<SingleResidentSessionRestore>(new SingleResidentSessionRestore(
        std::move(bundle),globals,bootstrap,family,limits.world.world,limits.relationships.relationships));
    if(!result->world_->restore_snapshot(result->bundle_.world)||
       !result->relationships_.restore_snapshot(result->bundle_.relationships))
        throw FormatError("session world or relationships exceed fresh candidate limits");
    bind_legacy_session_entity_references(result->bundle_,*result->world_);
    for(const auto& entry:result->bundle_.host.global_values) {
        const auto index=static_cast<std::int16_t>(entry.index);
        if(simulation_clock&&simulation_clock->global(index))continue; // Derived exclusively from SIMS.
        if(index==9&&result->family_global_)result->family_global_=entry.value;
        else if(result->bootstrap_.read(index))result->bootstrap_.write(index,entry.value);
        else if(!result->lot_.write(index,entry.value))throw FormatError("session global restore failed");
    }
    return result;
}

void SingleResidentSessionRestore::install_seated_continuation_owners(Vm& vm,
    const DefinitionCandidate& candidate,const std::vector<EntryPoint>& entry_points,
    const SessionActionRoutineLoader& load) {
    if(attempted_||seated_owners_verified_||!bundle_.seated_continuation)
        throw FormatError("seated owners can only be installed once before execution restore");
    // Resolve every resource first. On installation failure the caller must
    // discard this unpublished candidate and VM, just as for restore_execution.
    const auto owners=resolve_seated_session_owners(bundle_,candidate,entry_points,load);
    for(const auto& owner:owners)if(!vm.install_owner(owner.first,owner.second)){
        attempted_=true;throw FormatError("fresh VM rejected verified seated action owner");
    }
    seated_owners_verified_=true;seated_vm_=&vm;
}

void SingleResidentSessionRestore::install_dynamic_action_owners(Vm& vm,
    const DynamicActionLoader& load) {
    const auto& owners=bundle_.runtime_entities.front().dynamic_action_owners;
    if(attempted_||dynamic_owners_verified_||owners.empty()||!load)
        throw FormatError("dynamic action owners can only be installed once before execution restore");
    std::vector<std::pair<CodeOwner,RoutineSet>> resolved;resolved.reserve(owners.size());
    for(const auto& owner:owners){
        auto source=load(owner);
        if(source.second!=owner.linked_semiglobal||
           !source.first.count(routine_key(owner.root_routine))||
           session_action_routine_fingerprint(source.first)!=owner.routine_fingerprint)
            throw FormatError("dynamic action owner resource, GLOB link or routine closure changed");
        resolved.emplace_back(owner.owner,std::move(source.first));
    }
    for(auto& owner:resolved)if(!vm.install_owner(owner.first,std::move(owner.second))){
        attempted_=true;throw FormatError("fresh VM rejected verified dynamic action owner");
    }
    dynamic_owners_verified_=true;dynamic_vm_=&vm;
}

void SingleResidentSessionRestore::restore_execution(Vm& vm,EntityThreadControllers& controllers,
    Playback& playback,EntityVmScheduler& scheduler,const PlaybackRestoreResolver& resolver,
    RandomState& random,std::uint64_t& tick) {
    if(attempted_)throw FormatError("session candidate cannot be restored twice");
    attempted_=true;
    if(bundle_.seated_continuation&&(!seated_owners_verified_||seated_vm_!=&vm))
        throw FormatError("seated action resources were not verified and installed into this VM");
    const auto& state=bundle_.runtime_entities.front();
    if(!state.dynamic_action_owners.empty()&&(!dynamic_owners_verified_||dynamic_vm_!=&vm))
        throw FormatError("dynamic action resources were not verified and installed into this VM");
    if(!vm.restore_snapshot(state.vm))throw FormatError("session VM does not match fresh programs and bindings");
    if(!controllers.restore_snapshot(state.controllers))throw FormatError("session controllers do not match fresh VM and world");
    std::string why;
    if(!playback.restore_snapshot(state.playback,vm,resolver,&why))
        throw FormatError("session animation restore failed: "+why);
    if(!scheduler.restore_snapshot(bundle_.scheduler))throw FormatError("session scheduler does not match fresh execution hooks");
    random.reset(bundle_.host.random_seed);tick=bundle_.host.tick;
}

} // namespace odh
