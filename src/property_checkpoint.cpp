#include "odh/property_checkpoint.hpp"
#include "odh/sha256.hpp"
#include "odh/property_disposition.hpp"
#include <algorithm>
#include <set>

namespace odh {
namespace {
void require(bool condition,const char* message){if(!condition)throw FormatError(message);}
bool same_lease(const SessionFamilyLease& a,const SessionFamilyLease& b) {
    return a.neighborhood_identity==b.neighborhood_identity && a.family_identity==b.family_identity &&
        a.occupancy_epoch==b.occupancy_epoch && a.family_chunk_id==b.family_chunk_id;
}
}
void PreparedPropertyCheckpoint::verify_overlay(const FamilyOverlay& observed) const {
    require(encode_family_overlay(observed)==expected_overlay_,"property preparation overlay changed");
}
PreparedPropertyCheckpoint prepare_property_checkpoint(const NeighborhoodFamilySource& source,
    const FamilyOverlay& expected,std::uint16_t family_id,const std::vector<std::uint8_t>& bytes,
    const SessionResourceIdentity& resources,const DefinitionIndex& definitions,
    const std::vector<SavedMultitileGroup>& groups,bool complete,
    const SavedWorldLimits& limits,const SessionBundleLimits& codec_limits) {
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
    World working(limits.world,bundle.world.attribute_policy);
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
    result.survivors_=prepare_property_survivors(working,relationships,definitions,groups,complete,seeds,approvals,limits);
    result.lease_=lease;result.resources_=bundle.resources;result.checkpoint_sha256_=*state->checkpoint_sha256;
    result.house_number_=state->house_number;result.budget_=state->budget;
    result.expected_overlay_=encode_family_overlay(expected);return result;
}
} // namespace odh
