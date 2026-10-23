#pragma once

#include "odh/world.hpp"

#include <cstdint>
#include <vector>

namespace odh {

// Candidate order used by the TS1 mailbox-based family arrival search.
// Positions are in sixteenth-tile units. Callers must still apply lot
// placement and collision rules before using a candidate.
std::vector<EntityPosition> household_spawn_candidates(
    EntityPosition mailbox, std::uint16_t lot_width, std::uint16_t lot_height);

} // namespace odh
