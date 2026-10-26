#include "odh/household_session_restore.hpp"
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
// Only one resident may own a dynamic Go Here continuation. Ordinary Route45
// continuations retain their existing roster rules.
const SessionRuntimeEntity* dynamic_route_resident(const SessionBundle& bundle) {
    const SessionRuntimeEntity* result=nullptr;
    for(const auto& state:bundle.runtime_entities) {
        if(state.dynamic_action_owners.empty())continue;
        if(result||state.dynamic_action_owners.size()!=1||!state.route)
            throw FormatError("household restore supports exactly one dynamic Go Here walker and owner");
        const auto& owner=state.dynamic_action_owners.front();
        if(owner.target_id!=state.route->target||owner.target_guid!=0x7c4||
           owner.root_routine!=4096||state.route->entry_flags!=0||!state.route->call_failure_trees)
            throw FormatError("household restore supports only the exact Go Here route owner");
        result=&state;
    }
    return result;
}
WorldSnapshot session_restore_baseline(const SessionBundle& bundle,const WorldSnapshot& original,
        const SessionCreatedObjectResolver& resolve) {
    auto expected=original;
    if(!bundle.generic_execution)return expected;
    for(const auto& removed:bundle.removed_original_objects){
        const auto found=std::find_if(expected.entities.begin(),expected.entities.end(),
            [&](const auto& value){return value.id==removed.id;});
        if(found==expected.entities.end()||found->kind!=EntityKind::object||
           found->generation!=removed.generation||!found->definition||found->definition->guid!=removed.guid)
            throw FormatError("removed original object does not match the selected lot baseline");
        expected.entities.erase(found);
    }
    for(const auto& created:bundle.created_objects){
        if(!resolve||created.generation<=original.generation_sequence||
           std::any_of(expected.entities.begin(),expected.entities.end(),
               [&](const auto& value){return value.id==created.id;}))
            throw FormatError("created object has no fresh verified origin outside the lot baseline");
        auto metadata=resolve(created);
        if(metadata.id!=created.id||metadata.generation!=created.generation||
           metadata.kind!=EntityKind::object||!metadata.definition||metadata.definition->guid!=created.guid||
           metadata.ui_ghost||metadata.attachment||metadata.position)
            throw FormatError("created object resource constructor returned incompatible metadata");
        expected.entities.push_back(std::move(metadata));
    }
    std::sort(expected.entities.begin(),expected.entities.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return expected;
}
void validate_baseline(const SessionBundle& bundle,const WorldSnapshot& original,
        const SessionCreatedObjectResolver& created_objects) {
    const auto expected=session_restore_baseline(bundle,original,created_objects);
    if(bundle.world.attribute_policy!=original.attribute_policy)
        throw FormatError("session World attribute policy differs from the original lot");
    const Entity* route_marker=nullptr;
    if(const auto* routed=dynamic_route_resident(bundle)) {
        const auto& owner=routed->dynamic_action_owners.front();
        const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& entity){return entity.id==owner.target_id;});
        if(found==bundle.world.entities.end()||found->kind!=EntityKind::object||
           !found->definition||found->definition->guid!=0x7c4||found->generation!=owner.target_generation||
           !found->ui_ghost||found->static_footprint||!found->position||
           !(*found->position==routed->route->target_position)||
           std::any_of(original.entities.begin(),original.entities.end(),
               [&](const auto& entity){return entity.id==owner.target_id;}))
            throw FormatError("saved Go Here route marker is not the exact temporary Destination object");
        route_marker=&*found;
    }
    if(bundle.world.entities.size()!=expected.entities.size()+(route_marker?1u:0u))
        throw FormatError("session entity roster differs from original lot and optional Go Here marker");
    for(const auto& b:expected.entities) {
        const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& entity){return entity.id==b.id;});
        if(found==bundle.world.entities.end())throw FormatError("session entity roster lost original lot entity");
        const auto& a=*found;
        const auto route=std::find_if(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
            [&](const auto& runtime){return runtime.id==a.id&&runtime.route.has_value();});
        const bool route_actor=route!=bundle.runtime_entities.end();
        const bool created=bundle.generic_execution&&std::any_of(bundle.created_objects.begin(),bundle.created_objects.end(),
            [&](const auto& value){return value.id==a.id&&value.generation==a.generation;});
        const bool route_footprint_change=route_actor&&b.static_footprint&&
            !a.static_footprint&&route->route->walking_started;
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
        if(a.slots.size()!=b.slots.size())mark("slot_count");
        // TS1 attribute banks grow on demand after an actor's first successful
        // write. Their values and lengths are part of WorldSnapshot; the
        // snapshot decoder and World::publish_snapshot enforce the bank and
        // aggregate word limits when restoring them.
        if(a.person_data.size()!=b.person_data.size())mark("person_data_count");
        if(a.motives.size()!=b.motives.size())mark("motive_count");
        if(a.ui_ghost!=b.ui_ghost)mark("ghost_flag_changed");
        if(a.static_footprint!=b.static_footprint&&!route_footprint_change&&!created)mark("footprint_flag_changed");
        if(a.ui_ghost)mark("ghost_entity");
        if(!a.static_footprint&&!route_actor&&!created)mark("dynamic_footprint_entity");
        if(!changed.empty())
            throw FormatError("session World metadata rejected for entity "+std::to_string(b.id)+": "+changed);
    }
    for(const auto& entity:bundle.world.entities){
        const auto found=std::find_if(expected.entities.begin(),expected.entities.end(),
            [&](const auto& value){return value.id==entity.id;});
        if(found==expected.entities.end() && &entity!=route_marker)
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
void validate_owner(const VmContext& context,const SessionRuntimeEntity& state) {
    if(context.code_owner==0||context.code_owner==static_cast<CodeOwner>(state.id))return;
    if(std::any_of(state.resource_owners.begin(),state.resource_owners.end(),
        [&](const auto& owner){return owner.owner==context.code_owner;}))return;
    if(state.route&&std::any_of(state.dynamic_action_owners.begin(),state.dynamic_action_owners.end(),
       [&](const auto& owner){return owner.owner==context.code_owner;}))return;
    if(context.code_owner!=static_cast<CodeOwner>(state.id))
        throw FormatError("session requires an unserialized dynamic code owner");
}
void validate_runtime(const SessionBundle& bundle) {
    if(bundle.seated_continuation)throw FormatError("household seated continuation is not supported by this restore profile");
    if(bundle.household_roster.empty()||bundle.scheduler.faulted)
        throw FormatError("household restore requires a complete nonfaulted version-2 roster");
    for(const auto& state:bundle.runtime_entities) {
    const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& entity){return entity.id==state.id;});
    if(actor==bundle.world.entities.end()||actor->attachment||(!actor->person_data.empty()&&actor->person_data[0]!=0))
        throw FormatError("household resident seated-action binding is unsupported");
    if(state.vm.status==VmStatus::fault||state.vm.status==VmStatus::dormant)
        throw FormatError("session VM is faulted or dormant");
    for(const auto& frame:state.vm.stack) {
        validate_owner(frame.context,state);
        if(frame.return_mode==VmCallReturnMode::external_frame_next_tick)
            throw FormatError("session external portal frames are unsupported");
    }
    const auto pending=[&](const VmPending& wait){
        validate_owner(wait.context,state);
        const auto& instruction=wait.instruction.instruction;
        const bool saved_go_here=state.route&&state.route->call_failure_trees&&instruction.opcode==27&&
            instruction.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0};
        if(wait.instruction.instruction.opcode!=0&&wait.instruction.instruction.opcode!=17&&
           wait.instruction.instruction.opcode!=44&&
           !(instruction.opcode==45&&state.route.has_value())&&!saved_go_here)
            throw FormatError("session pending route or external service is unsupported");
    };
    if(state.vm.pending)pending(*state.vm.pending);
    for(const auto& wait:state.vm.suspended_pending)pending(wait);
    if(state.vm.external_frame_return)
        throw FormatError("session external portal return is unsupported");
    if(state.vm.interaction_return)validate_owner({state.id,state.id,state.id,
        state.vm.interaction_return->code_owner},state);
    for(const auto& action:state.controllers.interactions.actions) {
        validate_owner(action.call.context,state);
        if(action.call.return_mode==VmCallReturnMode::external_frame_next_tick)
            throw FormatError("session queued portal action is unsupported");
    }
    const auto playback_key=[&](const PlaybackResourceKey& key){
        validate_owner({key.caller,key.callee,key.stack_object,key.code_owner},state);
    };
    if(state.playback.state)playback_key(state.playback.state->resource);
    if(state.playback.carry)playback_key(*state.playback.carry);
    }
}
}

