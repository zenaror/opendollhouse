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
struct RoomPortalEdge {
    std::uint16_t source_room = 0;
    std::uint16_t target_room = 0;
    std::int16_t portal_id = 0;
    EntityPosition position{};
};
struct IgnoredRoomPortal {
    std::int16_t portal_id = 0;
    std::uint16_t target_room = 0;
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
    std::vector<RoomPortalEdge> edges;
};

enum class RoomPortalRouteStatus : std::uint8_t {
    found, no_path, invalid_input, budget_exhausted
};
struct RoomPortalRoute {
    RoomPortalRouteStatus status = RoomPortalRouteStatus::invalid_input;
    std::vector<RoomPortalEdge> portals;
    std::size_t expanded = 0;
    double cost = 0.0;
};
// Bounded A* over directed Portal15 room edges. The score follows Simitone's
// distance metric (XY Euclidean units plus 10 per floor); ignored entries
// identify one Portal object -> target-room connection.
RoomPortalRoute find_room_portal_route(
    const RoomPortalInventory&, std::uint16_t source_room,
    std::uint16_t target_room, EntityPosition start, EntityPosition destination,
    const std::vector<IgnoredRoomPortal>& ignored = {},
    std::size_t edge_limit = 4096, std::size_t expansion_limit = 4096,
    std::size_t frontier_limit = 4096);

// Builds room/tile openings from architectural-door groups and directed room
// edges from portal members. A missing/out-of-lot door or portal position,
// unknown room, duplicate ID, or tile claimed by different door groups makes
// the inventory incomplete. Edges follow the first other Portal member in
// group order, matching VMContext.AddRoomPortal.
RoomPortalInventory build_room_portal_inventory(
    const GeometricRoomMap&, const std::vector<RoomPortalMemberSnapshot>&);

// Bounded host-side index for live room/portal membership updates. Each
// accepted registration, movement or removal rebuilds a candidate inventory
// and then publishes it with the member set on the calling thread. An
// incomplete inventory is still published (fail-closed for routing); rejected
// capacity/identity operations leave the previous generation untouched. This
// type is not thread-safe. The host must feed all relevant runtime changes;
// this class is not connected to World callbacks. replace_members supports
// rebuilding the complete bounded snapshot without repeated per-member scans.
class DynamicRoomPortalInventory {
public:
    explicit DynamicRoomPortalInventory(GeometricRoomMap map,
                                        std::size_t member_limit = 4096);

    bool register_member(const RoomPortalMemberSnapshot&);
    bool update_member(const RoomPortalMemberSnapshot&);
    bool unregister_member(std::int16_t id);
    bool replace_members(const std::vector<RoomPortalMemberSnapshot>&);

    const RoomPortalInventory& current() const noexcept { return inventory_; }
    std::size_t member_count() const noexcept { return members_.size(); }
    std::uint64_t generation() const noexcept { return generation_; }

private:
    bool publish(std::map<std::int16_t, RoomPortalMemberSnapshot>&&,
                 std::vector<std::int16_t>&&);

    GeometricRoomMap map_;
    std::size_t member_limit_;
    std::map<std::int16_t, RoomPortalMemberSnapshot> members_;
    std::vector<std::int16_t> member_order_;
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
