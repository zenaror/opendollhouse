#pragma once

#include "odh/room_map.hpp"
#include <cstdint>
#include <vector>

namespace odh {

// VMObstacle's coordinates are in 1/16-tile units. The source expands each
// blocked tile by three units on every side before joining adjacent runs.
struct RoomObstacleRect {
    std::int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct RoomObstacleCandidateSet {
    std::uint16_t room = 0;
    std::uint8_t level = 0;
    // Covers room-obstacle generation only. It does not include object/avatar
    // collision or mean that routing/placement clearance is complete.
    bool complete = false;
    std::vector<RoomObstacleRect> rectangles;
};
struct RoomPortalOpening {
    std::uint16_t target_room = 0;
    std::uint32_t tile_x = 0, tile_y = 0;
    std::uint8_t tile_level = 0;
};

// Geometric port of VMRoomMap.GenerateRoomObs's room-membership raster and
// rectangle optimization. Every tile outside `room` is conservatively kept
// blocked. Missing object/portal evidence is never interpreted as an opening.
// This is a candidate obstacle set, not route clearance or runtime acceptance.
// Throws std::runtime_error for malformed maps, room IDs, or level selection.
RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level);

// Snapshot-only variant. `portal_openings` must contain every architectural
// door tile/group member that is a Portal into `target_room`; each such tile is
// omitted from that room's boundary obstacles, matching GenerateRoomObs. The
// caller asserts inventory completeness explicitly. Result.complete is true
// only for a prepared room map with resolved floor lookups and a complete
// portal inventory; it still says nothing about entity collisions or routing.
RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level,
    const std::vector<RoomPortalOpening>& portal_openings,
    bool portal_inventory_complete);

} // namespace odh
