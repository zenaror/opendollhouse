#pragma once

#include "odh/route.hpp"
#include "odh/room_map.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/saved_collision_candidates.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {

enum class SavedRoomRouteGridState : std::uint8_t { ready, unsupported };
enum class SavedRoomRouteGridReason : std::uint8_t {
    none,
    incomplete_entity_inventory,
    room_identity_invalid,
    room_bounds_invalid,
    room_obstacles_mismatch,
    room_obstacles_incomplete,
    wall_obstacles_incomplete,
    entity_inventory_over_budget,
    obstacle_inventory_over_budget,
    route_grid_over_budget,
    raster_work_budget_exhausted,
    entity_room_unknown,
    entity_geometry_unknown,
    coordinate_overflow
};

struct SavedRoomRouteGridLimits {
    std::size_t cells = 262144;
    std::size_t entities = 4096;
    std::size_t obstacle_rectangles = 16384;
    std::size_t rasterized_cells = 4194304;
};

struct SavedRoomRouteGrid {
    SavedRoomRouteGridState state = SavedRoomRouteGridState::unsupported;
    SavedRoomRouteGridReason reason = SavedRoomRouteGridReason::entity_room_unknown;
    std::optional<RouteGrid> grid;
    std::size_t blocked_by_room = 0;
    std::size_t blocked_by_room_obstacles = 0;
    std::size_t blocked_by_wall_obstacles = 0;
    std::size_t blocked_by_entities = 0;
    std::size_t skipped_outside_entities = 0;
    std::size_t rasterized_cells = 0;
};

// Builds a bounded, room-local *candidate* grid on the saved 1/16-tile
// lattice. Room membership, RoomObs and saved entity boxes are conservative
// blockers. An uncertain entity with a known box is blocked; an entity whose
// room/geometry cannot be established makes the whole result unsupported.
// This is not runtime clearance: it does not evaluate live flags, bilateral
// EntryPoint 5, door state, portal traversal, or VMRectRouter semantics.
SavedRoomRouteGrid build_saved_room_route_grid(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level,
    const RoomObstacleCandidateSet&,
    const std::vector<RoomObstacleRect>& wall_obstacles,
    bool wall_obstacles_complete,
    const std::vector<SavedCollisionCandidate>& entities,
    std::int16_t actor_id, bool entity_inventory_complete,
    const SavedRoomRouteGridLimits& limits = {});

const char* saved_room_route_grid_reason_name(SavedRoomRouteGridReason) noexcept;

} // namespace odh
