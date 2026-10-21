#include "odh/saved_room_route_grid.hpp"

#include <algorithm>
#include <limits>

namespace odh {
namespace {
constexpr std::int64_t units_per_tile = 16;
constexpr std::int64_t route_margin = 3;

SavedRoomRouteGrid unsupported(SavedRoomRouteGridReason reason) {
    SavedRoomRouteGrid result;
    result.reason = reason;
    return result;
}

bool expand_route_obstacle(const CollisionBox& box, CollisionBox& expanded) {
    const auto x1 = std::int64_t(box.x1) - route_margin;
    const auto y1 = std::int64_t(box.y1) - route_margin;
    const auto x2 = std::int64_t(box.x2) + route_margin;
    const auto y2 = std::int64_t(box.y2) + route_margin;
    if (x1 < std::numeric_limits<std::int32_t>::min() ||
        y1 < std::numeric_limits<std::int32_t>::min() ||
        x2 > std::numeric_limits<std::int32_t>::max() ||
        y2 > std::numeric_limits<std::int32_t>::max()) return false;
    expanded = {static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
                static_cast<std::int32_t>(x2), static_cast<std::int32_t>(y2)};
    return true;
}
}

SavedRoomRouteGrid build_saved_room_route_grid(
    const GeometricRoomMap& map, std::uint16_t room, std::uint8_t level,
    const RoomObstacleCandidateSet& room_obstacles,
    const std::vector<RoomObstacleRect>& wall_obstacles,
    bool wall_obstacles_complete,
    const std::vector<SavedCollisionCandidate>& entities,
    std::int16_t actor_id, bool entity_inventory_complete,
    const SavedRoomRouteGridLimits& limits) {
    if (!entity_inventory_complete)
        return unsupported(SavedRoomRouteGridReason::incomplete_entity_inventory);
    if (entities.size() > limits.entities)
        return unsupported(SavedRoomRouteGridReason::entity_inventory_over_budget);
    if (!map.width || !map.height || map.width > 64 || map.height > 64 ||
        level >= map.levels.size() || room == 0 || room >= map.rooms.size() ||
        map.rooms[room].id != room || map.rooms[room].level != level)
        return unsupported(SavedRoomRouteGridReason::room_identity_invalid);
    if (room_obstacles.room != room || room_obstacles.level != level)
        return unsupported(SavedRoomRouteGridReason::room_obstacles_mismatch);
    if (!room_obstacles.complete)
        return unsupported(SavedRoomRouteGridReason::room_obstacles_incomplete);
    if (!wall_obstacles_complete)
        return unsupported(SavedRoomRouteGridReason::wall_obstacles_incomplete);
    if (room_obstacles.rectangles.size() > limits.obstacle_rectangles ||
        wall_obstacles.size() > limits.obstacle_rectangles - room_obstacles.rectangles.size() ||
        entities.size() > limits.obstacle_rectangles - room_obstacles.rectangles.size() - wall_obstacles.size())
        return unsupported(SavedRoomRouteGridReason::obstacle_inventory_over_budget);

    const auto& bounds = map.rooms[room].bounds;
    if (!bounds.width || !bounds.height || bounds.x >= map.width || bounds.y >= map.height ||
        bounds.width > map.width - bounds.x || bounds.height > map.height - bounds.y)
        return unsupported(SavedRoomRouteGridReason::room_bounds_invalid);

    const auto origin_x = std::int64_t(bounds.x) * units_per_tile;
    const auto origin_y = std::int64_t(bounds.y) * units_per_tile;
    const auto width = std::uint64_t(bounds.width) * units_per_tile;
    const auto height = std::uint64_t(bounds.height) * units_per_tile;
    if (origin_x > std::numeric_limits<std::int32_t>::max() ||
        origin_y > std::numeric_limits<std::int32_t>::max() ||
        origin_x + static_cast<std::int64_t>(width) - 1 > std::numeric_limits<std::int32_t>::max() ||
        origin_y + static_cast<std::int64_t>(height) - 1 > std::numeric_limits<std::int32_t>::max())
        return unsupported(SavedRoomRouteGridReason::coordinate_overflow);
    if (!limits.cells || width > limits.cells || height > limits.cells / width)
        return unsupported(SavedRoomRouteGridReason::route_grid_over_budget);

    const auto valid_rect = [](const RoomObstacleRect& rect) {
        return rect.x1 < rect.x2 && rect.y1 < rect.y2;
    };
    for (const auto& rect : room_obstacles.rectangles)
        if (!valid_rect(rect)) return unsupported(SavedRoomRouteGridReason::room_obstacles_incomplete);
    for (const auto& rect : wall_obstacles)
        if (!valid_rect(rect)) return unsupported(SavedRoomRouteGridReason::wall_obstacles_incomplete);

    struct EntityObstacle { CollisionBox box; };
    std::vector<EntityObstacle> obstacles;
    obstacles.reserve(entities.size());
    std::size_t skipped_outside = 0;
    for (const auto& entity : entities) {
        if (entity.object_id == actor_id || entity.state == SavedCollisionCandidateState::absent)
            continue;

        // The saved sentinel maps to room 0 in Simitone's GetRoomAt. The
        // route frame only enumerates RoomInfo.Entities for its nonzero room.
        if (entity.reason == SavedCollisionCandidateReason::position_unavailable) {
            ++skipped_outside;
            continue;
        }
        if (!entity.position)
            return unsupported(SavedRoomRouteGridReason::entity_room_unknown);
        const auto entity_room = geometric_room_at(map, entity.position->x,
                                                   entity.position->y,
                                                   entity.position->level);
        if (!entity_room)
            return unsupported(SavedRoomRouteGridReason::entity_room_unknown);
        if (*entity_room != room) continue;
        if (!entity.box || entity.box->x1 >= entity.box->x2 || entity.box->y1 >= entity.box->y2)
            return unsupported(SavedRoomRouteGridReason::entity_geometry_unknown);
        CollisionBox expanded{};
        if (!expand_route_obstacle(*entity.box, expanded))
            return unsupported(SavedRoomRouteGridReason::coordinate_overflow);
        obstacles.push_back({expanded});
    }

    SavedRoomRouteGrid result;
    result.grid.emplace(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                        limits.cells, RoutePoint{static_cast<std::int32_t>(origin_x),
                                                 static_cast<std::int32_t>(origin_y)});
    result.skipped_outside_entities = skipped_outside;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const RoutePoint point{static_cast<std::int32_t>(origin_x + x),
                                   static_cast<std::int32_t>(origin_y + y)};
            const auto mapped_room = geometric_room_at(map, point.x, point.y,
                                                       static_cast<std::int16_t>(level + 1));
            if (!mapped_room)
                return unsupported(SavedRoomRouteGridReason::room_identity_invalid);
            if (*mapped_room != room) {
                result.grid->block(point);
                ++result.blocked_by_room;
                continue;
            }
        }
    }

    const auto rasterize = [&](const RoomObstacleRect& rect, std::size_t& count) {
        const auto x1 = std::max<std::int64_t>(origin_x, std::int64_t(rect.x1) + 1);
        const auto y1 = std::max<std::int64_t>(origin_y, std::int64_t(rect.y1) + 1);
        const auto x2 = std::min<std::int64_t>(origin_x + static_cast<std::int64_t>(width), rect.x2);
        const auto y2 = std::min<std::int64_t>(origin_y + static_cast<std::int64_t>(height), rect.y2);
        if (x1 >= x2 || y1 >= y2) return true;
        const auto area = static_cast<std::uint64_t>(x2 - x1) * static_cast<std::uint64_t>(y2 - y1);
        if (area > limits.rasterized_cells - result.rasterized_cells) return false;
        result.rasterized_cells += static_cast<std::size_t>(area);
        for (auto y = y1; y < y2; ++y) {
            for (auto x = x1; x < x2; ++x) {
                const RoutePoint point{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
                if (result.grid->walkable(point)) {
                    result.grid->block(point);
                    ++count;
                }
            }
        }
        return true;
    };
    for (const auto& obstacle : room_obstacles.rectangles)
        if (!rasterize(obstacle, result.blocked_by_room_obstacles))
            return unsupported(SavedRoomRouteGridReason::raster_work_budget_exhausted);
    for (const auto& obstacle : wall_obstacles)
        if (!rasterize(obstacle, result.blocked_by_wall_obstacles))
            return unsupported(SavedRoomRouteGridReason::raster_work_budget_exhausted);
    for (const auto& obstacle : obstacles) {
        const RoomObstacleRect rect{obstacle.box.x1, obstacle.box.y1,
                                    obstacle.box.x2, obstacle.box.y2};
        if (!rasterize(rect, result.blocked_by_entities))
            return unsupported(SavedRoomRouteGridReason::raster_work_budget_exhausted);
    }
    result.state = SavedRoomRouteGridState::ready;
    result.reason = SavedRoomRouteGridReason::none;
    return result;
}

