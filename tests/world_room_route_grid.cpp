#include "odh/world_room_route_grid.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* message) {
    if(!value) { std::cerr<<message<<'\n';std::exit(1); }
}
odh::AttemptWalkObstacleSnapshot snapshot() {
    odh::AttemptWalkObstacleSnapshot value;
    value.state=odh::AttemptWalkObstacleState::ready;
    value.reason=odh::AttemptWalkObstacleReason::none;
    value.room_min_x=0;value.room_min_y=0;value.room_max_x=64;value.room_max_y=64;
    value.obstacles={{-16,-16,80,0},{-16,64,80,80},{-16,-16,0,80},{64,-16,80,80},
                     {31,0,33,64}};
    return value;
}
}

int main() {
    auto input=snapshot();
    auto result=odh::build_world_room_route_grid(input);
    require(result.state==odh::WorldRoomRouteGridState::ready&&result.grid,
            "complete AttemptWalk snapshot produces a runtime route grid");
    require(result.grid->contains({-16,-16})&&result.grid->contains({79,79})&&
            !result.grid->contains({80,79})&&result.grid->cells()==96U*96U,
            "grid bounds include one-tile outer bands around the padded room");
    require(result.grid->walkable({0,0})&&result.grid->walkable({31,1})&&
            !result.grid->walkable({32,1})&&result.grid->walkable({33,1}),
            "rectangles block strict interiors using the AttemptWalk integer-cell convention");
    require(!result.grid->walkable({-15,4})&&!result.grid->walkable({65,4})&&
            !result.grid->walkable({4,-15})&&!result.grid->walkable({4,65}),
            "all four room fence bands are represented in the route grid");
    require(result.obstacle_rectangles==5&&result.rasterized_cells>0&&result.newly_blocked_cells>0,
            "grid reports bounded raster work and unique blocked cells");

    input.state=odh::AttemptWalkObstacleState::unsupported;
    input.reason=odh::AttemptWalkObstacleReason::entry_point5_result_unknown;
    result=odh::build_world_room_route_grid(input);
    require(result.state==odh::WorldRoomRouteGridState::unsupported&&!result.grid&&
            result.reason==odh::WorldRoomRouteGridReason::obstacle_snapshot_incomplete&&
            result.source_obstacle_reason==input.reason,
            "unsupported live obstacle snapshot preserves its reason and never yields a partial grid");
    input=snapshot();
    result=odh::build_world_room_route_grid(input,{16384,100,1000});
    require(result.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget&&!result.grid,
            "route grid allocation respects cell budget");
    result=odh::build_world_room_route_grid(input,{16384,96U*96U,1});
    require(result.reason==odh::WorldRoomRouteGridReason::raster_work_budget_exhausted&&!result.grid,
            "rectangle rasterization respects deterministic work budget");
    result=odh::build_world_room_route_grid(input,{4,96U*96U,100000});
    require(result.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget&&!result.grid,
            "obstacle inventory respects rectangle budget");

    input=snapshot();input.room_max_x=65;
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::room_bounds_invalid&&!result.grid,
            "misaligned room bounds are rejected instead of rounded");
    input=snapshot();input.obstacles.push_back({10,10,10,12});
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::obstacle_invalid&&!result.grid,
            "malformed obstacle rectangles fail closed");
    input=snapshot();input.room_min_x=std::numeric_limits<std::int32_t>::min();
    input.room_max_x=std::numeric_limits<std::int32_t>::min()+16;
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::room_bounds_invalid&&!result.grid,
            "bounds that overflow the signed route coordinate range are rejected");
    std::cout<<10<<" live room route-grid checks passed (snapshot fixture only)\n";
}
