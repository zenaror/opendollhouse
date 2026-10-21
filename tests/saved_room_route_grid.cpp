#include "odh/saved_room_route_grid.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
odh::GeometricRoomMap room_map() {
    odh::GeometricRoomMap map;
    map.width=5; map.height=5; map.floor_resources_resolved=true; map.rooms.resize(2);
    map.rooms[1]={1,0,25,{0,0,5,5},false,false,{}};
    map.levels[0].assign(25,0x00010001U);
    map.levels[1].assign(25,0x00010001U);
    return map;
}
odh::SavedCollisionCandidate candidate(std::int16_t id, odh::CollisionBox box,
                                       odh::EntityPosition pos,
                                       odh::SavedCollisionCandidateState state=odh::SavedCollisionCandidateState::candidate,
                                       odh::SavedCollisionCandidateReason reason=odh::SavedCollisionCandidateReason::none) {
    return {id,false,state,reason,box,pos};
}
}
int main() {
    try {
        auto map=room_map();
        const auto room_obs=odh::generate_room_obstacle_candidates(map,1,0,{},true);
        require(room_obs.complete,"complete RoomObs fixture");
        auto built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{},105,true);
        require(built.state==odh::SavedRoomRouteGridState::ready && built.grid,
                "complete room geometry builds a candidate grid");
        require(built.grid->contains({40,40}) && built.grid->walkable({40,40}),
                "room interior is walkable");
        require(!built.grid->contains({-1,40}),"grid is bounded to the room tile extents");

        const auto furniture=candidate(128,{36,36,44,44},{40,40,1,0});
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{furniture},105,true);
        require(built.state==odh::SavedRoomRouteGridState::ready && built.grid &&
                !built.grid->walkable({40,40}) && built.blocked_by_entities>0,
                "entity footprint expands by the routing margin and blocks its interior");
        const auto route=odh::find_route(*built.grid,{8,40},{{72,40}});
        require(route.status==odh::RouteStatus::found && route.points.front()==odh::RoutePoint{8,40} &&
                route.points.back()==odh::RoutePoint{72,40},
                "RouteGrid finds a bounded path around the conservative entity footprint");

        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{{38,38,42,42}},true,{},105,true);
        require(built.state==odh::SavedRoomRouteGridState::ready && built.grid &&
                !built.grid->walkable({40,40}) && built.blocked_by_wall_obstacles>0,
                "supplied WallObs geometry blocks walkable candidates");

        auto avatar=candidate(137,{37,37,43,43},{40,40,1,0},
            odh::SavedCollisionCandidateState::unsupported,
            odh::SavedCollisionCandidateReason::avatar_kill_timeout_unavailable);
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{avatar},105,true);
        require(built.state==odh::SavedRoomRouteGridState::ready && built.grid &&
                !built.grid->walkable({40,40}),
                "uncertain but known avatar geometry is conservatively blocked");

        auto outside=candidate(4,{0,0,0,0},{-16,-16,1,0},
            odh::SavedCollisionCandidateState::unsupported,
            odh::SavedCollisionCandidateReason::position_unavailable);
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{outside},105,true);
        require(built.state==odh::SavedRoomRouteGridState::ready && built.skipped_outside_entities==1,
                "saved out-of-world sentinel belongs to room zero, outside this room-local entity list");

        auto unknown=candidate(9,{0,0,0,0},{40,40,1,0},
            odh::SavedCollisionCandidateState::unsupported,
            odh::SavedCollisionCandidateReason::collision_metadata_missing);
        unknown.box.reset();
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{unknown},105,true);
        require(built.state==odh::SavedRoomRouteGridState::unsupported && !built.grid &&
                built.reason==odh::SavedRoomRouteGridReason::entity_geometry_unknown,
                "same-room entity with unresolved geometry fails closed");

        auto incomplete=room_obs; incomplete.complete=false;
        built=odh::build_saved_room_route_grid(map,1,0,incomplete,{},true,{},105,true);
        require(built.state==odh::SavedRoomRouteGridState::unsupported &&
                built.reason==odh::SavedRoomRouteGridReason::room_obstacles_incomplete,
                "incomplete RoomObs never produces a route grid");
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},false,{},105,true);
        require(built.reason==odh::SavedRoomRouteGridReason::wall_obstacles_incomplete,
                "incomplete WallObs never produces a route grid");
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{},105,false);
        require(built.reason==odh::SavedRoomRouteGridReason::incomplete_entity_inventory,
                "incomplete entity inventory never produces a route grid");
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{},true,{},105,true,{10,10});
        require(built.reason==odh::SavedRoomRouteGridReason::route_grid_over_budget,
                "room grid allocation obeys the caller's hard cell budget");
        built=odh::build_saved_room_route_grid(map,1,0,room_obs,{{20,20,30,30}},true,{},105,true,
                                                {262144,4096,16384,16});
        require(built.reason==odh::SavedRoomRouteGridReason::raster_work_budget_exhausted,
                "rectangle rasterization obeys the deterministic work budget");
        std::cout<<15<<" saved room route-grid checks passed (static candidates only)\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"Saved room route-grid test failed: "<<e.what()<<'\n';
        return 1;
    }
}