const char* saved_room_route_grid_reason_name(SavedRoomRouteGridReason reason) noexcept {
    switch (reason) {
    case SavedRoomRouteGridReason::none: return "none";
    case SavedRoomRouteGridReason::incomplete_entity_inventory: return "incomplete_entity_inventory";
    case SavedRoomRouteGridReason::room_identity_invalid: return "room_identity_invalid";
    case SavedRoomRouteGridReason::room_bounds_invalid: return "room_bounds_invalid";
    case SavedRoomRouteGridReason::room_obstacles_mismatch: return "room_obstacles_mismatch";
    case SavedRoomRouteGridReason::room_obstacles_incomplete: return "room_obstacles_incomplete";
    case SavedRoomRouteGridReason::wall_obstacles_incomplete: return "wall_obstacles_incomplete";
    case SavedRoomRouteGridReason::entity_inventory_over_budget: return "entity_inventory_over_budget";
    case SavedRoomRouteGridReason::obstacle_inventory_over_budget: return "obstacle_inventory_over_budget";
    case SavedRoomRouteGridReason::route_grid_over_budget: return "route_grid_over_budget";
    case SavedRoomRouteGridReason::raster_work_budget_exhausted: return "raster_work_budget_exhausted";
    case SavedRoomRouteGridReason::entity_room_unknown: return "entity_room_unknown";
    case SavedRoomRouteGridReason::entity_geometry_unknown: return "entity_geometry_unknown";
    case SavedRoomRouteGridReason::coordinate_overflow: return "coordinate_overflow";
    }
    return "invalid_reason";
}

} // namespace odh
