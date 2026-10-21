#pragma once

#include "odh/saved_architecture.hpp"
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {

struct PreparedArchitecture;

// Simitone's GetPreciseFloor profile for positions in 1/16-tile units.
// A missing result means the position, level, or retained grid is unsupported.
// Floor pattern 0 is a valid value and is therefore not used as a sentinel.
std::optional<std::uint16_t> saved_precise_floor(
    const SavedArchitecture&, std::int32_t x, std::int32_t y, std::int16_t level);

// Same coordinate/diagonal selection over VMTS1ActivatorNew's prepared grids.
// Alias misses remain visible through PreparedArchitecture diagnostics; this
// query returns the retained prepared value and does not certify floor validity.
std::optional<std::uint16_t> prepared_precise_floor(
    const PreparedArchitecture&, std::int32_t x, std::int32_t y, std::int16_t level);

enum class WallEdgeKnowledge : std::uint8_t { unresolved, non_solid, solid };
struct ResolvedWallEdges {
    WallEdgeKnowledge top_left = WallEdgeKnowledge::unresolved;
    WallEdgeKnowledge top_right = WallEdgeKnowledge::unresolved;
};
enum class WallRaycastState : std::uint8_t { clear, blocked, unsupported };
struct WallRaycastResult {
    WallRaycastState state = WallRaycastState::unsupported;
    std::int32_t tile_x = -1;
    std::int32_t tile_y = -1;
    bool top_left = false;
};

// Bounded port of VMArchitecture.RaycastWall for full-tile coordinates. Door
// presence is not in SavedWallTile, so every crossed wall edge needs explicit
// solidity knowledge. Diagonal segments are ignored, as in the reference
// RaycastWall; this function is not the full VMAvatar placement test.
WallRaycastResult saved_wall_raycast(
    const SavedArchitecture&, std::int16_t level,
    std::int32_t start_x, std::int32_t start_y,
    std::int32_t end_x, std::int32_t end_y,
    const std::vector<ResolvedWallEdges>&);

} // namespace odh