HouseholdSessionRestore::HouseholdSessionRestore(SessionBundle bundle,
    const LotContext& lot,const Ts1BootstrapGlobals& bootstrap,
    std::optional<std::int16_t> family,WorldLimits world_limits,RelationshipLimits relationship_limits)
    :bundle_(std::move(bundle)),world_(std::make_unique<World>(world_limits)),relationships_(relationship_limits),
     lot_(lot),bootstrap_(bootstrap),family_global_(family) {}

const std::vector<SessionDynamicActionOwner>& HouseholdSessionRestore::dynamic_action_owners(std::int16_t id) const {
    const auto found=std::find_if(bundle_.runtime_entities.begin(),bundle_.runtime_entities.end(),
        [&](const auto& state){return state.id==id;});
    if(found==bundle_.runtime_entities.end())throw FormatError("household dynamic owner resident is absent");
    return found->dynamic_action_owners;
}

const std::vector<SessionResourceCodeOwner>& HouseholdSessionRestore::resource_owners(std::int16_t id) const {
    const auto found=std::find_if(bundle_.runtime_entities.begin(),bundle_.runtime_entities.end(),
        [&](const auto& state){return state.id==id;});
    if(found==bundle_.runtime_entities.end())throw FormatError("resource owner resident is absent");
    return found->resource_owners;
}
void HouseholdSessionRestore::install_resource_owners(std::int16_t id,Vm& vm,const ResourceOwnerLoader& load) {
    if(attempted_||!bundle_.generic_execution||resource_owner_vms_.count(id)||!load)
        throw FormatError("resource owners must be installed once into the fresh resident VM");
    const auto& owners=resource_owners(id);
    std::vector<std::pair<CodeOwner,RoutineSet>> resolved;
    for(const auto& owner:owners){
        auto source=load(owner);
        if(source.second!=owner.linked_semiglobal||
           !source.first.count(routine_key(owner.root_routine))||
           session_loaded_bank_fingerprint(source.first)!=owner.routine_fingerprint)
            throw FormatError("resource code owner bank or provenance changed");
        resolved.emplace_back(owner.owner,std::move(source.first));
    }
    for(auto& owner:resolved)if(!vm.install_owner(owner.first,std::move(owner.second))){
        attempted_=true;throw FormatError("fresh resident VM rejected its verified resource owner");
    }
    resource_owner_vms_.emplace(id,&vm);
}

