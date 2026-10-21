#pragma once

#include "odh/route.hpp"
#include "odh/saved_attempt_walk_obstacles.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace odh {

enum class WorldRoomRouteGridState : std::uint8_t { ready, unsupported };
enum class WorldRoomRouteGridReason : std::uint8_t {
    none,
    obstacle_snapshot_incomplete,
    room_bounds_invalid,
    route_grid_over_budget,
    obstacle_invalid,
    raster_work_budget_exhausted
};

struct WorldRoomRouteGridLimits {
    std::size_t rectangles = 16384;
    std::size_t cells = 262144;
    std::size_t rasterized_cells = 262144;
};

struct WorldRoomRouteGrid {
    WorldRoomRouteGridState state = WorldRoomRouteGridState::unsupported;
    WorldRoomRouteGridReason reason = WorldRoomRouteGridReason::obstacle_snapshot_incomplete;
    std::optional<AttemptWalkObstacleReason> source_obstacle_reason;
    std::optional<RouteGrid> grid;
    std::size_t obstacle_rectangles = 0;
    std::size_t rasterized_cells = 0;
    std::size_t newly_blocked_cells = 0;
};

// Rasterize the already-composed AttemptWalk obstacle snapshot into a bounded
// 1/16-tile route grid. It preserves the snapshot's room fence bands and strict
// rectangle interior convention. This does not prove VMRectRouter equivalence.
WorldRoomRouteGrid build_world_room_route_grid(
    const AttemptWalkObstacleSnapshot&,
    const WorldRoomRouteGridLimits& = {});

const char* world_room_route_grid_reason_name(WorldRoomRouteGridReason) noexcept;

} // namespace odh
