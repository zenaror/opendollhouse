#include "odh/world_room_route_grid.hpp"

#include <algorithm>
#include <limits>

namespace odh {
namespace {
WorldRoomRouteGrid unsupported(WorldRoomRouteGridReason reason,
                               std::optional<AttemptWalkObstacleReason> source_reason = {}) {
    WorldRoomRouteGrid result;
    result.reason=reason;
    result.source_obstacle_reason=source_reason;
    return result;
}
}

WorldRoomRouteGrid build_world_room_route_grid(
    const AttemptWalkObstacleSnapshot& snapshot,const WorldRoomRouteGridLimits& limits,
    std::optional<WorldRoomRouteGridWindow> window) {
    if(snapshot.state!=AttemptWalkObstacleState::ready)
        return unsupported(WorldRoomRouteGridReason::obstacle_snapshot_incomplete,snapshot.reason);
    if(!snapshot.room_id || snapshot.level>1)
        return unsupported(WorldRoomRouteGridReason::snapshot_identity_invalid);
    if(snapshot.obstacles.size()>limits.rectangles)
        return unsupported(WorldRoomRouteGridReason::route_grid_over_budget);
    const auto min_x=static_cast<std::int64_t>(snapshot.room_min_x);
    const auto min_y=static_cast<std::int64_t>(snapshot.room_min_y);
    const auto max_x=static_cast<std::int64_t>(snapshot.room_max_x);
    const auto max_y=static_cast<std::int64_t>(snapshot.room_max_y);
    if(min_x>=max_x||min_y>=max_y||min_x%16!=0||min_y%16!=0||max_x%16!=0||max_y%16!=0)
        return unsupported(WorldRoomRouteGridReason::room_bounds_invalid);

    // AttemptWalk's snapshot bounds include the room's one-tile padded bounds;
    // its four outer fence bands extend one more tile on every side.
    auto origin_x=min_x-16,origin_y=min_y-16;
    auto end_x=max_x+16,end_y=max_y+16;
    if(window) {
        if(window->min.x>=window->max.x||window->min.y>=window->max.y)
            return unsupported(WorldRoomRouteGridReason::room_bounds_invalid);
        origin_x=std::max(origin_x,static_cast<std::int64_t>(window->min.x));
        origin_y=std::max(origin_y,static_cast<std::int64_t>(window->min.y));
        end_x=std::min(end_x,static_cast<std::int64_t>(window->max.x));
        end_y=std::min(end_y,static_cast<std::int64_t>(window->max.y));
    }
    const auto width=end_x-origin_x,height=end_y-origin_y;
    constexpr std::int64_t route_dimension_limit=32767;
    if(origin_x<std::numeric_limits<std::int32_t>::min()||
       origin_y<std::numeric_limits<std::int32_t>::min()||
       end_x-1>std::numeric_limits<std::int32_t>::max()||
       end_y-1>std::numeric_limits<std::int32_t>::max()||
       width<=0||height<=0||width>route_dimension_limit||height>route_dimension_limit)
        return unsupported(WorldRoomRouteGridReason::room_bounds_invalid);
    const auto cell_count=static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height);
    if(cell_count>limits.cells)
        return unsupported(WorldRoomRouteGridReason::route_grid_over_budget);

    WorldRoomRouteGrid result;
    result.windowed=window.has_value();
    result.room_id=snapshot.room_id;
    result.level=snapshot.level;
    result.obstacle_rectangles=snapshot.obstacles.size();
    result.grid.emplace(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),
                        limits.cells,RoutePoint{static_cast<std::int32_t>(origin_x),
                                                static_cast<std::int32_t>(origin_y)});
    for(const auto& rect:snapshot.obstacles) {
        if(rect.x1>=rect.x2||rect.y1>=rect.y2)
            return unsupported(WorldRoomRouteGridReason::obstacle_invalid);
        const auto x1=std::max(origin_x,static_cast<std::int64_t>(rect.x1)+1);
        const auto y1=std::max(origin_y,static_cast<std::int64_t>(rect.y1)+1);
        const auto x2=std::min(end_x,static_cast<std::int64_t>(rect.x2));
        const auto y2=std::min(end_y,static_cast<std::int64_t>(rect.y2));
        if(x1>=x2||y1>=y2) continue;
        const auto area=static_cast<std::uint64_t>(x2-x1)*static_cast<std::uint64_t>(y2-y1);
        if(area>limits.rasterized_cells-result.rasterized_cells)
            return unsupported(WorldRoomRouteGridReason::raster_work_budget_exhausted);
        result.rasterized_cells+=static_cast<std::size_t>(area);
        for(auto y=y1;y<y2;++y)for(auto x=x1;x<x2;++x) {
            const RoutePoint point{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)};
            if(result.grid->walkable(point)) {
                result.grid->block(point);
                ++result.newly_blocked_cells;
            }
        }
    }
    result.state=WorldRoomRouteGridState::ready;
    result.reason=WorldRoomRouteGridReason::none;
    return result;
}

const char* world_room_route_grid_reason_name(WorldRoomRouteGridReason reason) noexcept {
    switch(reason) {
    case WorldRoomRouteGridReason::none:return "none";
    case WorldRoomRouteGridReason::obstacle_snapshot_incomplete:return "obstacle_snapshot_incomplete";
    case WorldRoomRouteGridReason::snapshot_identity_invalid:return "snapshot_identity_invalid";
    case WorldRoomRouteGridReason::room_bounds_invalid:return "room_bounds_invalid";
    case WorldRoomRouteGridReason::route_grid_over_budget:return "route_grid_over_budget";
    case WorldRoomRouteGridReason::obstacle_invalid:return "obstacle_invalid";
    case WorldRoomRouteGridReason::raster_work_budget_exhausted:return "raster_work_budget_exhausted";
    }
    return "unknown";
}

} // namespace odh
