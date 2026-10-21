#include "odh/world_attempt_walk.hpp"

namespace odh {
namespace {
AttemptWalkObstacleSnapshot unsupported(AttemptWalkObstacleReason reason) {
    AttemptWalkObstacleSnapshot result;
    result.reason = reason;
    return result;
}

std::optional<std::uint16_t> flags_for(const World& world, std::int16_t id) {
    const VmFrame frame{routine_key(4096), 0, {id, id, id, 0}, {}, {}};
    const auto flags = world.read(frame, 3, 8);
    if (!flags) return std::nullopt;
    return static_cast<std::uint16_t>(*flags);
}

std::optional<std::uint16_t> entry_point5_for(const Entity& entity) {
    if (!entity.entry_points || entity.entry_points->size() <= 5) return std::nullopt;
    return (*entity.entry_points)[5].action;
}

bool same_inventory(const RoomInfoObstacleSnapshot& a,
                    const RoomInfoObstacleSnapshot& b) {
    if (a.status != b.status || a.reason != b.reason ||
        a.obstacles.size() != b.obstacles.size()) return false;
    for (std::size_t i = 0; i < a.obstacles.size(); ++i) {
        const auto& x = a.obstacles[i];
        const auto& y = b.obstacles[i];
        if (x.owner_id != y.owner_id || x.room != y.room ||
            x.collection != y.collection || x.box.x1 != y.box.x1 ||
            x.box.y1 != y.box.y1 || x.box.x2 != y.box.x2 ||
            x.box.y2 != y.box.y2) return false;
    }
    return true;
}
} // namespace

AttemptWalkObstacleSnapshot build_world_attempt_walk_obstacles(
    const World& world, const DefinitionIndex& definitions,
    const RuntimeRoomResolver& room_for,
    const AvatarKillTimeoutResolver& kill_timeout_for,
    const GeometricRoomMap& rooms, std::uint16_t room, std::uint8_t level,
    std::int16_t actor_id, const RoomObstacleCandidateSet& room_obstacles,
    const WallObstacleCandidateSet& wall_obstacles,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const AttemptWalkEntryPoint5Resolver& resolve_entry_point5,
    const AttemptWalkObstacleLimits& limits) {
    if (actor_id <= 0) return unsupported(AttemptWalkObstacleReason::invalid_actor);
    if (!limits.entities || limits.entities > 4096)
        return unsupported(AttemptWalkObstacleReason::entity_inventory_over_budget);

    const auto* actor = world.find(actor_id);
    if (!actor || actor->kind != EntityKind::avatar || !actor->position)
        return unsupported(AttemptWalkObstacleReason::invalid_actor);
    const auto actor_flags = flags_for(world, actor_id);
    if (!actor_flags) return unsupported(AttemptWalkObstacleReason::entity_flags_unknown);

    const auto inventory = snapshot_world_room_info(world, definitions, room_for,
        kill_timeout_for, room, limits.entities);
    if (inventory.status != RoomInfoObstacleIndexStatus::complete)
        return unsupported(inventory.reason == RoomInfoObstacleIndexReason::inventory_over_budget
            ? AttemptWalkObstacleReason::entity_inventory_over_budget
            : AttemptWalkObstacleReason::entity_inventory_incomplete);

    std::vector<AttemptWalkObstacleEntity> entities;
    entities.reserve(inventory.obstacles.size());
    for (const auto& obstacle : inventory.obstacles) {
        if (obstacle.owner_id <= 0 || obstacle.owner_id > 32767)
            return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
        const auto id = static_cast<std::int16_t>(obstacle.owner_id);
        const auto* entity = world.find(id);
        if (!entity || !entity->position)
            return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
        AttemptWalkObstacleEntity candidate;
        candidate.id = id;
        candidate.avatar = entity->kind == EntityKind::avatar;
        candidate.room = obstacle.room;
        candidate.footprint_state = AttemptWalkFootprintState::present;
        candidate.footprint = obstacle.box;
        candidate.flags = flags_for(world, id);
        candidate.entry_point5_action = entry_point5_for(*entity);
        entities.push_back(candidate);
    }

    struct EntityState {
        std::int16_t id;
        std::uint64_t generation;
        EntityPosition position;
        std::optional<std::uint16_t> flags;
        std::optional<std::uint16_t> entry_point5;
    };
    std::vector<EntityState> captured;
    captured.reserve(entities.size());
    for (const auto& entity : entities) {
        const auto* current = world.find(entity.id);
        if (!current || !current->position)
            return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
        captured.push_back({entity.id, current->generation, *current->position,
                            flags_for(world, entity.id), entry_point5_for(*current)});
    }
    const auto actor_generation = actor->generation;
    const auto actor_position = *actor->position;
    const auto actor_action = entry_point5_for(*actor);

    auto result = build_attempt_walk_obstacles(rooms, room, level, actor_position,
        actor_id, *actor_flags, actor_action, room_obstacles, wall_obstacles,
        entities, true, avatar_consideration_complete, avatars_to_consider,
        resolve_entry_point5, limits);

    const auto current_inventory = snapshot_world_room_info(world, definitions,
        room_for, kill_timeout_for, room, limits.entities);
    const auto* current_actor = world.find(actor_id);
    if (!same_inventory(inventory, current_inventory) || !current_actor ||
        current_actor->generation != actor_generation || !current_actor->position ||
        !(*current_actor->position == actor_position) ||
        flags_for(world, actor_id) != actor_flags ||
        entry_point5_for(*current_actor) != actor_action)
        return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
    for (const auto& before : captured) {
        const auto* current = world.find(before.id);
        if (!current || current->generation != before.generation || !current->position ||
            !(*current->position == before.position) ||
            flags_for(world, before.id) != before.flags ||
            entry_point5_for(*current) != before.entry_point5)
            return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
    }
    return result;
}

} // namespace odh
