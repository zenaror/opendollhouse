#pragma once

#include "odh/floor_placement.hpp"
#include "odh/route_geometry.hpp"
#include "odh/saved_world.hpp"

namespace odh {

enum class SavedFloorPlacementState : std::uint8_t { allowed, rejected, unsupported };

struct SavedFloorPlacementCheck {
    SavedFloorPlacementState state = SavedFloorPlacementState::unsupported;
    std::optional<std::uint16_t> floor;
    std::optional<std::uint16_t> placement_flags;
    std::optional<std::uint16_t> level_offset;
    std::optional<FloorPlacementResult> rule_result;
};

// Applies only the saved entity's FloorChangeValid gate at a prepared lot
// position. Missing OBJD LevelOffset, ambiguous prepared floor identity, or
// unsupported floor coordinates return unsupported. Passing this check does
// not establish placement or route safety.
SavedFloorPlacementCheck check_saved_floor_placement(
    const SavedWorldEntity&, const PreparedArchitecture&,
    std::int32_t x, std::int32_t y, std::int16_t level);

} // namespace odh
