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
    value.room_id=1;value.level=0;
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
    require(result.room_id==1&&result.level==0,
            "runtime grid retains its source room and level identity");
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

    {
        auto large=snapshot();
        large.room_max_x=large.room_max_y=1024;
        large.obstacles={{127,0,129,1024}};
        auto full=odh::build_world_room_route_grid(large);
        require(full.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget&&!full.grid,
                "large room remains subject to the original allocation budget");
        const odh::WorldRoomRouteGridWindow window{{64,64},{192,192}};
        auto local=odh::build_world_room_route_grid(large,{},window);
        require(local.state==odh::WorldRoomRouteGridState::ready&&local.grid&&local.windowed&&
                local.room_id==large.room_id&&local.grid->cells()==128U*128U&&
                local.grid->contains({64,64})&&local.grid->contains({191,191})&&
                !local.grid->contains({192,191})&&!local.grid->walkable({128,100}),
                "bounded window clips allocation but retains full-snapshot obstacle interiors and identity");
        require(odh::find_route(*local.grid,{80,100},{{100,100}}).status==odh::RouteStatus::found&&
                odh::find_route(*local.grid,{100,100},{{110,100}}).status==odh::RouteStatus::found,
                "two consecutive local routes remain possible inside a large room window");
        large.obstacles.push_back({104,0,106,1024});
        local=odh::build_world_room_route_grid(large,{},window);
        require(local.state==odh::WorldRoomRouteGridState::ready&&
                odh::find_route(*local.grid,{100,100},{{110,100}}).status==odh::RouteStatus::no_path,
                "rebuilding the bounded window observes a mutation between local routes");
        local=odh::build_world_room_route_grid(large,{16384,100,100000},window);
        require(local.reason==odh::WorldRoomRouteGridReason::route_grid_over_budget&&!local.grid,
                "an explicit window cannot bypass the allocation budget");
        local=odh::build_world_room_route_grid(large,{16384,262144,1},window);
        require(local.reason==odh::WorldRoomRouteGridReason::raster_work_budget_exhausted&&!local.grid,
                "an explicit window cannot bypass the raster work budget");
        local=odh::build_world_room_route_grid(large,{},odh::WorldRoomRouteGridWindow{{2048,2048},{2050,2050}});
        require(local.reason==odh::WorldRoomRouteGridReason::room_bounds_invalid&&!local.grid,
                "a window disjoint from snapshot bounds fails closed");
    }

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
    input=snapshot();input.room_id=0;
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::snapshot_identity_invalid&&!result.grid,
            "ready snapshots without a room identity are rejected");
    input=snapshot();input.level=2;
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::snapshot_identity_invalid&&!result.grid,
            "snapshots with an unsupported level are rejected");
    input=snapshot();input.obstacles.push_back({10,10,10,12});
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::obstacle_invalid&&!result.grid,
            "malformed obstacle rectangles fail closed");
    input=snapshot();input.room_min_x=std::numeric_limits<std::int32_t>::min();
    input.room_max_x=std::numeric_limits<std::int32_t>::min()+16;
    result=odh::build_world_room_route_grid(input);
    require(result.reason==odh::WorldRoomRouteGridReason::room_bounds_invalid&&!result.grid,
            "bounds that overflow the signed route coordinate range are rejected");
    std::cout<<17<<" live room route-grid checks passed (snapshot fixture only)\n";
}
