#include "odh/saved_attempt_walk_obstacles.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace odh {
namespace {
constexpr std::uint16_t disallow_person_intersection = 1U << 1;
constexpr std::uint16_t allow_person_intersection = 1U << 4;
constexpr std::int64_t route_margin = 3;

AttemptWalkObstacleSnapshot unsupported(AttemptWalkObstacleReason reason) {
    AttemptWalkObstacleSnapshot result;
    result.reason = reason;
    return result;
}

bool valid_rect(const RoomObstacleRect& rect) {
    return rect.x1 < rect.x2 && rect.y1 < rect.y2;
}

bool append_rect(AttemptWalkObstacleSnapshot& result, const RoomObstacleRect& rect,
                 std::size_t limit) {
    if (!valid_rect(rect) || result.obstacles.size() >= limit) return false;
    result.obstacles.push_back(rect);
    return true;
}

bool expand(const CollisionBox& box, RoomObstacleRect& result) {
    if (box.x1 >= box.x2 || box.y1 >= box.y2) return false;
    const auto x1 = std::int64_t(box.x1) - route_margin;
    const auto y1 = std::int64_t(box.y1) - route_margin;
    const auto x2 = std::int64_t(box.x2) + route_margin;
    const auto y2 = std::int64_t(box.y2) + route_margin;
    if (x1 < std::numeric_limits<std::int32_t>::min() ||
        y1 < std::numeric_limits<std::int32_t>::min() ||
        x2 > std::numeric_limits<std::int32_t>::max() ||
        y2 > std::numeric_limits<std::int32_t>::max()) return false;
    result = {static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
              static_cast<std::int32_t>(x2), static_cast<std::int32_t>(y2)};
    return true;
}

bool point_inside(const RoomObstacleRect& rect, std::int32_t x, std::int32_t y) {
    // VMObstacle.Intersects(point-rectangle) is strict at all four edges.
    return x > rect.x1 && x < rect.x2 && y > rect.y1 && y < rect.y2;
}
} // namespace

