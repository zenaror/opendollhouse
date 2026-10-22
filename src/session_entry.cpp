#include "odh/session_entry.hpp"
#include "odh/formats.hpp"

#include <map>
#include <set>

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

std::vector<ReconciledHouseholdMember> reconcile_session_household(
    const SessionEntry& entry, const World& world) {
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
    result.reserve(entry.members.size());
    std::set<std::uint32_t> seen;
    for (const auto& member : entry.members) {
        if (!seen.insert(member.guid).second)
            throw FormatError("selected household contains a duplicate Sim GUID");
        const auto found = avatars_by_guid.find(member.guid);
        if (found == avatars_by_guid.end()) {
            result.push_back({member.guid, HouseholdMemberPresence::absent,
                              std::nullopt, std::nullopt});
            continue;
        }
        const auto* avatar = found->second;
        if (avatar->position) {
            result.push_back({member.guid, HouseholdMemberPresence::present_with_position,
                              avatar->id, avatar->position});
        } else {
            result.push_back({member.guid, HouseholdMemberPresence::present_without_position,
                              avatar->id, std::nullopt});
        }
    }
    return result;
}

} // namespace odh
