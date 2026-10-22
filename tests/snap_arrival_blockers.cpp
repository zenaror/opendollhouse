#include "odh/saved_attempt_walk_obstacles.hpp"
#include "odh/snap.hpp"
#include "odh/world_room_route_grid.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

odh::GeometricRoomMap room_map() {
    odh::GeometricRoomMap map;
    map.width = 5;
    map.height = 5;
    for (auto& level : map.levels)
        level.assign(25, 1U | (1U << 16));
    map.rooms.resize(2);
    map.rooms[1] = {1, 1, 9, {1, 1, 3, 3}, false, false, {}};
    return map;
}

odh::SlotTable stair_slots() {
    odh::SlotRecord route{};
    route.type = 3;
    route.snap_target_slot = -1;
    route.flags = 2048;
    route.proximity = std::array<std::int32_t, 3>{16, 16, 16};
    route.max_size = 0;
    route.unknown_i10 = 0;
    route.gradient_bits = 0x3E400000;
    route.height = 0;
    route.facing = -2;
    route.resolution = 16;
    return {0, 10, {route}, 0};
}

odh::RoutineSet stair_routine() {
    return {{odh::routine_key(4113), odh::Bhav{0x8002,
        {{46, 254, 255, {0, 0, 3, 0, 2, 0, 0, 0}}}, {},
        odh::Bhav::Metadata{0, 4, 0, 0}}}};
}

struct Result {
    odh::VmStatus status;
    odh::EntityPosition actor_position;
    std::size_t placement_checks;
};

Result run(bool furniture_at_arrival) {
    auto map = room_map();
    std::vector<odh::AttemptWalkObstacleEntity> entities;
    if (furniture_at_arrival) {
        entities.push_back({20, false, 1, odh::AttemptWalkFootprintState::present,
            odh::CollisionBox{46, 46, 50, 50}, 0, 0});
    }
    const auto snapshot = odh::build_attempt_walk_obstacles(
        map, 1, 1, {32, 32, 2, 0}, 1, 0, 0,
        {1, 1, true, {}}, {1, true, {}}, entities, true, true, {});
    require(snapshot.state == odh::AttemptWalkObstacleState::ready,
            "complete arrival snapshot should be usable");
    const auto grid = odh::build_world_room_route_grid(snapshot);
    require(grid.state == odh::WorldRoomRouteGridState::ready && grid.grid,
            "arrival snapshot should produce a route grid");
    require(grid.room_id == 1 && grid.level == 1,
            "route grid should retain the upstairs room identity");
    if (furniture_at_arrival) {
        require(snapshot.entity_obstacle_ids == std::vector<std::int16_t>{20},
                "the furniture should be included among room blockers");
        require(!grid.grid->walkable({48, 48}),
                "furniture footprint should cover the stair arrival point");
    } else {
        require(snapshot.entity_obstacle_ids.empty() && grid.grid->walkable({48, 48}),
                "the control arrival point should remain clear");
    }

    odh::World world;
    world.create(1, odh::EntityKind::avatar, {0, 88, 1});
    world.create(2, odh::EntityKind::object, {0, 0, 4});
    world.set_position(1, {32, 32, 1, 6});
    world.set_position(2, {48, 48, 2, 0});
    std::size_t checks = 0;
    odh::ObjdBinding binding{138, 0x79E2B825, 129, 128};
    binding.master_id = 4;
    auto slots = stair_slots();
    odh::Snap snap{world, 2, binding, slots, 128, {},
        [&](std::int16_t actor, std::int16_t target, const odh::EntityPosition& point) {
            require(actor == 1 && target == 2,
                    "arrival validation should use the live Sim and stair identities");
            ++checks;
            return grid.grid->walkable({point.x, point.y})
                ? odh::Snap::StairPlacementStatus::accepted
                : odh::Snap::StairPlacementStatus::blocked;
        }};
    odh::Vm vm(stair_routine(), world.vm_memory(), {}, snap.primitive());
    snap.bind(vm);
    vm.start(odh::routine_key(4113), {1, 2, 2});
    const auto status = vm.run(1);
    return {status, *world.find(1)->position, checks};
}

void test_avatar_blocker_identity() {
    const auto map = room_map();
    // Synthetic avatar 30 stands on the exact square where the stair would
    // place the arriving Sim. This tests the obstacle snapshot only; it does
    // not claim that the live host has a second active Sim scheduler.
    const std::vector<odh::AttemptWalkObstacleEntity> entities{{
        30, true, 1, odh::AttemptWalkFootprintState::present,
        odh::CollisionBox{46, 46, 50, 50}, 0, 0}};
    const auto snapshot = odh::build_attempt_walk_obstacles(
        map, 1, 1, {32, 32, 2, 0}, 1, 0, 0,
        {1, 1, true, {}}, {1, true, {}}, entities, true, true, {30});
    if (snapshot.state != odh::AttemptWalkObstacleState::ready)
        std::cerr << "avatar snapshot unsupported: "
                  << odh::attempt_walk_obstacle_reason_name(snapshot.reason) << '\n';
    require(snapshot.state == odh::AttemptWalkObstacleState::ready,
            "complete avatar blocker snapshot should be usable");
    require(snapshot.entity_obstacle_ids == std::vector<std::int16_t>{30},
            "the blocked arrival should retain the avatar ID that supplied its footprint");
    const auto grid = odh::build_world_room_route_grid(snapshot);
    require(grid.state == odh::WorldRoomRouteGridState::ready && grid.grid &&
            !grid.grid->walkable({48, 48}),
            "the avatar footprint should block the stair arrival square");
}
}

int main() {
    test_avatar_blocker_identity();
    const auto blocked = run(true);
    require(blocked.status == odh::VmStatus::returned_false &&
            blocked.actor_position == odh::EntityPosition{32, 32, 1, 6} &&
            blocked.placement_checks == 1,
            "occupied stair arrival should fail immediately without moving the Sim");

    const auto clear = run(false);
    require(clear.status == odh::VmStatus::returned_true &&
            clear.actor_position == odh::EntityPosition{48, 48, 2, 6} &&
            clear.placement_checks == 2,
            "clear stair arrival should pass and place the Sim on the other floor");
    std::cout << "synthetic avatar blocker identity, furniture blocker, and clear-control checks passed\n";
}
