#include "odh/session_entry.hpp"
#include "odh/formats.hpp"

#include <map>
#include <set>
#include <algorithm>

namespace odh {

SessionEntry resolve_session_entry(unsigned mode, std::uint16_t house,
    std::string house_file, const LotLocations& locations,
    const FamilyContext& families, const NeighborContext& neighbors,
    const DefinitionIndex& characters) {
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
    if (family->family.member_guids.empty())
        throw FormatError("selected family has no members");

    SessionEntry result{mode, house, std::move(house_file), family->chunk_id,
                        family->family.family_number, {}};
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
