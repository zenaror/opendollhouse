#include "odh/world_attempt_walk.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

odh::GeometricRoomMap room_map() {
    odh::GeometricRoomMap map;
    map.width = 5; map.height = 5;
    map.levels[0].assign(25, 1U | (1U << 16));
    map.levels[1].assign(25, 1U | (1U << 16));
    map.rooms.resize(2);
    map.rooms[1] = {1, 0, 9, {1, 1, 3, 3}, false, false, {}};
    return map;
}

odh::DefinitionIndex definitions() {
    odh::ObjectDefinition definition{};
    definition.guid = 100; definition.tile_width = 16; definition.footprint_mask = 0;
    return odh::DefinitionIndex({{"fixture", "object.iff", 0, definition}});
}

bool setup(odh::World& world, std::uint16_t action = 0) {
    const std::vector<odh::EntryPoint> entries(6, {0, 0});
    auto actor_entries = entries; actor_entries[5].action = action;
    auto object_entries = entries; object_entries[5].action = action;
    return world.create(1, odh::EntityKind::avatar, {0, 88, 1}) &&
        world.define_type(1, 100) && world.declare_object_word(1, 8, 0) &&
        world.set_position(1, {32, 32, 1, 0}) &&
        world.define_entry_points(1, actor_entries) &&
        world.create(2, odh::EntityKind::object, {0, 0, 1}) && world.define_type(2, 100) &&
        world.declare_object_word(2, 8, 0) && world.set_position(2, {50, 50, 1, 0}) &&
        world.define_entry_points(2, object_entries);
}
}

int main() {
    using namespace odh;
    const auto defs = definitions();
    const auto rooms = room_map();
    const RoomObstacleCandidateSet room_obstacles{1, 0, true, {{70, 70, 75, 75}}};
    const WallObstacleCandidateSet wall_obstacles{0, true, {{80, 80, 85, 85}}};
    const RuntimeRoomResolver room_for = [](const EntityPosition&) -> std::optional<std::uint16_t> { return 1; };
    const AvatarKillTimeoutResolver timeout = [](std::int16_t, std::uint64_t) -> std::optional<std::int32_t> { return -1; };

    World world;
    require(setup(world, 300), "live World fixture includes actor and obstacle EntryPoint 5 tables");
    auto result = build_world_attempt_walk_obstacles(world, defs, room_for, timeout,
        rooms, 1, 0, 1, room_obstacles, wall_obstacles, true, {},
        [](std::int16_t, std::int16_t) -> std::optional<bool> { return false; });
    require(result.state == AttemptWalkObstacleState::ready &&
        result.entity_obstacle_ids == std::vector<std::int16_t>{2} &&
        result.entry_point5_calls == 2,
        "adapter feeds live RoomInfo boxes, flags and bilateral EP5 into AttemptWalk");
    require(result.obstacles.size() == 7,
        "live AttemptWalk output combines wall, room, bands and expanded entity footprints");

    World exempt_world;
    require(setup(exempt_world, 300), "EP5 callback fixture uses private actions");
    result = build_world_attempt_walk_obstacles(exempt_world, defs, room_for, timeout,
        rooms, 1, 0, 1, room_obstacles, wall_obstacles, true, {},
        [](std::int16_t, std::int16_t) -> std::optional<bool> { return true; });
    require(result.state == AttemptWalkObstacleState::ready &&
        result.entity_obstacle_ids.empty() && result.exempted_by_entry_point5 == 1,
        "live AttemptWalk retains bilateral EntryPoint 5 exemption semantics");

    World mutating_world;
    require(setup(mutating_world, 300), "mutation fixture has private EntryPoint 5 actions");
    result = build_world_attempt_walk_obstacles(mutating_world, defs, room_for, timeout,
        rooms, 1, 0, 1, room_obstacles, wall_obstacles, true, {},
        [&](std::int16_t, std::int16_t) -> std::optional<bool> {
            require(mutating_world.set_position(2, {51, 50, 1, 0}), "resolver mutates obstacle position");
            return false;
        });
    require(result.state == AttemptWalkObstacleState::unsupported &&
        result.reason == AttemptWalkObstacleReason::entity_inventory_incomplete &&
        result.obstacles.empty(),
        "mutation during EP5 resolution invalidates the whole runtime AttemptWalk snapshot");

    std::cout << "Live World AttemptWalk adapter checks passed\n";
}