AttemptWalkObstacleSnapshot build_attempt_walk_obstacles(
    const GeometricRoomMap& map, std::uint16_t room, std::uint8_t level,
    const EntityPosition& actor_position, std::int16_t actor_id,
    std::uint16_t actor_flags, std::optional<std::uint16_t> actor_entry_point5_action,
    const RoomObstacleCandidateSet& room_obstacles,
    const WallObstacleCandidateSet& wall_obstacles,
    const std::vector<AttemptWalkObstacleEntity>& entities,
    bool entity_inventory_complete, bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const AttemptWalkEntryPoint5Resolver& resolve_entry_point5,
    const AttemptWalkObstacleLimits& limits) {
    if (actor_id <= 0 || !room || level > 1 || actor_position.level != level + 1)
        return unsupported(AttemptWalkObstacleReason::invalid_actor);
    const auto actor_room = geometric_room_at(map, actor_position.x, actor_position.y,
                                               actor_position.level);
    if (!actor_room || *actor_room != room)
        return unsupported(AttemptWalkObstacleReason::actor_room_mismatch);
    if (!entity_inventory_complete)
        return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
    if (!limits.entities || limits.entities > 4096 || entities.size() > limits.entities)
        return unsupported(AttemptWalkObstacleReason::entity_inventory_over_budget);
    if (!limits.rectangles || limits.rectangles > 16384 ||
        !limits.entry_point5_calls || limits.entry_point5_calls > 4096)
        return unsupported(AttemptWalkObstacleReason::obstacle_inventory_over_budget);
    if (!room_obstacles.complete)
        return unsupported(AttemptWalkObstacleReason::room_obstacles_incomplete);
    if (!wall_obstacles.complete)
        return unsupported(AttemptWalkObstacleReason::wall_obstacles_incomplete);
    if (room_obstacles.room != room || room_obstacles.level != level ||
        wall_obstacles.level != level)
        return unsupported(AttemptWalkObstacleReason::obstacle_set_mismatch);

    const auto room_record = std::find_if(map.rooms.begin(), map.rooms.end(),
        [room](const GeometricRoom& candidate) { return candidate.id == room; });
    if (room_record == map.rooms.end() || room_record->level != level)
        return unsupported(AttemptWalkObstacleReason::actor_room_mismatch);
    const auto& bounds = room_record->bounds;
    if (!bounds.width || !bounds.height || bounds.x >= map.width || bounds.y >= map.height ||
        bounds.width > map.width - bounds.x || bounds.height > map.height - bounds.y)
        return unsupported(AttemptWalkObstacleReason::actor_room_mismatch);

    AttemptWalkObstacleSnapshot result;
    // Simitone room bounds are zero-based inclusive cell bounds. AttemptWalk
    // pads those coordinates by one tile on each side before multiplying by 16.
    const auto left = (std::int64_t(bounds.x) - 1) * 16;
    const auto top = (std::int64_t(bounds.y) - 1) * 16;
    const auto right = left + (std::int64_t(bounds.width) + 2) * 16;
    const auto bottom = top + (std::int64_t(bounds.height) + 2) * 16;
    const auto band_left = left - 16;
    const auto band_top = top - 16;
    const auto band_right = right + 16;
    const auto band_bottom = bottom + 16;
    const auto in_range = [](std::int64_t v) {
        return v >= std::numeric_limits<std::int32_t>::min() &&
               v <= std::numeric_limits<std::int32_t>::max();
    };
    if (!in_range(band_left) || !in_range(band_top) || !in_range(band_right) ||
        !in_range(band_bottom))
        return unsupported(AttemptWalkObstacleReason::coordinate_overflow);
    result.room_min_x = static_cast<std::int32_t>(left);
    result.room_min_y = static_cast<std::int32_t>(top);
    result.room_max_x = static_cast<std::int32_t>(right);
    result.room_max_y = static_cast<std::int32_t>(bottom);

    // VMRoom.RoutingObstacles is wallObs.Concat(roomObs); AttemptWalk then
    // appends the same four one-tile bands in this order. The concatenation
    // order is retained as evidence, though route semantics use the set.
    for (const auto& rect : wall_obstacles.rectangles)
        if (!append_rect(result, rect, limits.rectangles))
            return unsupported(AttemptWalkObstacleReason::invalid_rectangle);
    for (const auto& rect : room_obstacles.rectangles)
        if (!append_rect(result, rect, limits.rectangles))
            return unsupported(AttemptWalkObstacleReason::invalid_rectangle);
    const RoomObstacleRect bands[] = {
        {static_cast<std::int32_t>(band_left), static_cast<std::int32_t>(band_top),
         static_cast<std::int32_t>(band_right), static_cast<std::int32_t>(top)},
        {static_cast<std::int32_t>(band_left), static_cast<std::int32_t>(bottom),
         static_cast<std::int32_t>(band_right), static_cast<std::int32_t>(band_bottom)},
        {static_cast<std::int32_t>(band_left), static_cast<std::int32_t>(band_top),
         static_cast<std::int32_t>(left), static_cast<std::int32_t>(band_bottom)},
        {static_cast<std::int32_t>(right), static_cast<std::int32_t>(band_top),
         static_cast<std::int32_t>(band_right), static_cast<std::int32_t>(band_bottom)}
    };
    for (const auto& band : bands)
        if (!append_rect(result, band, limits.rectangles))
            return unsupported(AttemptWalkObstacleReason::obstacle_inventory_over_budget);

    std::set<std::int16_t> ids;
    std::set<std::int16_t> considered_avatars;
    for (const auto id : avatars_to_consider) {
        if (id <= 0 || !considered_avatars.insert(id).second)
            return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
    }
    const bool consider_avatars = (actor_flags & allow_person_intersection) == 0;
    const auto evaluate_ep5 = [&](std::int16_t caller, std::int16_t stack_object,
                                  const std::optional<std::uint16_t>& action)
        -> std::optional<bool> {
        if (!action) return std::nullopt;
        if (*action <= 255) return false;
        if (!resolve_entry_point5) return std::nullopt;
        if (result.entry_point5_calls >= limits.entry_point5_calls) return std::nullopt;
        ++result.entry_point5_calls;
        return resolve_entry_point5(caller, stack_object);
    };
    for (const auto& entity : entities) {
        if (entity.id <= 0 || !ids.insert(entity.id).second)
            return unsupported(AttemptWalkObstacleReason::entity_inventory_incomplete);
        if (entity.id == actor_id) continue;
        if (!entity.room) {
            auto failure = unsupported(AttemptWalkObstacleReason::entity_room_unknown);
            failure.unsupported_entity_id = entity.id;
            return failure;
        }
        if (*entity.room != room) continue;
        const bool considered = !entity.avatar ||
            (consider_avatars && considered_avatars.count(entity.id) != 0);
        if (!considered) {
            if (entity.avatar && consider_avatars && !avatar_consideration_complete)
                return unsupported(AttemptWalkObstacleReason::avatar_consideration_unknown);
            ++result.skipped_nonblocking_entities;
            continue;
        }
        if (entity.footprint_state == AttemptWalkFootprintState::absent) {
            ++result.skipped_nonblocking_entities;
            continue;
        }
        if (entity.footprint_state != AttemptWalkFootprintState::present || !entity.footprint) {
            auto failure = unsupported(AttemptWalkObstacleReason::entity_footprint_unknown);
            failure.unsupported_entity_id = entity.id;
            return failure;
        }
        if (!entity.flags) {
            auto failure = unsupported(AttemptWalkObstacleReason::entity_flags_unknown);
            failure.unsupported_entity_id = entity.id;
            return failure;
        }
        const bool flags_block =
            ((*entity.flags & disallow_person_intersection) != 0) ||
            ((*entity.flags & allow_person_intersection) == 0);
        if (!flags_block) {
            ++result.skipped_nonblocking_entities;
            continue;
        }
        if (!actor_entry_point5_action)
            return unsupported(AttemptWalkObstacleReason::entry_point5_unknown);
        const auto actor_calls_before = result.entry_point5_calls;
        const auto actor_exempts = evaluate_ep5(actor_id, entity.id, actor_entry_point5_action);
        if (!actor_exempts)
            return unsupported(result.entry_point5_calls == limits.entry_point5_calls &&
                               actor_calls_before == result.entry_point5_calls
                ? AttemptWalkObstacleReason::entry_point5_budget_exhausted
                : AttemptWalkObstacleReason::entry_point5_result_unknown);
        bool exempt = *actor_exempts;
        if (!exempt) {
            if (!entity.entry_point5_action) {
                auto failure = unsupported(AttemptWalkObstacleReason::entry_point5_unknown);
                failure.unsupported_entity_id = entity.id;
                return failure;
            }
            const auto blocker_calls_before = result.entry_point5_calls;
            const auto blocker_exempts = evaluate_ep5(entity.id, actor_id,
                                                       entity.entry_point5_action);
            if (!blocker_exempts)
                return unsupported(result.entry_point5_calls == limits.entry_point5_calls &&
                                   blocker_calls_before == result.entry_point5_calls &&
                                   *entity.entry_point5_action > 255
                    ? AttemptWalkObstacleReason::entry_point5_budget_exhausted
                    : AttemptWalkObstacleReason::entry_point5_result_unknown);
            exempt = *blocker_exempts;
        }
        if (exempt) {
            ++result.exempted_by_entry_point5;
            continue;
        }
        RoomObstacleRect expanded{};
        if (!expand(*entity.footprint, expanded))
            return unsupported(AttemptWalkObstacleReason::coordinate_overflow);
        if (result.obstacles.size() >= limits.rectangles)
            return unsupported(AttemptWalkObstacleReason::obstacle_inventory_over_budget);
        result.obstacles.push_back(expanded);
        result.entity_obstacle_ids.push_back(entity.id);
    }
    for (const auto& obstacle : result.obstacles) {
        if (point_inside(obstacle, actor_position.x, actor_position.y)) {
            result.start_intersects = true;
            break;
        }
    }
    result.state = AttemptWalkObstacleState::ready;
    result.reason = AttemptWalkObstacleReason::none;
    return result;
}

