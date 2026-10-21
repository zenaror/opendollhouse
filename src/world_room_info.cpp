#include "odh/world_room_info.hpp"

#include <vector>

namespace odh {
namespace {
constexpr std::int16_t flags_index = 8;
constexpr std::uint16_t has_zero_extent = 1U << 2;

RoomInfoObstacleQuery unsupported(RoomInfoObstacleIndexReason reason) {
    return {RoomInfoObstacleIndexStatus::unsupported, reason, {}};
}

struct ObstacleState {
    std::int16_t id;
    EntityKind kind;
    std::uint64_t generation;
    std::optional<EntityPosition> position;
    std::optional<Attachment> attachment;
    std::optional<std::uint32_t> guid;
    std::optional<std::int16_t> flags;
    bool static_footprint;
};

bool same_attachment(const std::optional<Attachment>& a,
                     const std::optional<Attachment>& b) noexcept {
    if (a.has_value() != b.has_value()) return false;
    return !a || (a->container == b->container && a->slot == b->slot &&
                  a->positioned == b->positioned);
}

bool same_position(const std::optional<EntityPosition>& a,
                   const std::optional<EntityPosition>& b) noexcept {
    if (a.has_value() != b.has_value()) return false;
    return !a || *a == *b;
}

std::optional<std::int16_t> object_flags(const Entity& entity) {
    const auto found = entity.object_words.find(flags_index);
    return found == entity.object_words.end() ? std::nullopt
                                               : std::optional<std::int16_t>{found->second};
}

ObstacleState capture(const Entity& entity) {
    const auto guid = entity.definition
        ? std::optional<std::uint32_t>{entity.definition->guid} : std::nullopt;
    return {entity.id, entity.kind, entity.generation, entity.position, entity.attachment,
            guid, object_flags(entity), entity.static_footprint};
}

bool matches_world(const World& world, const std::vector<ObstacleState>& source) {
    std::size_t index = 0;
    auto cursor = std::int16_t{0};
    while (const auto next = world.next_entity(cursor)) {
        cursor = *next;
        if (index >= source.size()) return false;
        const auto* entity = world.find(cursor);
        if (!entity) return false;
        const auto current = capture(*entity);
        const auto& before = source[index++];
        if (current.id != before.id || current.kind != before.kind ||
            current.generation != before.generation || !same_position(current.position, before.position) ||
            !same_attachment(current.attachment, before.attachment) ||
            current.guid != before.guid || current.flags != before.flags ||
            current.static_footprint != before.static_footprint)
            return false;
    }
    return index == source.size();
}

} // namespace

RoomInfoObstacleQuery query_world_room_info(
    const World& world, const DefinitionIndex& definitions,
    const RuntimeRoomResolver& room_for, const AvatarKillTimeoutResolver& kill_timeout_for,
    std::uint16_t room, const CollisionBox& candidate, std::int32_t target_id,
    std::size_t entity_limit) {
    static thread_local bool query_active = false;
    if (query_active)
        return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
    struct QueryGuard {
        bool& active;
        explicit QueryGuard(bool& value) : active(value) { active = true; }
        ~QueryGuard() { active = false; }
    } guard(query_active);

    if (entity_limit == 0 || entity_limit > 4096)
        return unsupported(RoomInfoObstacleIndexReason::inventory_over_budget);

    std::vector<ObstacleState> source;
    auto cursor = std::int16_t{0};
    while (const auto next = world.next_entity(cursor)) {
        cursor = *next;
        if (source.size() >= entity_limit)
            return unsupported(RoomInfoObstacleIndexReason::inventory_over_budget);
        const auto* entity = world.find(cursor);
        if (!entity)
            return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
        source.push_back(capture(*entity));
    }

    RoomInfoObstacleIndex index(entity_limit);
    std::vector<RoomInfoObstacle> obstacles;
    for (const auto& entity : source) {
        // World uses no position for out-of-world entities. Simitone does not
        // register an obstacle until an entity has a valid world position.
        if (!entity.position || entity.attachment) continue;

        if (!entity.flags)
            return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
        const auto flags = static_cast<std::uint16_t>(*entity.flags);
        const bool zero_extent = (flags & has_zero_extent) != 0;

        CollisionShapeResult shape{};
        if (entity.kind == EntityKind::avatar) {
            if (!kill_timeout_for)
                return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
            const auto kill_timeout = kill_timeout_for(entity.id, entity.generation);
            if (!kill_timeout)
                return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
            // VMAvatar.GetObstacle returns no persistent obstacle for a dying
            // avatar unless HasZeroExtent is set; otherwise it uses radius 3.
            if (*kill_timeout > -1 && !zero_extent) continue;
            shape = avatar_candidate_collision_shape(*entity.position);
        } else if (entity.kind == EntityKind::object) {
            if (zero_extent) continue;
            if (!entity.guid)
                return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
            const auto* definition = definitions.unique(*entity.guid);
            if (!definition)
                return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
            shape = object_collision_shape(definition->definition, *entity.position, flags, false);
        } else {
            return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
        }

        if (shape.state == CollisionShapeState::absent) continue;
        if (shape.state != CollisionShapeState::present || !room_for)
            return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);
        const auto owner_room = room_for(*entity.position);
        if (!owner_room)
            return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);

        obstacles.push_back({entity.id, *owner_room, shape.box,
            entity.static_footprint ? RoomInfoObstacleCollection::static_set
                                    : RoomInfoObstacleCollection::dynamic_list});
    }

    if (!matches_world(world, source))
        return unsupported(RoomInfoObstacleIndexReason::inventory_incomplete);

    if (!index.rebuild(obstacles, true))
        return unsupported(index.reason());
    return index.query(room, candidate, target_id);
}

} // namespace odh
