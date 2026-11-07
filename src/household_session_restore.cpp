#include "odh/avatar_containment.hpp"
#include "odh/avatar_person_data.hpp"
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
std::vector<const SessionRuntimeEntity*> avatar_runtime_states(const SessionBundle& bundle) {
    std::vector<const SessionRuntimeEntity*> result;
    for(const auto& state:bundle.runtime_entities)result.push_back(&state);
    for(const auto& npc:bundle.npc_avatars)result.push_back(&npc.runtime);
    return result;
}
// Only one resident may own a dynamic Go Here continuation. Ordinary Route45
// continuations retain their existing roster rules.
const SessionRuntimeEntity* dynamic_route_resident(const SessionBundle& bundle) {
    const SessionRuntimeEntity* result=nullptr;
    for(const auto& state:bundle.runtime_entities) {
        if(state.dynamic_action_owners.empty())continue;
        if(result||state.dynamic_action_owners.size()!=1||!root_route(state))
            throw FormatError("household restore supports exactly one dynamic Go Here walker and owner");
        const auto& owner=state.dynamic_action_owners.front();
        const auto* route=root_route(state);
        if(owner.target_id!=route->target||owner.target_guid!=0x7c4||
           owner.root_routine!=4096||route->entry_flags!=0||!route->call_failure_trees)
            throw FormatError("household restore supports only the exact Go Here route owner");
        result=&state;
    }
    return result;
}
WorldSnapshot session_restore_baseline(const SessionBundle& bundle,const WorldSnapshot& original,
        const SessionCreatedObjectResolver& resolve,const SessionNpcAvatarResolver& resolve_npc,
        const SessionTravelAvatarResolver& resolve_traveler,const SessionRetiredHouseholdAvatarResolver& resolve_retired_household) {
    auto expected=original;
    if(!bundle.generic_execution)return expected;
    for(const auto& removed:bundle.removed_original_objects){
        const auto found=std::find_if(expected.entities.begin(),expected.entities.end(),
            [&](const auto& value){return value.id==removed.id;});
        if(found==expected.entities.end()||found->kind!=EntityKind::object||
           found->generation!=removed.generation||!found->definition||found->definition->guid!=removed.guid)
            throw FormatError("removed original object does not match the selected lot baseline");
        if(std::any_of(bundle.world.retained_entities.begin(),bundle.world.retained_entities.end(),[&](const auto& e){return e.id==removed.id&&e.generation==removed.generation;}))
            expected.retained_entities.push_back(*found);
        expected.entities.erase(found);
    }
    if(bundle.object_history){
        for(const auto& origin:bundle.object_history->retired_created_objects){
            if(std::none_of(bundle.world.retained_entities.begin(),bundle.world.retained_entities.end(),[&](const auto& e){return e.id==origin.id&&e.generation==origin.generation;}))continue;
            if(!resolve||origin.generation<=original.generation_sequence)throw FormatError("retained created body lacks fresh verified resource provenance");
            auto metadata=resolve(origin);
            if(metadata.id!=origin.id||metadata.generation!=origin.generation||metadata.kind!=EntityKind::object||
               !metadata.definition||metadata.definition->guid!=origin.guid||metadata.ui_ghost||metadata.attachment||metadata.position)
                throw FormatError("retained created resource constructor returned incompatible metadata");
            expected.retained_entities.push_back(std::move(metadata));
        }
        for(const auto& origin:bundle.object_history->retired_household_avatars){const auto& member=origin.member;
            // Every historical record needs its sealed source/resource owner,
            // even after its body was legitimately collected. No active-FAMI
            // membership or NPC constructor is borrowed for the previous family.
            if(!resolve_retired_household)throw FormatError("retired household history lacks its historical source/resource resolver");
            auto metadata=resolve_retired_household(origin);
            if(metadata.id!=member.id||metadata.generation!=member.generation||metadata.kind!=EntityKind::avatar||
               !metadata.definition||metadata.definition->guid!=member.guid||metadata.ui_ghost||metadata.dead||
               metadata.attachment||metadata.position||metadata.person_data.size()<=61||
               metadata.person_data[31]!=member.neighbor_id||
               static_cast<std::uint16_t>(metadata.person_data[61])!=origin.source_lease.family_chunk_id)
                throw FormatError("retired household resource constructor returned incompatible metadata");
            const auto old=std::find_if(expected.entities.begin(),expected.entities.end(),[&](const auto& value){
                return value.id==member.id&&value.generation==member.generation;});
            if(old!=expected.entities.end()){
                if(old->kind!=EntityKind::avatar||!old->definition||old->definition->guid!=member.guid)
                    throw FormatError("retired household origin aliases an incompatible original identity");
                metadata=*old;expected.entities.erase(old);
            }else if(member.generation<=original.generation_sequence)
                throw FormatError("retired household identity borrows an unknown original generation");
            if(std::any_of(bundle.world.retained_entities.begin(),bundle.world.retained_entities.end(),[&](const auto& e){
                    return e.id==member.id&&e.generation==member.generation;}))expected.retained_entities.push_back(std::move(metadata));
        }
        for(const auto& origin:bundle.object_history->retired_npc_avatars){
            if(std::none_of(bundle.world.retained_entities.begin(),bundle.world.retained_entities.end(),[&](const auto& e){return e.id==origin.id&&e.generation==origin.generation;}))continue;
            if(!resolve_npc||origin.generation<=original.generation_sequence)throw FormatError("retained NPC body lacks fresh verified character provenance");
            auto metadata=resolve_npc(origin);
            if(metadata.id!=origin.id||metadata.generation!=origin.generation||metadata.kind!=EntityKind::avatar||
               !metadata.definition||metadata.definition->guid!=origin.guid||metadata.ui_ghost||metadata.attachment||metadata.position)
                throw FormatError("retained NPC constructor returned incompatible metadata");
            expected.retained_entities.push_back(std::move(metadata));
        }
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
    for(const auto& npc:bundle.npc_avatars) {
        const auto& origin=npc.origin;
        if(!resolve_npc||origin.generation<=original.generation_sequence||
           std::any_of(expected.entities.begin(),expected.entities.end(),[&](const auto& value){return value.id==origin.id;}))
            throw FormatError("NPC avatar has no fresh verified origin outside the lot baseline");
        auto metadata=resolve_npc(origin);
        if(metadata.id!=origin.id||metadata.generation!=origin.generation||metadata.kind!=EntityKind::avatar||
           !metadata.definition||metadata.definition->guid!=origin.guid||metadata.ui_ghost||metadata.attachment||metadata.position||
           (metadata.person_data.size()!=88&&metadata.person_data.size()!=101)||metadata.person_data[31]!=origin.neighbor_id||metadata.motives.size()!=16)
            throw FormatError("NPC avatar resource constructor returned incompatible metadata");
        expected.entities.push_back(std::move(metadata));
    }
    // Keep the original provenance floor: source objects may have been created
    // before the household. HHRD does NOT authorize itself: prepare() already
    // matched every member to the independently authenticated family/resources.
    // The same immutable character resolver handles new HOME residents and
    // travelers; only a visited lot additionally requires its admitted party.
    const bool visiting=bundle.travel&&bundle.travel->current_house!=bundle.travel->home_house;
    for(const auto& member:bundle.household_roster){
        if(std::any_of(expected.entities.begin(),expected.entities.end(),[&](const auto& entity){return entity.id==member.id;}))
            continue; // Original metadata is checked below, never replaced.
        if(!resolve_traveler||member.generation<=original.generation_sequence)
            throw FormatError("new household avatar lacks authenticated fresh character provenance");
        if(visiting){
            const Ts1TravelMember identity{member.guid,member.neighbor_id,member.object_definition_id,
                member.character_asset,member.character_fingerprint};
            if(!bundle.travel->family_activated||std::none_of(bundle.travel->travelers.begin(),bundle.travel->travelers.end(),
                    [&](const auto& traveler){return same_ts1_travel_member(traveler,identity);}))
                throw FormatError("traveling household metadata differs from the admitted travel subset");
        }
        auto metadata=resolve_traveler(member);
        if(metadata.id!=member.id||metadata.generation!=member.generation||metadata.kind!=EntityKind::avatar||
           !metadata.definition||metadata.definition->guid!=member.guid||metadata.dead||metadata.ui_ghost||
           metadata.attachment||metadata.position||metadata.person_data.size()<=61||
           metadata.person_data[31]!=member.neighbor_id||
           static_cast<std::uint16_t>(metadata.person_data[61])!=bundle.host.family_chunk_id||metadata.motives.size()!=16)
            throw FormatError("household character constructor returned incompatible metadata");
        expected.entities.push_back(std::move(metadata));
    }
    std::sort(expected.entities.begin(),expected.entities.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return expected;
}
void validate_baseline(const SessionBundle& bundle,const WorldSnapshot& original,
        const SessionCreatedObjectResolver& created_objects,const SessionNpcAvatarResolver& npc_avatars,
        const SessionTravelAvatarResolver& travel_avatars,const SessionRetiredHouseholdAvatarResolver& retired_household_avatars) {
    const auto expected=session_restore_baseline(bundle,original,created_objects,npc_avatars,travel_avatars,retired_household_avatars);
    if(bundle.world.attribute_policy!=original.attribute_policy)
        throw FormatError("session World attribute policy differs from the original lot");
    if(expected.retained_entities.size()!=bundle.world.retained_entities.size())throw FormatError("retained body provenance is incomplete");
    for(const auto& body:bundle.world.retained_entities){
        const auto base=std::find_if(expected.retained_entities.begin(),expected.retained_entities.end(),[&](const auto& e){return e.id==body.id&&e.generation==body.generation;});
        if(base==expected.retained_entities.end()||base->kind!=body.kind||!same_definition(base->definition,body.definition)||
           base->definition_words!=body.definition_words||!same_entry_points(base->entry_points,body.entry_points)||
           !same_banks(base->saved_object_banks,body.saved_object_banks)||base->unattached_slot!=body.unattached_slot||
           !compatible_containment_extent(body,*base)||!compatible_person_data_extent(body,*base)||body.motives.size()!=base->motives.size()||body.ui_ghost!=base->ui_ghost)
            throw FormatError("retained body metadata differs from its original resource");
    }
    const auto avatars=avatar_runtime_states(bundle);
    const Entity* route_marker=nullptr;
    if(const auto* routed=dynamic_route_resident(bundle)) {
        const auto& owner=routed->dynamic_action_owners.front();
        const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& entity){return entity.id==owner.target_id;});
        if(found==bundle.world.entities.end()||found->kind!=EntityKind::object||
           !found->definition||found->definition->guid!=0x7c4||found->generation!=owner.target_generation||
           !found->ui_ghost||found->static_footprint||!found->position||
           !(*found->position==root_route(*routed)->target_position)||
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
        const auto route=std::find_if(avatars.begin(),avatars.end(),
            [&](const auto* runtime){return runtime->id==a.id&&has_route(*runtime);});
        const bool route_actor=route!=avatars.end();
        // StaticFootprint is mutable execution state, not resource metadata.
        // A completed child route can leave it dynamic while its parent has
        // not started walking. GEXP preserves that state and the exact actor
        // continuation; limited legacy profiles still use their route gate.
        const bool native_actor=bundle.generic_execution&&a.kind==EntityKind::avatar&&
            std::any_of(avatars.begin(),avatars.end(),[&](const auto* runtime){
                return runtime->id==a.id&&runtime->controllers.interactions.actor_id==a.id&&
                    runtime->controllers.interactions.actor_generation==a.generation;});
        const bool created=bundle.generic_execution&&(std::any_of(bundle.created_objects.begin(),bundle.created_objects.end(),
            [&](const auto& value){return value.id==a.id&&value.generation==a.generation;})||
            std::any_of(bundle.npc_avatars.begin(),bundle.npc_avatars.end(),[&](const auto& npc){
                return npc.origin.id==a.id&&npc.origin.generation==a.generation;}));
        const bool route_footprint_change=route_actor&&b.static_footprint&&
            !a.static_footprint&&route_walking_started(**route);
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
        // write. Their values and lengths are part of WorldSnapshot; the
        // snapshot decoder and World::publish_snapshot enforce the bank and
        // aggregate word limits when restoring them.
        if(!compatible_person_data_extent(a,b))mark("person_data_count");
        if(a.motives.size()!=b.motives.size())mark("motive_count");
        if(a.ui_ghost!=b.ui_ghost)mark("ghost_flag_changed");
        if(a.static_footprint!=b.static_footprint&&!route_footprint_change&&!created&&!native_actor){
            const auto flag_word=[](const Entity& entity){const auto it=entity.object_words.find(8);
                return it==entity.object_words.end()?std::string("absent"):std::to_string(it->second);};
            mark((std::string("footprint_flag_changed(saved=")+std::to_string(a.static_footprint)+
                ",baseline="+std::to_string(b.static_footprint)+",flags="+flag_word(a)+
                ",baseline_flags="+flag_word(b)+",route="+std::to_string(route_actor)+
                ",walking="+std::to_string(route_actor&&route_walking_started(**route))+
                ",tick="+std::to_string(bundle.host.tick)+")").c_str());
        }
        if(a.ui_ghost)mark("ghost_entity");
        if(!a.static_footprint&&!route_actor&&!created&&!native_actor)mark("dynamic_footprint_entity");
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
       (!bundle.generic_execution&&!find_seated_session_continuation(bundle,bundle.host.runtime_avatar_id)&&(actor->attachment||
       (!actor->person_data.empty()&&actor->person_data[0]!=0))))
        throw FormatError("session resident identity or seated-action binding is unsupported");
}
void validate_owner(const VmContext& context,const SessionRuntimeEntity& state) {
    if(context.code_owner==0||context.code_owner==static_cast<CodeOwner>(state.id))return;
    if(std::any_of(state.resource_owners.begin(),state.resource_owners.end(),
        [&](const auto& owner){return owner.owner==context.code_owner;}))return;
    if(has_route(state)&&std::any_of(state.dynamic_action_owners.begin(),state.dynamic_action_owners.end(),
       [&](const auto& owner){return owner.owner==context.code_owner;}))return;
    if(context.code_owner!=static_cast<CodeOwner>(state.id))
        throw FormatError("session requires an unserialized dynamic code owner");
}
void validate_runtime(const SessionBundle& bundle) {
    validate_seated_session_continuations(bundle);
    for(const auto& npc:bundle.npc_avatars)if(!npc.runtime.dynamic_action_owners.empty())
        throw FormatError("NPC dynamic Go Here marker owners are not a supported restore boundary");
    if(bundle.household_roster.empty()||bundle.scheduler.faulted)
        throw FormatError("household restore requires a complete nonfaulted version-2 roster");
    for(const auto* state_ptr:avatar_runtime_states(bundle)) {
    const auto& state=*state_ptr;
    const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& entity){return entity.id==state.id;});
    const auto* seated=find_seated_session_continuation(bundle,state.id);
    if(actor==bundle.world.entities.end()||(!bundle.generic_execution&&!seated&&(actor->attachment||(!actor->person_data.empty()&&actor->person_data[0]!=0))))
        throw FormatError("household resident seated-action binding is unsupported");
    if(state.vm.status==VmStatus::fault||state.vm.status==VmStatus::dormant)
        throw FormatError("session VM is faulted or dormant");
    for(const auto& frame:state.vm.stack) {
        validate_owner(frame.context,state);
        if(frame.return_mode==VmCallReturnMode::external_frame_next_tick&&
           !owns_route_external(state,frame.invocation_ticket,frame.context.code_owner))
            throw FormatError("session external portal frame has no owning route stack");
    }
    const auto pending=[&](const VmPending& wait){
        validate_owner(wait.context,state);
        const auto& instruction=wait.instruction.instruction;
        // decode_session_bundle already validates the complete instruction,
        // exact relative profile, failure flags and route/VM identity links.
        const bool saved_relative=state.route&&instruction.opcode==27;
        const bool saved_look=state.route&&state.route->look_towards&&instruction.opcode==22&&
            instruction.operands[0]>=2&&instruction.operands[0]<=5&&
            state.route->look_towards->mode==instruction.operands[0]&&
            state.route->ticket==wait.ticket&&state.vm.pending&&state.vm.pending->ticket==wait.ticket;
        const bool saved_dialog=bundle.dialog&&bundle.dialog->thread_owner.id==state.id&&
            state.vm.pending&&state.vm.pending->ticket==wait.ticket&&
            dialog_matches_vm_snapshot(*bundle.dialog,state.vm);
        if(wait.instruction.instruction.opcode!=0&&wait.instruction.instruction.opcode!=17&&
           wait.instruction.instruction.opcode!=44&&
           !(instruction.opcode==45&&state.route.has_value())&&!saved_relative&&!saved_look&&!saved_dialog&&
           !owns_route_pending(state,wait))
            throw FormatError("session pending route or external service is unsupported");
    };
    if(state.vm.pending)pending(*state.vm.pending);
    for(const auto& wait:state.vm.suspended_pending)pending(wait);
    if(state.vm.external_frame_return&&
       !owns_route_external(state,state.vm.external_frame_return->ticket,state.vm.external_frame_return->code_owner))
        throw FormatError("session external portal return has no owning route stack");
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

const SessionRuntimeEntity* HouseholdSessionRestore::runtime_entity(std::int16_t id) const {
    for(const auto& state:bundle_.runtime_entities)if(state.id==id)return &state;
    for(const auto& npc:bundle_.npc_avatars)if(npc.runtime.id==id)return &npc.runtime;
    return nullptr;
}
const std::vector<SessionDynamicActionOwner>& HouseholdSessionRestore::dynamic_action_owners(std::int16_t id) const {
    const auto* state=runtime_entity(id);
    if(!state)throw FormatError("avatar dynamic owner thread is absent");
    return state->dynamic_action_owners;
}

void HouseholdSessionRestore::install_seated_continuation_owners(std::int16_t id,Vm& vm,
    const DefinitionCandidate& candidate,const std::vector<EntryPoint>& entry_points,
    const SessionActionRoutineLoader& load) {
    const auto* c=seated_continuation(id);
    if(attempted_||!c||seated_owner_vms_.count(id))
        throw FormatError("seated resources must be verified once for their resident before execution restore");
    const auto installed=resource_owner_vms_.find(id);
    if(!resource_owners(id).empty()&&(installed==resource_owner_vms_.end()||installed->second!=&vm))
        throw FormatError("seated verification requires this resident's installed resource banks");
    const auto owners=resolve_seated_session_owners(bundle_,*c,candidate,entry_points,load);
    // Household SEAT uses full GEXP banks, never a second legacy closure bank.
    if(!owners.empty())throw FormatError("household seated continuation has legacy closure owners");
    seated_owner_vms_.emplace(id,&vm);
}

const std::vector<SessionResourceCodeOwner>& HouseholdSessionRestore::resource_owners(std::int16_t id) const {
    const auto* state=runtime_entity(id);
    if(!state)throw FormatError("avatar resource owner thread is absent");
    return state->resource_owners;
}

void HouseholdSessionRestore::install_resource_owners(std::int16_t id,Vm& vm,const ResourceOwnerLoader& load) {
    if(attempted_||!bundle_.generic_execution||resource_owner_vms_.count(id)||!load)
        throw FormatError("resource owners must be installed once into the fresh resident VM");
    const auto& owners=resource_owners(id);
    std::vector<std::pair<CodeOwner,RoutineSet>> resolved;
    for(const auto& owner:owners){
        auto source=load(owner);
        if(source.second!=owner.linked_semiglobal||
           !source.first.count(session_resource_root_key(owner))||
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
    if(owners.size()!=1||!root_route(*routed))
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
    const SessionCreatedObjectResolver& created_objects,const SessionNpcAvatarResolver& npc_avatars,
    std::optional<Ts1TravelState> expected_travel,const SessionTravelAvatarResolver& travel_avatars,
    const SessionRetiredHouseholdAvatarResolver& retired_household_avatars) {
    auto bundle=decode_session_bundle(archive,resources,limits);
    if(bundle.travel_visited_session)throw FormatError("travel envelope must be reopened through the travel checkpoint adapter");
    if(bundle.travel.has_value()!=expected_travel.has_value()||
       (bundle.travel&&encode_ts1_travel_state(*bundle.travel)!=encode_ts1_travel_state(*expected_travel)))
        throw FormatError("TRIP restore requires explicitly authenticated matching travel state");
    if(expected_roster.empty()||!same_session_household_roster(bundle.household_roster,expected_roster))
        throw FormatError("saved household roster differs from family, World or character resources");
    const auto& host=bundle.host;
    if(host.neighborhood_mode!=selection.neighborhood_mode||host.house_number!=selection.house_number||
       host.family_chunk_id!=selection.family_chunk_id||host.family_number!=selection.family_number||
       host.resident_guid!=selection.resident_guid||host.resident_neighbor_id!=selection.resident_neighbor_id||
       host.runtime_avatar_id!=selection.runtime_avatar_id)
        throw FormatError("session selection differs from the explicitly selected lot and resident");
    validate_baseline(bundle,original,created_objects,npc_avatars,travel_avatars,retired_household_avatars);validate_runtime(bundle);
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

    {
        auto& reserved=limits.world.world.reserved_entity_ids;
        // Preserve known old removals even in legacy profiles. A live reused
        // numeric ID remains a legacy compatibility case, never a new claim of
        // complete history or permission for neutral-property preparation.
        if(!bundle.object_history||!bundle.object_history->reusable_entity_ids)for(const auto& removed:bundle.removed_original_objects)
            if(std::none_of(bundle.world.entities.begin(),bundle.world.entities.end(),
                [&](const auto& entity){return entity.id==removed.id;}))reserved.push_back(removed.id);
        if(bundle.object_history)
        reserved.insert(reserved.end(),bundle.object_history->reserved_entity_ids.begin(),bundle.object_history->reserved_entity_ids.end());
        std::sort(reserved.begin(),reserved.end());reserved.erase(std::unique(reserved.begin(),reserved.end()),reserved.end());
        if(reserved.size()>4096)throw FormatError("restored object-history reservation union exceeds limit");
    }
    auto result=std::unique_ptr<HouseholdSessionRestore>(new HouseholdSessionRestore(
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

void HouseholdSessionRestore::restore_execution(const std::vector<SessionExecutionBinding>& bindings,
    EntityVmScheduler& scheduler,RandomState& random,std::uint64_t& tick) {
    restore_execution(bindings,{},scheduler,random,tick);
}

void HouseholdSessionRestore::restore_execution(const std::vector<SessionExecutionBinding>& residents,
    const std::vector<SessionExecutionBinding>& npcs,EntityVmScheduler& scheduler,RandomState& random,std::uint64_t& tick) {
    if(attempted_)throw FormatError("session candidate cannot be restored twice");
    if(bundle_.travel&&!travel_consumed_)throw FormatError("host must consume validated travel state before restoring VM execution");
    attempted_=true;
    if(residents.size()!=bundle_.runtime_entities.size()||npcs.size()!=bundle_.npc_avatars.size())
        throw FormatError("household or NPC execution binding roster is partial");
    std::vector<SessionExecutionBinding> bindings=residents;
    bindings.insert(bindings.end(),npcs.begin(),npcs.end());
    const auto states=avatar_runtime_states(bundle_);
    // Validate the complete binding roster before applying any execution values.
    for(std::size_t n=0;n<bindings.size();++n){const auto& b=bindings[n];
        if(b.id!=states[n]->id||!b.vm||!b.controllers||!b.playback)
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
    for(const auto& binding:bindings)if(seated_continuation(binding.id)){
        const auto verified=seated_owner_vms_.find(binding.id);
        if(verified==seated_owner_vms_.end()||verified->second!=binding.vm)
            throw FormatError("seated resident closures were not verified in this execution graph");
    }
    for(const auto& binding:bindings){
        if(resource_owners(binding.id).empty())continue;
        const auto verified=resource_owner_vms_.find(binding.id);
        if(verified==resource_owner_vms_.end()||verified->second!=binding.vm)
            throw FormatError("resident resource owners were not verified in this execution graph");
    }
    for(std::size_t n=0;n<bindings.size();++n){const auto& b=bindings[n];const auto& state=*states[n];
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