const char* attempt_walk_obstacle_reason_name(AttemptWalkObstacleReason reason) noexcept {
    switch (reason) {
    case AttemptWalkObstacleReason::none: return "none";
    case AttemptWalkObstacleReason::invalid_actor: return "invalid_actor";
    case AttemptWalkObstacleReason::actor_room_mismatch: return "actor_room_mismatch";
    case AttemptWalkObstacleReason::room_obstacles_incomplete: return "room_obstacles_incomplete";
    case AttemptWalkObstacleReason::wall_obstacles_incomplete: return "wall_obstacles_incomplete";
    case AttemptWalkObstacleReason::obstacle_set_mismatch: return "obstacle_set_mismatch";
    case AttemptWalkObstacleReason::entity_inventory_incomplete: return "entity_inventory_incomplete";
    case AttemptWalkObstacleReason::entity_inventory_over_budget: return "entity_inventory_over_budget";
    case AttemptWalkObstacleReason::obstacle_inventory_over_budget: return "obstacle_inventory_over_budget";
    case AttemptWalkObstacleReason::avatar_consideration_unknown: return "avatar_consideration_unknown";
    case AttemptWalkObstacleReason::entity_room_unknown: return "entity_room_unknown";
    case AttemptWalkObstacleReason::entity_footprint_unknown: return "entity_footprint_unknown";
    case AttemptWalkObstacleReason::entity_flags_unknown: return "entity_flags_unknown";
    case AttemptWalkObstacleReason::entry_point5_unknown: return "entry_point5_unknown";
    case AttemptWalkObstacleReason::entry_point5_result_unknown: return "entry_point5_result_unknown";
    case AttemptWalkObstacleReason::entry_point5_budget_exhausted: return "entry_point5_budget_exhausted";
    case AttemptWalkObstacleReason::invalid_rectangle: return "invalid_rectangle";
    case AttemptWalkObstacleReason::coordinate_overflow: return "coordinate_overflow";
    }
    return "invalid_reason";
}

} // namespace odh
