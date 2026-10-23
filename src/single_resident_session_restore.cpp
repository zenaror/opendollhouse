#include "odh/single_resident_session_restore.hpp"
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
void validate_baseline(const SessionBundle& bundle,const WorldSnapshot& original) {
    if(bundle.world.attribute_policy!=original.attribute_policy||
       bundle.world.entities.size()!=original.entities.size())
        throw FormatError("session entity roster differs from the original lot");
    for(std::size_t n=0;n<original.entities.size();++n) {
        const auto& a=bundle.world.entities[n];const auto& b=original.entities[n];
        if(a.id!=b.id||a.kind!=b.kind||a.generation!=b.generation||
           !same_definition(a.definition,b.definition)||a.definition_words!=b.definition_words||
           !same_entry_points(a.entry_points,b.entry_points)||
           !same_banks(a.saved_object_banks,b.saved_object_banks)||a.unattached_slot!=b.unattached_slot||
           a.slots.size()!=b.slots.size()||a.attributes.size()!=b.attributes.size()||a.person_data.size()!=b.person_data.size()||
           a.motives.size()!=b.motives.size()||a.ui_ghost!=b.ui_ghost||
           a.static_footprint!=b.static_footprint||a.ui_ghost||!a.static_footprint)
            throw FormatError("session changed original lot metadata or contains transient routing entities");
    }
    const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& entity){return entity.id==bundle.host.runtime_avatar_id;});
    if(actor==bundle.world.entities.end()||!actor->definition||
       actor->definition->guid!=bundle.host.resident_guid||actor->attachment||
       (!actor->person_data.empty()&&actor->person_data[0]!=0))
        throw FormatError("session resident identity or seated-action binding is unsupported");
}
void validate_owner(const VmContext& context,std::int16_t actor) {
    if(context.code_owner!=static_cast<CodeOwner>(actor))
        throw FormatError("session requires an unserialized dynamic code owner");
}
void validate_runtime(const SessionBundle& bundle) {
    if(bundle.runtime_entities.size()!=1||bundle.runtime_entities[0].id!=bundle.host.runtime_avatar_id||
       bundle.scheduler.faulted)
        throw FormatError("restore supports exactly one nonfaulted resident thread");
    const auto& state=bundle.runtime_entities[0];
    if(state.vm.status==VmStatus::fault||state.vm.status==VmStatus::dormant)
        throw FormatError("session VM is faulted or dormant");
    for(const auto& frame:state.vm.stack) {
        validate_owner(frame.context,state.id);
        if(frame.return_mode==VmCallReturnMode::external_frame_next_tick)
            throw FormatError("session external portal frames are unsupported");
    }
    const auto pending=[&](const VmPending& wait){
        validate_owner(wait.context,state.id);
        if(wait.instruction.instruction.opcode!=0&&wait.instruction.instruction.opcode!=17&&
           wait.instruction.instruction.opcode!=44)
            throw FormatError("session pending route or external service is unsupported");
    };
    if(state.vm.pending)pending(*state.vm.pending);
    for(const auto& wait:state.vm.suspended_pending)pending(wait);
    if(state.vm.external_frame_return)
        throw FormatError("session external portal return is unsupported");
    if(state.vm.interaction_return&&state.vm.interaction_return->code_owner!=static_cast<CodeOwner>(state.id))
        throw FormatError("session interaction return requires a dynamic owner");
    for(const auto& action:state.controllers.interactions.actions) {
        validate_owner(action.call.context,state.id);
        if(action.call.return_mode==VmCallReturnMode::external_frame_next_tick)
            throw FormatError("session queued portal action is unsupported");
    }
    const auto playback_key=[&](const PlaybackResourceKey& key){
        if(key.code_owner!=static_cast<CodeOwner>(state.id))
            throw FormatError("session animation requires a dynamic owner");
    };
    if(state.playback.state)playback_key(state.playback.state->resource);
    if(state.playback.carry)playback_key(*state.playback.carry);
}
}

