#pragma once

#include "odh/collision.hpp"
#include "odh/room_map.hpp"
#include "odh/room_obstacles.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace odh {

enum class AttemptWalkFootprintState : std::uint8_t { present, absent, unsupported };

// Normalized read-only input from one entity already known to belong to the
// caller's RoomInfo.Entities snapshot. `room` remains explicit so callers must
// establish room membership instead of treating missing coordinates as empty.
struct AttemptWalkObstacleEntity {
    std::int16_t id = 0;
    bool avatar = false;
    std::optional<std::uint16_t> room;
    AttemptWalkFootprintState footprint_state = AttemptWalkFootprintState::unsupported;
    std::optional<CollisionBox> footprint;
    std::optional<std::uint16_t> flags;
    std::optional<std::uint16_t> entry_point5_action;
};

enum class AttemptWalkObstacleState : std::uint8_t { ready, unsupported };
enum class AttemptWalkObstacleReason : std::uint8_t {
    none,
    invalid_actor,
    actor_room_mismatch,
    room_obstacles_incomplete,
    wall_obstacles_incomplete,
    obstacle_set_mismatch,
    entity_inventory_incomplete,
    entity_inventory_over_budget,
    obstacle_inventory_over_budget,
    avatar_consideration_unknown,
    entity_room_unknown,
    entity_footprint_unknown,
    entity_flags_unknown,
    entry_point5_unknown,
    entry_point5_result_unknown,
    entry_point5_budget_exhausted,
    invalid_rectangle,
    coordinate_overflow
};

struct AttemptWalkObstacleLimits {
    std::size_t entities = 4096;
    std::size_t rectangles = 16384;
    std::size_t entry_point5_calls = 4096;
};

struct AttemptWalkObstacleSnapshot {
    AttemptWalkObstacleState state = AttemptWalkObstacleState::unsupported;
    AttemptWalkObstacleReason reason = AttemptWalkObstacleReason::entity_inventory_incomplete;
    std::vector<RoomObstacleRect> obstacles;
    std::vector<std::int16_t> entity_obstacle_ids;
    std::optional<std::int16_t> unsupported_entity_id;
    std::size_t entry_point5_calls = 0;
    std::size_t skipped_nonblocking_entities = 0;
    std::size_t exempted_by_entry_point5 = 0;
    bool start_intersects = false;
    std::int32_t room_min_x = 0, room_min_y = 0;
    std::int32_t room_max_x = 0, room_max_y = 0;
};

using AttemptWalkEntryPoint5Resolver =
    std::function<std::optional<bool>(std::int16_t caller, std::int16_t stack_object)>;

// Builds the obstacle snapshot assembled by Simitone VMRoutingFrame.AttemptWalk:
// WallObs + RoomObs, four one-tile room-bound bands, then eligible RoomInfo
// entity footprints expanded by three world units. This is only an input
// snapshot; it does not route, prove live registration/door state, or establish
// TS1 executable equivalence.
AttemptWalkObstacleSnapshot build_attempt_walk_obstacles(
    const GeometricRoomMap&, std::uint16_t room, std::uint8_t level,
    const EntityPosition& actor_position, std::int16_t actor_id,
    std::uint16_t actor_flags, std::optional<std::uint16_t> actor_entry_point5_action,
    const RoomObstacleCandidateSet& room_obstacles,
    const WallObstacleCandidateSet& wall_obstacles,
    const std::vector<AttemptWalkObstacleEntity>& entities,
    bool entity_inventory_complete,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const AttemptWalkEntryPoint5Resolver& resolve_entry_point5 = {},
    const AttemptWalkObstacleLimits& limits = {});

const char* attempt_walk_obstacle_reason_name(AttemptWalkObstacleReason) noexcept;

} // namespace odh
