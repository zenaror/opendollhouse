#pragma once

#include "odh/saved_attempt_walk_obstacles.hpp"
#include "odh/world_room_info.hpp"

namespace odh {

// Rebuilds the current RoomInfo inventory from World, preserves each live
// footprint/collection owner, and feeds those entities into the existing
// AttemptWalk policy composer with caller-supplied architectural obstacles.
// The caller must provide a complete avatar consideration set and a bounded,
// side-effect-free EP5 resolver. This does not prove portal/door state or run a
// route search.
AttemptWalkObstacleSnapshot build_world_attempt_walk_obstacles(
    const World&, const DefinitionIndex&, const RuntimeRoomResolver&,
    const AvatarKillTimeoutResolver&, const GeometricRoomMap&,
    std::uint16_t room, std::uint8_t level, std::int16_t actor_id,
    const RoomObstacleCandidateSet&, const WallObstacleCandidateSet&,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const AttemptWalkEntryPoint5Resolver& resolve_entry_point5 = {},
    const AttemptWalkObstacleLimits& limits = {});

} // namespace odh
