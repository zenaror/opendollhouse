#pragma once

#include "odh/room_map.hpp"
#include "odh/world.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
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

// Minimal saved-World evidence used to conservatively enumerate Portal15
// openings. group_leader <= 0 denotes a singleton whose own id is its group.
struct RoomPortalMemberSnapshot {
    std::int16_t id = 0;
    std::int16_t group_leader = 0;
    bool architectural_door = false;
    bool portal = false;
    bool contained = false;
    std::optional<EntityPosition> position;
};

struct RoomPortalInventory {
    bool complete = true;
    std::size_t overlapping_door_tiles = 0;
    std::vector<RoomPortalOpening> openings;
};

// Builds room/tile openings from architectural-door groups. A missing or
// out-of-lot door/portal position, unknown room, duplicate ID, or tile claimed
// by different door groups makes the inventory incomplete. Overlapping doors
// still produce diagnostic openings, but callers must keep room obstacles
// incomplete because Simitone selects the first architectural door at a tile.
RoomPortalInventory build_room_portal_inventory(
    const GeometricRoomMap&, const std::vector<RoomPortalMemberSnapshot>&);

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
// only for a prepared room map with unambiguous used floor identities and a
// complete portal inventory; undeclared IDs use the source-byte fallback. It
// still says nothing about entity collisions or route clearance.
RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level,
    const std::vector<RoomPortalOpening>& portal_openings,
    bool portal_inventory_complete);

} // namespace odh