void HouseholdSessionRestore::install_dynamic_action_owners(std::int16_t id,Vm& vm,
    const DynamicActionLoader& load) {
    const auto* routed=dynamic_route_resident(bundle_);
    if(attempted_||dynamic_owners_verified_||!routed||id!=routed->id||!load)
        throw FormatError("dynamic action owners can only be installed once for the routed resident before restore");
    const auto& owners=dynamic_action_owners(id);
    if(owners.size()!=1||!local_route(id))
        throw FormatError("household resident has no single verified Go Here dynamic owner");
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
        attempted_=true;throw FormatError("fresh household VM rejected verified dynamic action owner");
    }
    dynamic_owners_verified_=true;dynamic_owner_resident_=id;dynamic_owner_vm_=&vm;
}

std::unique_ptr<HouseholdSessionRestore> HouseholdSessionRestore::prepare(
    const std::vector<std::uint8_t>& archive,const SessionResourceIdentity& resources,
    const SessionHostState& selection,const std::vector<SessionHouseholdMember>& expected_roster,
    const WorldSnapshot& original,const LotContext& globals,
    const Ts1BootstrapGlobals& bootstrap,std::optional<std::int16_t> family,SessionBundleLimits limits,
    const SessionCreatedObjectResolver& created_objects) {
    auto bundle=decode_session_bundle(archive,resources,limits);
    if(expected_roster.empty()||!same_session_household_roster(bundle.household_roster,expected_roster))
        throw FormatError("saved household roster differs from family, World or character resources");
    const auto& host=bundle.host;
    if(host.neighborhood_mode!=selection.neighborhood_mode||host.house_number!=selection.house_number||
       host.family_chunk_id!=selection.family_chunk_id||host.family_number!=selection.family_number||
       host.resident_guid!=selection.resident_guid||host.resident_neighbor_id!=selection.resident_neighbor_id||
       host.runtime_avatar_id!=selection.runtime_avatar_id)
        throw FormatError("session selection differs from the explicitly selected lot and resident");
    validate_baseline(bundle,original,created_objects);validate_runtime(bundle);
    if(family&&*family!=static_cast<std::int16_t>(selection.family_chunk_id))
        throw FormatError("host family global does not match selected household");
    if(globals.read(10)!=std::optional<std::int16_t>{static_cast<std::int16_t>(selection.house_number)})
        throw FormatError("host lot global does not match selected house");
    // Require the exact set implemented by the host, with the same precedence
    // as scope 6 reads. No unsupported or missing global silently becomes zero.
    std::optional<Ts1Clock> simulation_clock;
    if(bundle.simulation){simulation_clock.emplace();
        if(!simulation_clock->restore(bundle.simulation->clock))throw FormatError("invalid restored clock");}
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
    auto result=std::unique_ptr<HouseholdSessionRestore>(new HouseholdSessionRestore(
        std::move(bundle),globals,bootstrap,family,limits.world.world,limits.relationships.relationships));
    if(!result->world_->restore_snapshot(result->bundle_.world)||
       !result->relationships_.restore_snapshot(result->bundle_.relationships))
        throw FormatError("session world or relationships exceed fresh candidate limits");
    for(const auto& entry:result->bundle_.host.global_values) {
        const auto index=static_cast<std::int16_t>(entry.index);
        if(simulation_clock&&simulation_clock->global(index))continue; // Derived exclusively from SIMS.
        if(index==9&&result->family_global_)result->family_global_=entry.value;
        else if(result->bootstrap_.read(index))result->bootstrap_.write(index,entry.value);
        else if(!result->lot_.write(index,entry.value))throw FormatError("session global restore failed");
    }
    return result;
}

