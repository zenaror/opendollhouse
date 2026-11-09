#include "odh/session_entry.hpp"
#include "odh/formats.hpp"
#include "odh/session_bundle.hpp"

#include <map>
#include <set>
#include <algorithm>

namespace odh {

SessionEntry resolve_session_entry(unsigned mode, std::uint16_t house,
    std::string house_file, const LotLocations& locations,
    const FamilyContext& families, const NeighborContext& neighbors,
    const DefinitionIndex& characters, bool identity_only) {
    if (!neighborhood_mode_supported(mode))
        throw FormatError("selected neighborhood mode is unsupported");
    if (house == 0 || house >= 99)
        throw FormatError("selected house is outside the selectable range");
    if (house_file.empty()) throw FormatError("selected house path is empty");

    std::size_t location_matches = 0;
    for (const auto& location : locations.lots)
        if (location.house == house) ++location_matches;
    if (location_matches != 1)
        throw FormatError("selected house is absent or ambiguous in LotLocations");

    const auto* family = families.by_house(static_cast<std::int16_t>(house));
    if (!family) throw FormatError("selected house has no resident family");
    SessionEntry result{mode, house, std::move(house_file), family->chunk_id,
                        family->family.family_number, {}};
    if (identity_only) return result;
    if (family->family.member_guids.empty()) throw FormatError("selected family has no members without an accepted continuation");
    result.members.reserve(family->family.member_guids.size());
    std::set<std::uint32_t> seen;
    for (const auto guid : family->family.member_guids) {
        if (!seen.insert(guid).second)
            throw FormatError("selected family contains a duplicate Sim GUID");
        const auto neighbor_id = neighbors.id_for_guid(guid);
        if (!neighbor_id)
            throw FormatError("family Sim GUID has no active neighborhood record");
        const auto* neighbor = neighbors.by_id(*neighbor_id);
        if (!neighbor || neighbor->guid != guid)
            throw FormatError("family Sim GUID does not match its neighborhood record");

        const auto* candidates = characters.candidates(guid);
        const DefinitionCandidate* character = nullptr;
        if (candidates) {
            for (const auto& candidate : *candidates) {
                const auto& definition = candidate.definition;
                if (definition.version != 138 || definition.object_type != 2) continue;
                if (character)
                    throw FormatError("family Sim GUID has ambiguous character resources");
                character = &candidate;
            }
        }
        if (!character)
            throw FormatError("family Sim GUID has no matching character resource");
        result.members.push_back({guid, *neighbor_id, character->source,
                                  character->asset, character->chunk_id});
    }
    return result;
}

SessionEntry resolve_session_execution_entry(const SessionEntry& current,
    const SessionBundle& saved, const NeighborContext& neighbors,
    const DefinitionIndex& characters) {
    if (!saved.generic_execution || (saved.household_roster.empty()&&!saved.npc_primary) ||
        saved.host.house_number != current.house_number ||
        saved.host.neighborhood_mode != current.neighborhood_mode ||
        saved.host.family_chunk_id != current.family_chunk_id ||
        saved.host.family_number != current.family_number)
        throw FormatError("accepted execution checkpoint differs from the selected residence");
    if(saved.npc_primary){
        if(!saved.character_family||!saved.travel||
           !saved.household_roster.empty()||!saved.runtime_entities.empty())
            throw FormatError("NPC execution entry lacks exact checkpoint provenance");
        const auto ref=*saved.npc_primary;
        const auto npc=std::find_if(saved.npc_avatars.begin(),saved.npc_avatars.end(),[&](const auto& n){return n.origin.id==ref.id&&n.origin.generation==ref.generation;});
        if(npc==saved.npc_avatars.end()||ref.id!=saved.host.runtime_avatar_id||npc->origin.guid!=saved.host.resident_guid||npc->origin.neighbor_id!=saved.host.resident_neighbor_id||
           std::count_if(saved.npc_avatars.begin(),saved.npc_avatars.end(),[&](const auto& n){return n.origin.id==ref.id;})!=1)
            throw FormatError("NPC execution primary differs from HOST");
        if(std::count_if(saved.world.entities.begin(),saved.world.entities.end(),[&](const auto& e){return e.id==ref.id&&e.generation==ref.generation&&
            e.kind==EntityKind::avatar&&!e.dead&&!e.ui_ghost&&e.definition&&e.definition->guid==npc->origin.guid&&
            e.person_data.size()>31&&e.person_data[31]==npc->origin.neighbor_id;})!=1)
            throw FormatError("NPC execution primary differs from the exact saved World identity");
        const auto& o=npc->origin;const auto* neighbor=neighbors.by_id(o.neighbor_id);
        if(!neighbor||neighbor->guid!=o.guid)throw FormatError("NPC execution primary differs from replayed neighbor catalog");
        if(saved.personal_state&&saved.personal_state->catalog){
        const auto& definitions=saved.personal_state->catalog->definitions;
        const bool catalogued=std::count_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.guid==o.guid&&d.source==o.source&&d.asset==o.asset&&d.definition_chunk_id==o.object_definition_id&&d.fingerprint==o.character_fingerprint;})==1;
        const bool created=std::count_if(saved.character_family->shared.characters.begin(),saved.character_family->shared.characters.end(),[&](const auto& c){return c.identity.guid==o.guid&&c.identity.neighbor_id==o.neighbor_id&&c.identity.definition_chunk_id==o.object_definition_id&&c.identity.character_asset==native_personal_resource_key(o.source,o.asset)&&c.identity.character_fingerprint==o.character_fingerprint;})==1;
        if(!catalogued&&!created)throw FormatError("NPC execution primary differs from exact catalog/creation resource");
        }
        SessionEntry out=current;out.members.clear();out.npc_primary=SessionNpcExecutionAnchor{{o.guid,o.neighbor_id,o.source,o.asset,o.object_definition_id},ref,o.character_fingerprint};return out;
    }
    // A legacy archive has no changed-roster receipt. Its strict equality
    // contract remains intact; CFAM is required for a different current FAMI.
    if (!saved.character_family) {
        if (current.members.size() != saved.household_roster.size())
            throw FormatError("legacy execution roster differs from current family");
        for (const auto& member : saved.household_roster)
            if (std::none_of(current.members.begin(),current.members.end(),[&](const auto& m){
                return m.guid==member.guid && m.neighbor_id==member.neighbor_id;
            })) throw FormatError("legacy execution member is outside current family");
    }
    SessionEntry out=current; out.members.clear();
    std::set<std::uint32_t> guids; std::set<std::int16_t> ids;
    for (const auto& member : saved.household_roster) {
        const auto* neighbor=neighbors.by_id(member.neighbor_id);
        if (!guids.insert(member.guid).second || !ids.insert(member.neighbor_id).second ||
            !neighbor || neighbor->guid!=member.guid)
            throw FormatError("accepted execution member differs from its exact NBRS identity");
        const DefinitionCandidate* exact=nullptr;
        if (const auto* choices=characters.candidates(member.guid)) for (const auto& candidate:*choices)
            if (candidate.definition.version==138 && candidate.definition.object_type==2 &&
                candidate.chunk_id==member.object_definition_id &&
                candidate.source+"/"+candidate.asset==member.character_asset) {
                if (exact) throw FormatError("accepted execution character is ambiguous");
                exact=&candidate;
            }
        if (!exact) throw FormatError("accepted execution character resource differs from HHRD");
        out.members.push_back({member.guid,member.neighbor_id,exact->source,exact->asset,exact->chunk_id});
    }
    const auto primary=std::find_if(saved.household_roster.begin(),saved.household_roster.end(),[&](const auto& member){
        return member.id==saved.host.runtime_avatar_id && member.guid==saved.host.resident_guid &&
            member.neighbor_id==saved.host.resident_neighbor_id;
    });
    if (primary==saved.household_roster.end()) throw FormatError("accepted execution primary lacks its exact HHRD origin");
    return out;
}