SingleResidentSessionRestore::SingleResidentSessionRestore(SessionBundle bundle,
    const LotContext& lot,const Ts1BootstrapGlobals& bootstrap,
    std::optional<std::int16_t> family,WorldLimits world_limits,RelationshipLimits relationship_limits)
    :bundle_(std::move(bundle)),world_(world_limits),relationships_(relationship_limits),
     lot_(lot),bootstrap_(bootstrap),family_global_(family) {}

std::unique_ptr<SingleResidentSessionRestore> SingleResidentSessionRestore::prepare(
    const std::vector<std::uint8_t>& archive,const SessionResourceIdentity& resources,
    const SessionHostState& selection,const WorldSnapshot& original,const LotContext& globals,
    const Ts1BootstrapGlobals& bootstrap,std::optional<std::int16_t> family,SessionBundleLimits limits) {
    auto bundle=decode_session_bundle(archive,resources,limits);
    const auto& host=bundle.host;
    if(host.neighborhood_mode!=selection.neighborhood_mode||host.house_number!=selection.house_number||
       host.family_chunk_id!=selection.family_chunk_id||host.family_number!=selection.family_number||
       host.resident_guid!=selection.resident_guid||host.resident_neighbor_id!=selection.resident_neighbor_id||
       host.runtime_avatar_id!=selection.runtime_avatar_id)
        throw FormatError("session selection differs from the explicitly selected lot and resident");
    validate_baseline(bundle,original);validate_runtime(bundle);
    if(family&&*family!=static_cast<std::int16_t>(selection.family_chunk_id))
        throw FormatError("host family global does not match selected household");
    if(globals.read(10)!=std::optional<std::int16_t>{static_cast<std::int16_t>(selection.house_number)})
        throw FormatError("host lot global does not match selected house");
    // Require the exact set implemented by the host, with the same precedence
    // as scope 6 reads. No unsupported or missing global silently becomes zero.
    std::vector<std::uint16_t> expected;
    for(std::int16_t index=0;index<38;++index)
        if((index==9&&family)||bootstrap.read(index)||globals.read(index))
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
    auto result=std::unique_ptr<SingleResidentSessionRestore>(new SingleResidentSessionRestore(
        std::move(bundle),globals,bootstrap,family,limits.world.world,limits.relationships.relationships));
    if(!result->world_.restore_snapshot(result->bundle_.world)||
       !result->relationships_.restore_snapshot(result->bundle_.relationships))
        throw FormatError("session world or relationships exceed fresh candidate limits");
    for(const auto& entry:result->bundle_.host.global_values) {
        const auto index=static_cast<std::int16_t>(entry.index);
        if(index==9&&result->family_global_)result->family_global_=entry.value;
        else if(result->bootstrap_.read(index))result->bootstrap_.write(index,entry.value);
        else if(!result->lot_.write(index,entry.value))throw FormatError("session global restore failed");
    }
    return result;
}

void SingleResidentSessionRestore::restore_execution(Vm& vm,EntityThreadControllers& controllers,
    Playback& playback,EntityVmScheduler& scheduler,const PlaybackRestoreResolver& resolver,
    RandomState& random,std::uint64_t& tick) {
    if(attempted_)throw FormatError("session candidate cannot be restored twice");
    attempted_=true;
    const auto& state=bundle_.runtime_entities.front();
    if(!vm.restore_snapshot(state.vm))throw FormatError("session VM does not match fresh programs and bindings");
    if(!controllers.restore_snapshot(state.controllers))throw FormatError("session controllers do not match fresh VM and world");
    std::string why;
    if(!playback.restore_snapshot(state.playback,vm,resolver,&why))
        throw FormatError("session animation restore failed: "+why);
    if(!scheduler.restore_snapshot(bundle_.scheduler))throw FormatError("session scheduler does not match fresh execution hooks");
    random.reset(bundle_.host.random_seed);tick=bundle_.host.tick;
}

} // namespace odh
