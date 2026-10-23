#pragma once

#include "odh/avatar_neighbor.hpp"
#include "odh/world.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace odh {

// Candidate order used by the TS1 mailbox-based family arrival search.
// Positions are in sixteenth-tile units. Callers must still apply lot
// placement and collision rules before using a candidate.
std::vector<EntityPosition> household_spawn_candidates(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height);

// Return the first candidate accepted by the caller's placement validator,
// matching FindLocationFor's stop-on-success behavior. The callback must
// perform the full placement check; World::set_position alone is insufficient.
std::optional<EntityPosition> first_household_spawn_candidate(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height,
    const std::function<bool(const EntityPosition&)>& accepts);

struct HouseholdSpawnRequest {
    std::uint32_t guid;
    const NeighborRecord* neighbor;
};
struct MaterializedHouseholdMember {
    std::uint32_t guid;
    std::int16_t runtime_id;
};
struct HouseholdSpawnResult {
    std::unique_ptr<World> candidate;
    std::map<std::uint32_t, std::int16_t> runtime_ids;
    std::vector<MaterializedHouseholdMember> new_members;
};

// Copies the supplied owned WorldSnapshot and adds missing FAMI avatars to a
// private candidate. This stage returns those avatars without positions so a
// caller can run exact Init/EP8 and build their runtime controllers before
// testing arrival. It does not execute BHAVs, run the scheduler, or persist
// SavedWorld/OBJM/OBJT records. The callback binds exact character resources
// and metadata; callers remain responsible for successful runtime startup.
// Callback failure throws FormatError; callback exceptions propagate. Failure
// discards the private candidate and leaves source untouched.
using HouseholdAvatarPreparer = std::function<bool(World&, std::int16_t, std::uint32_t)>;
using HouseholdArrivalResolver = std::function<std::optional<EntityPosition>(
    const World&, std::int16_t, std::uint32_t)>;
HouseholdSpawnResult stage_household_members(
    const WorldSnapshot& source,
    const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    WorldLimits limits = {});

// Convenience path for callers that already prepared each avatar completely.
// Arrival must perform full placement validation; World::set_position is only
// applied after acceptance and is not itself the validator.
HouseholdSpawnResult materialize_household_members(
    const WorldSnapshot& source,
    const std::vector<HouseholdSpawnRequest>& household,
    std::uint16_t family_chunk_id,
    const HouseholdAvatarPreparer& prepare_avatar,
    const HouseholdArrivalResolver& resolve_arrival,
    WorldLimits limits = {});

} // namespace odh
