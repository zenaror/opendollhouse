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
    // Always false: saved geometry has no live architectural-door/portal state,
    // and the room map may still use unresolved floor resource IDs.
    bool complete = false;
    std::vector<RoomObstacleRect> rectangles;
};

// Geometric port of VMRoomMap.GenerateRoomObs's room-membership raster and
// rectangle optimization. Every tile outside `room` is conservatively kept
// blocked. Missing object/portal evidence is never interpreted as an opening.
// This is a candidate obstacle set, not route clearance or runtime acceptance.
// Throws std::runtime_error for malformed maps, room IDs, or level selection.
RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level);

} // namespace odh