SessionEntry resolve_visit_session_entry(const SessionEntry& home,
    unsigned mode, std::uint16_t house, std::string file,
    const LotLocations& locations, const std::vector<std::uint32_t>& travelers) {
    if (!neighborhood_mode_supported(mode) || !house || house >= 99 ||
        house == home.house_number || file.empty())
        throw FormatError("invalid temporary visit destination; home uses the return path");
    if (!home.family_chunk_id || home.members.empty() || home.members.size() > 8 ||
        travelers.empty() || travelers.size() > home.members.size())
        throw FormatError("temporary visit requires an origin household and traveler subset");
    if (std::count_if(locations.lots.begin(), locations.lots.end(),
            [&](const auto& lot) { return lot.house == house; }) != 1)
        throw FormatError("temporary visit destination is absent or ambiguous in LotLocations");
    std::set<std::uint32_t> home_guids, selected;
    std::set<std::int16_t> home_neighbors;
    for (const auto& member : home.members)
        if (!member.guid || member.neighbor_id <= 0 || member.character_asset.empty() ||
            !home_guids.insert(member.guid).second || !home_neighbors.insert(member.neighbor_id).second)
            throw FormatError("temporary visit origin has ambiguous member identities");
    SessionEntry result{mode, house, std::move(file), home.family_chunk_id, home.family_number, {}};
    for (const auto guid : travelers) {
        if (!selected.insert(guid).second)
            throw FormatError("temporary visit contains duplicate travelers");
        const auto found = std::find_if(home.members.begin(), home.members.end(),
            [&](const auto& member) { return member.guid == guid; });
        if (found == home.members.end())
            throw FormatError("temporary visitor is not a member of the permanent household");
        result.members.push_back(*found);
    }
    return result;
}

