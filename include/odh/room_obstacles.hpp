#pragma once

#include "odh/room_map.hpp"
#include "odh/route_geometry.hpp"
#include "odh/world.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
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

struct WallObstacleCandidateSet {
    std::uint8_t level = 0;
    // Geometric wallObs from saved architecture. A recognized door face omits
    // that wall segment; runtime door state and traversal are not represented.
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

// Bounded host-side index for live room/portal membership updates. Each
// accepted registration, movement or removal rebuilds a candidate inventory
// and then publishes it with the member set on the calling thread. An
// incomplete inventory is still published (fail-closed for routing); rejected
// capacity/identity operations leave the previous generation untouched. This
// type is not thread-safe. The host must feed all relevant runtime changes;
// this class is not connected to World callbacks.
class DynamicRoomPortalInventory {
public:
    explicit DynamicRoomPortalInventory(GeometricRoomMap map,
                                        std::size_t member_limit = 4096);

    bool register_member(const RoomPortalMemberSnapshot&);
    bool update_member(const RoomPortalMemberSnapshot&);
    bool unregister_member(std::int16_t id);

    const RoomPortalInventory& current() const noexcept { return inventory_; }
    std::size_t member_count() const noexcept { return members_.size(); }
    std::uint64_t generation() const noexcept { return generation_; }

private:
    bool publish(std::map<std::int16_t, RoomPortalMemberSnapshot>&&);

    GeometricRoomMap map_;
    std::size_t member_limit_;
    std::map<std::int16_t, RoomPortalMemberSnapshot> members_;
    RoomPortalInventory inventory_;
    std::uint64_t generation_ = 0;
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
// only for a prepared room map with unambiguous used floor identities and a
// complete portal inventory; undeclared IDs use the source-byte fallback. It
// still says nothing about entity collisions or route clearance.
RoomObstacleCandidateSet generate_room_obstacle_candidates(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level,
    const std::vector<RoomPortalOpening>& portal_openings,
    bool portal_inventory_complete);

// Ports VMRoomMap's wallObs rectangles for one saved architecture level.
// `resolved_edges` is row-major and must come from the same architecture
// snapshot. Present edges without recognized door evidence remain solid.
WallObstacleCandidateSet generate_wall_obstacle_candidates(
    const SavedArchitecture&, std::uint8_t level,
    const std::vector<ResolvedWallEdges>& resolved_edges,
    std::size_t rectangle_limit = 65536);

} // namespace odh