void HouseholdSessionRestore::restore_execution(const std::vector<SessionExecutionBinding>& bindings,
    EntityVmScheduler& scheduler,RandomState& random,std::uint64_t& tick) {
    if(attempted_)throw FormatError("session candidate cannot be restored twice");
    attempted_=true;
    if(bindings.size()!=bundle_.runtime_entities.size())throw FormatError("household execution binding roster is partial");
    // Validate the complete binding roster before applying any execution values.
    for(std::size_t n=0;n<bindings.size();++n){const auto& b=bindings[n];
        if(b.id!=bundle_.runtime_entities[n].id||!b.vm||!b.controllers||!b.playback)
            throw FormatError("household execution binding is missing, reordered or invalid");
        for(std::size_t p=0;p<n;++p)if(bindings[p].vm==b.vm||bindings[p].controllers==b.controllers||bindings[p].playback==b.playback)
            throw FormatError("household execution bindings alias different residents");
    }
    if(const auto* routed=dynamic_route_resident(bundle_)) {
        const auto binding=std::find_if(bindings.begin(),bindings.end(),
            [&](const auto& value){return value.id==routed->id;});
        if(binding==bindings.end()||!dynamic_owners_verified_||
           dynamic_owner_resident_!=binding->id||dynamic_owner_vm_!=binding->vm)
            throw FormatError("routed resident dynamic action resources were not verified and installed into this VM");
    }
    for(const auto& binding:bindings){
        if(resource_owners(binding.id).empty())continue;
        const auto verified=resource_owner_vms_.find(binding.id);
        if(verified==resource_owner_vms_.end()||verified->second!=binding.vm)
            throw FormatError("resident resource owners were not verified in this execution graph");
    }
    for(std::size_t n=0;n<bindings.size();++n){const auto& b=bindings[n];const auto& state=bundle_.runtime_entities[n];
        if(!b.vm->restore_snapshot(state.vm))throw FormatError("household VM does not match fresh programs and bindings");
        if(!b.controllers->restore_snapshot(state.controllers))throw FormatError("household controllers do not match fresh VM and world");
        std::string why;
        if(!b.playback->restore_snapshot(state.playback,*b.vm,b.resolver,&why))
            throw FormatError("household animation restore failed: "+why);
    }
    if(!scheduler.restore_snapshot(bundle_.scheduler))throw FormatError("household scheduler does not match fresh execution hooks");
    random.reset(bundle_.host.random_seed);tick=bundle_.host.tick;
}

} // namespace odh
