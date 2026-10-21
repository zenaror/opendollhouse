#pragma once

#include "odh/definition_index.hpp"
#include "odh/room_info_obstacles.hpp"
#include "odh/world.hpp"

#include <functional>
#include <optional>

namespace odh {

// Resolves the current runtime room for a positioned entity. A saved geometric
// room map may be used only when it is known to match the runtime room map.
using RuntimeRoomResolver = std::function<std::optional<std::uint16_t>(EntityPosition)>;

// Callers provide Simitone's VMAvatar.KillTimeout by stable identity, not by
// pointer. Unknown lifecycle state makes the query unsupported.
using AvatarKillTimeoutResolver = std::function<std::optional<std::int32_t>(
    std::int16_t entity_id, std::uint64_t generation)>;

// Rebuilds the bounded obstacle index from the current World before answering
// each query. This avoids stale snapshots without mutation observers. Unknown
// flags, definitions, rooms, or avatar lifecycle state make the query unsupported.
// target_id is the avatar being placed, matching RoomInfoObstacleIndex::query.
// Resolvers must be deterministic and side-effect-free. World mutations during
// resolution are detected and make the query unsupported.
RoomInfoObstacleQuery query_world_room_info(
    const World&, const DefinitionIndex&, const RuntimeRoomResolver&,
    const AvatarKillTimeoutResolver&, std::uint16_t room,
    const CollisionBox& candidate, std::int32_t target_id,
    std::size_t entity_limit = 4096);

} // namespace odh
