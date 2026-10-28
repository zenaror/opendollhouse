#include "odh/property_checkpoint.hpp"
#include "odh/sha256.hpp"
#include "odh/property_disposition.hpp"
#include <algorithm>
#include <set>
#include <map>

namespace odh {
namespace {
void require(bool condition,const char* message){if(!condition)throw FormatError(message);}
bool same_lease(const SessionFamilyLease& a,const SessionFamilyLease& b) {
    return a.neighborhood_identity==b.neighborhood_identity && a.family_identity==b.family_identity &&
        a.occupancy_epoch==b.occupancy_epoch && a.family_chunk_id==b.family_chunk_id;
}
bool same_metadata(const Entity& a,const Entity& b) {
    if(a.id!=b.id || a.generation!=b.generation || a.kind!=b.kind ||
        a.definition.has_value()!=b.definition.has_value() || !a.definition ||
        a.definition->guid!=b.definition->guid || a.definition->master_guid!=b.definition->master_guid ||
        a.definition_words!=b.definition_words || a.slots.size()!=b.slots.size() ||
        a.person_data.size()!=b.person_data.size() || a.motives.size()!=b.motives.size() ||
        a.ui_ghost!=b.ui_ghost || a.unattached_slot!=b.unattached_slot ||
        a.saved_object_banks.has_value()!=b.saved_object_banks.has_value() ||
        a.entry_points.has_value()!=b.entry_points.has_value())return false;
    if(a.saved_object_banks && (a.saved_object_banks->object_data!=b.saved_object_banks->object_data ||
        a.saved_object_banks->extras!=b.saved_object_banks->extras))return false;
    if(a.entry_points && (a.entry_points->size()!=b.entry_points->size() ||
        !std::equal(a.entry_points->begin(),a.entry_points->end(),b.entry_points->begin(),
            [](const auto& x,const auto& y){return x.condition==y.condition && x.action==y.action;})))return false;
    return true;
}
struct BaselineDerivation {
    std::vector<SavedMultitileGroup> groups;
    std::vector<std::int16_t> reservations;
};
BaselineDerivation derive_baseline(const PropertyCheckpointBaseline& baseline,const SessionBundle& bundle,
    const DefinitionIndex& index,const SavedWorldLimits& limits,const SessionCreatedObjectResolver& resolve) {
    require(bundle.object_history && bundle.object_history->complete,
        "neutral property requires complete native object history; legacy gaps cannot be inferred");
    require(std::any_of(baseline.installed_house_sha256.begin(),baseline.installed_house_sha256.end(),
        [](auto byte){return byte!=0;}),"property baseline lacks installed house identity");
    require(baseline.world.entities.size()<=4096 && baseline.groups.size()<=4096 &&
        baseline.reserved_ids.size()<=4096 && std::is_sorted(baseline.reserved_ids.begin(),baseline.reserved_ids.end()) &&
        std::adjacent_find(baseline.reserved_ids.begin(),baseline.reserved_ids.end())==baseline.reserved_ids.end() &&
        (baseline.reserved_ids.empty() || baseline.reserved_ids.front()>0),"invalid property baseline bounds or reservations");
    auto wl=limits.world;wl.reserved_entity_ids=baseline.reserved_ids;
    World validate(wl,baseline.world.attribute_policy);
    require(validate.restore_snapshot(baseline.world),"invalid occupied-session property baseline");
    require(bundle.world.attribute_policy==baseline.world.attribute_policy &&
        bundle.world.generation_sequence>=baseline.world.generation_sequence &&
        bundle.world.spatial_revision>=baseline.world.spatial_revision,"property checkpoint regresses baseline history");
    std::map<std::int16_t,const Entity*> original,current;
    for(const auto& entity:baseline.world.entities) {
        require(entity.definition && !entity.ui_ghost,"unsupported property baseline identity");
        original.emplace(entity.id,&entity);
    }
    for(const auto& entity:bundle.world.entities)current.emplace(entity.id,&entity);
    std::map<std::int16_t,const SessionRemovedOriginalObject*> removed;
    for(const auto& entry:bundle.removed_original_objects) {
        const auto found=original.find(entry.id);
        require(found!=original.end() && found->second->kind==EntityKind::object &&
            found->second->generation==entry.generation && found->second->definition->guid==entry.guid &&
            !current.count(entry.id) && removed.emplace(entry.id,&entry).second,
            "removed property origin differs from baseline or its numeric ID was reused");
    }
    std::map<std::int16_t,const SessionCreatedObject*> created;
    for(const auto& entry:bundle.created_objects) {
        require(!original.count(entry.id) && current.count(entry.id) &&
            entry.generation>baseline.world.generation_sequence && created.emplace(entry.id,&entry).second,
            "created property identity is not fresh outside baseline");
        const auto* definition=index.unique(entry.guid);
        require(definition && definition->source==entry.source && definition->asset==entry.asset &&
            definition->chunk_id==entry.definition_chunk_id && !definition->definition.is_multitile &&
            definition->definition.master_id==0,"created property resource provenance differs from canonical definition");
        require(bool(resolve),"created property object requires verified resource reconstruction");
        const auto metadata=resolve(entry);
        require(metadata.kind==EntityKind::object && !metadata.attachment && !metadata.position &&
            !metadata.ui_ghost && same_metadata(metadata,*current.at(entry.id)),
            "created property metadata differs from verified resource reconstruction");
    }
    for(const auto& [id,entity]:original) {
        const auto found=current.find(id);
        if(found==current.end())require(removed.count(id),"property checkpoint lost an unrecorded baseline object");
        else require(same_metadata(*entity,*found->second),"property current object metadata differs from baseline");
    }
    for(const auto& [id,entity]:current) {
        (void)entity;
        require(original.count(id) || created.count(id),"property current object lacks creation provenance");
    }
    BaselineDerivation result;
    require(baseline.groups.size()<=4096,"property baseline group budget exceeded");
    std::set<std::int16_t> seen;
    std::size_t members=0;
    for(const auto& group:baseline.groups) {
        require(!group.members.empty() && group.members.size()<=4096-members &&
            std::find(group.members.begin(),group.members.end(),group.leader)!=group.members.end(),"invalid baseline property group");
        members+=group.members.size();std::size_t absent=0;
        const DefinitionCandidate* provenance=nullptr;
        for(const auto id:group.members) {
            require(original.count(id) && seen.insert(id).second && original.at(id)->definition->master_guid==group.master_guid,
                "invalid baseline property group coverage or identity");
            const auto& entity=*original.at(id);const auto* own=index.unique(entity.definition->guid);
            require(own && own->definition.is_multitile==(own->definition.master_id!=0),"invalid baseline property definition");
            const auto type=entity.definition_words.find(9);
            require(type==entity.definition_words.end() || static_cast<std::uint16_t>(type->second)==own->definition.object_type,
                "baseline property type contradicts definition");
            if(own->definition.is_multitile) {
                const auto* master=index.unique_master(*own);
                require(master && master->definition.is_multitile && group.master_guid==master->definition.guid,
                    "invalid baseline property master");
            } else require(!group.master_guid && group.members.size()==1,"invalid baseline property singleton");
            require(!provenance || (provenance->source==own->source && provenance->asset==own->asset &&
                provenance->definition.master_id==own->definition.master_id),"cross-resource baseline property group");
            provenance=own;
            if(removed.count(id))++absent;
        }
        require(absent==0 || absent==group.members.size(),"checkpoint split a baseline property group");
        if(!absent)result.groups.push_back(group);
    }
    require(seen.size()==original.size(),"incomplete baseline property group coverage");
    for(const auto& [id,origin]:created){(void)origin;result.groups.push_back({id,{}, {id}});}
    result.reservations=baseline.reserved_ids;
    for(const auto id:baseline.reserved_ids)
        require(std::binary_search(bundle.object_history->reserved_entity_ids.begin(),bundle.object_history->reserved_entity_ids.end(),id),
            "complete object history lost baseline reservations");
    for(const auto& origin:bundle.object_history->retired_created_objects) {
        require(!original.count(origin.id) && origin.generation>baseline.world.generation_sequence,
            "retired creation is not fresh outside property baseline");
        const auto* definition=index.unique(origin.guid);
        require(definition && definition->source==origin.source && definition->asset==origin.asset &&
            definition->chunk_id==origin.definition_chunk_id && !definition->definition.is_multitile &&
            !definition->definition.master_id,"retired creation resource provenance differs from property resources");
    }
    result.reservations.insert(result.reservations.end(),bundle.object_history->reserved_entity_ids.begin(),bundle.object_history->reserved_entity_ids.end());
    result.reservations.insert(result.reservations.end(),limits.world.reserved_entity_ids.begin(),limits.world.reserved_entity_ids.end());
    for(const auto& [id,origin]:removed){(void)origin;result.reservations.push_back(id);}
    std::sort(result.reservations.begin(),result.reservations.end());
    result.reservations.erase(std::unique(result.reservations.begin(),result.reservations.end()),result.reservations.end());
    require(result.reservations.size()<=4096 && (result.reservations.empty() || result.reservations.front()>0),
        "property baseline reservation union exceeds limits");
    return result;
}
}
void PreparedPropertyCheckpoint::verify_overlay(const FamilyOverlay& observed) const {
    require(encode_family_overlay(observed)==expected_overlay_,"property preparation overlay changed");
}
PreparedPropertyCheckpoint prepare_property_checkpoint(const NeighborhoodFamilySource& source,
    const FamilyOverlay& expected,std::uint16_t family_id,const std::vector<std::uint8_t>& bytes,
    const SessionResourceIdentity& resources,const DefinitionIndex& definitions,
    const PropertyCheckpointBaseline& baseline,
    const SavedWorldLimits& limits,const SessionBundleLimits& codec_limits,const SessionCreatedObjectResolver& resolve) {
    require(bytes.size()<=codec_limits.archive.max_file_bytes,"property checkpoint exceeds archive limit");
    const auto replay=replay_family_overlay(source.families,source.identity,expected);
    const auto state=std::find_if(replay.states.begin(),replay.states.end(),
        [&](const auto& value){return value.family_chunk_id==family_id;});
    const auto family=std::find_if(replay.families.begin(),replay.families.end(),
        [&](const auto& value){return value.chunk_id==family_id;});
    require(state!=replay.states.end() && family!=replay.families.end(),"property family is absent");
    require(state->managed && state->house_number && state->occupancy_epoch && state->checkpoint_sha256 &&
        state->checkpoint_house==state->house_number && state->checkpoint_epoch==state->occupancy_epoch &&
        state->checkpoint_budget==state->budget,"property requires the current occupied checkpoint");
    require(sha256(bytes.data(),bytes.size())==*state->checkpoint_sha256,"property checkpoint digest differs from current head");
    const auto bundle=decode_session_bundle(bytes,resources,codec_limits);
    SessionFamilyLease lease{source.identity,state->family_identity,state->occupancy_epoch,family_id};
    require(bundle.family_lease && same_lease(*bundle.family_lease,lease) &&
        bundle.host.house_number==state->house_number && bundle.host.family_chunk_id==family_id &&
        bundle.host.family_number==family->family.family_number && bundle.host.family_budget==state->budget,
        "property checkpoint differs from current family occupancy or finances");
    std::vector<std::uint32_t> expected_guids=family->family.member_guids,saved_guids;
    for(const auto& member:bundle.household_roster)saved_guids.push_back(member.guid);
    std::sort(expected_guids.begin(),expected_guids.end());std::sort(saved_guids.begin(),saved_guids.end());
    require(!expected_guids.empty() && expected_guids.size()<=8 && expected_guids==saved_guids &&
        std::adjacent_find(expected_guids.begin(),expected_guids.end())==expected_guids.end(),
        "property checkpoint roster differs from effective family");
    const auto derived=derive_baseline(baseline,bundle,definitions,limits,resolve);
    auto survivor_limits=limits;survivor_limits.world.reserved_entity_ids=derived.reservations;
    World working(survivor_limits.world,bundle.world.attribute_policy);
    require(working.restore_snapshot(bundle.world),"property checkpoint World exceeds preparation bounds");
    RelationshipState relationships(limits.relationships);
    require(relationships.restore_snapshot(bundle.relationships),"property checkpoint relationships exceed preparation bounds");
    std::vector<PropertyDepartingActor> approvals;
    for(const auto& member:bundle.household_roster) {
        const auto* entity=working.find(member.id);
        require(entity && entity->kind==EntityKind::avatar && entity->generation==member.generation &&
            entity->definition && entity->definition->guid==member.guid,
            "property roster does not name exact current residents");
        approvals.push_back({member.id,member.generation,member.guid});
    }
    std::vector<std::int16_t> seeds;
    // Eviction004273c8 scans each entity and calls0043557a; unlike the value
    // collector00444cf9 it does not first restrict the predicate to group roots.
    for(const auto& entity:bundle.world.entities) {
        require(entity.definition.has_value(),"property disposition lacks current identity");
        const auto* own=definitions.unique(entity.definition->guid);
        require(own!=nullptr,"property disposition definition is missing or ambiguous");
        const auto flags=entity.object_words.find(43);
        const auto disposition=inspect_property_disposition(definitions,entity.definition->guid,own->definition.object_type,
            flags==entity.object_words.end()?std::optional<std::uint16_t>{}:
                std::optional<std::uint16_t>{static_cast<std::uint16_t>(flags->second)});
        require(disposition.disposition!=PropertyDisposition::unresolved,"property disposition is unresolved");
        if(disposition.disposition==PropertyDisposition::remove)seeds.push_back(entity.id);
    }
    PreparedPropertyCheckpoint result;
    result.survivors_=prepare_property_survivors(working,relationships,definitions,derived.groups,true,seeds,approvals,survivor_limits);
    result.lease_=lease;result.resources_=bundle.resources;result.checkpoint_sha256_=*state->checkpoint_sha256;
    result.house_number_=state->house_number;result.budget_=state->budget;
    result.installed_house_sha256_=baseline.installed_house_sha256;
    result.retired_created_objects_=bundle.object_history->retired_created_objects;
    const std::set<std::int16_t> retired(result.survivors_.removed_ids.begin(),result.survivors_.removed_ids.end());
    for(const auto& origin:bundle.created_objects) {
        if(retired.count(origin.id))result.retired_created_objects_.push_back(origin);
        else result.created_objects_.push_back(origin);
    }
    std::sort(result.retired_created_objects_.begin(),result.retired_created_objects_.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    result.removed_original_objects_=bundle.removed_original_objects;
    for(const auto& entity:baseline.world.entities)if(entity.kind==EntityKind::object && retired.count(entity.id))
        result.removed_original_objects_.push_back({entity.id,entity.generation,entity.definition->guid});
    for(const auto& entity:bundle.world.entities)if(retired.count(entity.id))
        result.retired_entities_.push_back({entity.id,entity.generation,entity.definition->guid});
    std::sort(result.removed_original_objects_.begin(),result.removed_original_objects_.end(),
        [](const auto& a,const auto& b){return a.id<b.id;});
    result.expected_overlay_=encode_family_overlay(expected);return result;
}
} // namespace odh
