#pragma once

#include "odh/world.hpp"

#include <cstdint>
#include <functional>
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

} // namespace odh