std::vector<ReconciledHouseholdMember> reconcile_session_household(
    const SessionEntry& entry, const World& world) {
    std::vector<std::uint32_t> member_guids;
    member_guids.reserve(entry.members.size());
    for (const auto& member : entry.members) member_guids.push_back(member.guid);
    return reconcile_session_household(member_guids, world);
}

std::vector<ReconciledHouseholdMember> reconcile_session_household(
    const std::vector<std::uint32_t>& member_guids, const World& world) {
    std::map<std::uint32_t, const Entity*> avatars_by_guid;
    auto cursor = std::int16_t{0};
    while (const auto id = world.next_entity(cursor, EntityKind::avatar)) {
        cursor = *id;
        const auto* avatar = world.find(*id);
        if (!avatar || !avatar->definition)
            throw FormatError("restored World contains an avatar without an exact GUID");
        if (!avatars_by_guid.emplace(avatar->definition->guid, avatar).second)
            throw FormatError("restored World contains duplicate avatar GUIDs");
    }

    std::vector<ReconciledHouseholdMember> result;
    result.reserve(member_guids.size());
    std::set<std::uint32_t> seen;
    for (const auto guid : member_guids) {
        if (!seen.insert(guid).second)
            throw FormatError("selected household contains a duplicate Sim GUID");
        const auto found = avatars_by_guid.find(guid);
        if (found == avatars_by_guid.end()) {
            result.push_back({guid, HouseholdMemberPresence::absent,
                              std::nullopt, std::nullopt});
            continue;
        }
        const auto* avatar = found->second;
        if (avatar->position) {
            result.push_back({guid, HouseholdMemberPresence::present_with_position,
                              avatar->id, avatar->position});
        } else {
            result.push_back({guid, HouseholdMemberPresence::present_without_position,
                              avatar->id, std::nullopt});
        }
    }
    return result;
}

} // namespace odh
