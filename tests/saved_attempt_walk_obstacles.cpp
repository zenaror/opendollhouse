#include "odh/saved_attempt_walk_obstacles.hpp"

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

bool is_rect(const odh::RoomObstacleRect& actual, std::int32_t x1, std::int32_t y1,
             std::int32_t x2, std::int32_t y2) {
    return actual.x1 == x1 && actual.y1 == y1 && actual.x2 == x2 && actual.y2 == y2;
}

odh::GeometricRoomMap room_map() {
    odh::GeometricRoomMap map;
    map.width = 5;
    map.height = 5;
    map.levels[0].assign(25, 1U | (1U << 16));
    map.levels[1].assign(25, 1U | (1U << 16));
    map.rooms.resize(2);
    map.rooms[1] = {1, 0, 9, {1, 1, 3, 3}, false, false, {}};
    return map;
}

odh::RoomObstacleCandidateSet room_obstacles() {
    return {1, 0, true, {{60, 60, 65, 65}}};
}

odh::WallObstacleCandidateSet wall_obstacles() {
    return {0, true, {{40, 40, 45, 45}}};
}

odh::AttemptWalkObstacleSnapshot build(
    const std::vector<odh::AttemptWalkObstacleEntity>& entities = {},
    std::uint16_t actor_flags = 0,
    std::optional<std::uint16_t> actor_ep5 = 0,
    bool avatar_complete = true,
    std::vector<std::int16_t> avatar_set = {},
    odh::AttemptWalkEntryPoint5Resolver resolver = {},
    odh::RoomObstacleCandidateSet rooms = room_obstacles(),
    odh::WallObstacleCandidateSet walls = wall_obstacles(),
    bool inventory_complete = true,
    odh::AttemptWalkObstacleLimits limits = {}) {
    return odh::build_attempt_walk_obstacles(
        room_map(), 1, 0, {32, 32, 1, 0}, 10, actor_flags, actor_ep5,
        rooms, walls, entities, inventory_complete, avatar_complete,
        avatar_set, resolver, limits);
}

odh::AttemptWalkObstacleEntity object(
    std::int16_t id, std::uint16_t flags = 0,
    std::optional<std::uint16_t> ep5 = 0,
    std::optional<std::uint16_t> room = 1,
    odh::AttemptWalkFootprintState state = odh::AttemptWalkFootprintState::present,
    std::optional<odh::CollisionBox> box = odh::CollisionBox{50, 50, 55, 55}) {
    return {id, false, room, state, box, flags, ep5};
}
}

int main() {
    using namespace odh;

    auto result = build();
    require(result.state == AttemptWalkObstacleState::ready &&
            result.obstacles.size() == 6 &&
            is_rect(result.obstacles[0], 40, 40, 45, 45) &&
            is_rect(result.obstacles[1], 60, 60, 65, 65),
            "WallObs and RoomObs must both be present in the base routing obstacle list");
    require(is_rect(result.obstacles[2], -16, -16, 96, 0) &&
            is_rect(result.obstacles[3], -16, 80, 96, 96) &&
            is_rect(result.obstacles[4], -16, -16, 0, 96) &&
            is_rect(result.obstacles[5], 80, -16, 96, 96),
            "AttemptWalk must append the exact four one-tile room-bound bands");
    require(!result.start_intersects, "clear actor start must remain unblocked");

    result = build({object(20)});
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids == std::vector<std::int16_t>{20} &&
            is_rect(result.obstacles.back(), 47, 47, 58, 58),
            "blocking object footprint must receive the reference three-unit margin");

    result = build({object(20, 16)});
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids.empty() && result.skipped_nonblocking_entities == 1,
            "AllowPersonIntersection object must not block unless Disallow also applies");
    result = build({object(20, 18)});
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids == std::vector<std::int16_t>{20},
            "DisallowPersonIntersection must dominate AllowPersonIntersection");

    int ep5_calls = 0;
    auto actor_exemption = [&ep5_calls](std::int16_t caller, std::int16_t other) {
        ++ep5_calls;
        return caller == 10 && other == 20 ? std::optional<bool>(true)
                                           : std::optional<bool>{};
    };
    result = build({object(20, 0, 0)}, 0, 4108, true, {}, actor_exemption);
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids.empty() && result.exempted_by_entry_point5 == 1 &&
            result.entry_point5_calls == 1 && ep5_calls == 1,
            "true actor EP5 must exempt the pair without calling blocker EP5");

    result = build({object(20, 0, {})}, 0, 4108, true, {}, actor_exemption);
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids.empty() && result.exempted_by_entry_point5 == 1,
            "true actor EP5 must short-circuit before unavailable blocker EP5 metadata");

    auto blocker_exemption = [&ep5_calls](std::int16_t caller, std::int16_t other) {
        ++ep5_calls;
        if (caller == 10 && other == 20) return std::optional<bool>(false);
        if (caller == 20 && other == 10) return std::optional<bool>(true);
        return std::optional<bool>{};
    };
    result = build({object(20, 0, 4108)}, 0, 0, true, {}, blocker_exemption);
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids.empty() && result.entry_point5_calls == 1,
            "true blocker EP5 must exempt after actor EP5 false");

    auto unknown_ep5 = [](std::int16_t, std::int16_t) { return std::optional<bool>{}; };
    result = build({object(20, 0, 4108)}, 0, 0, true, {}, unknown_ep5);
    require(result.state == AttemptWalkObstacleState::unsupported &&
            result.reason == AttemptWalkObstacleReason::entry_point5_result_unknown &&
            result.obstacles.empty(),
            "unknown bilateral EP5 must fail closed without returning partial obstacles");

    auto actor_inside = object(20, 0, 0, 1, AttemptWalkFootprintState::present,
                               CollisionBox{30, 30, 35, 35});
    result = build({actor_inside});
    require(result.state == AttemptWalkObstacleState::ready && result.start_intersects,
            "start-point intersection must be reported using strict VMObstacle geometry");

    auto other_room = object(20, 0, 0, 2);
    result = build({other_room});
    require(result.state == AttemptWalkObstacleState::ready && result.entity_obstacle_ids.empty(),
            "known entities in another room must not enter this room obstacle set");

    auto known_absent = object(20, 0, 0, 1, AttemptWalkFootprintState::absent, {});
    result = build({known_absent});
    require(result.state == AttemptWalkObstacleState::ready && result.entity_obstacle_ids.empty(),
            "known footprint absence must skip an entity before flags and EP5");

    auto unknown_room = object(20, 0, 0, {}, AttemptWalkFootprintState::present);
    result = build({unknown_room});
    require(result.state == AttemptWalkObstacleState::unsupported &&
            result.reason == AttemptWalkObstacleReason::entity_room_unknown &&
            result.unsupported_entity_id == 20,
            "unknown room membership must not be treated as out of room");

    auto avatar = object(21);
    avatar.avatar = true;
    result = build({avatar}, 0, 0, false, {});
    require(result.state == AttemptWalkObstacleState::unsupported &&
            result.reason == AttemptWalkObstacleReason::avatar_consideration_unknown,
            "unknown avatar consideration membership must fail closed");
    result = build({avatar}, 0, 0, true, {21});
    require(result.state == AttemptWalkObstacleState::ready &&
            result.entity_obstacle_ids == std::vector<std::int16_t>{21},
            "explicitly considered avatar must be treated as a possible blocker");
    result = build({avatar}, 16, 0, false, {});
    require(result.state == AttemptWalkObstacleState::ready && result.entity_obstacle_ids.empty(),
            "actor AllowPersonIntersection must disable avatar consideration");

    result = build({object(20)}, 0, 0, true, {}, {}, room_obstacles(),
                   wall_obstacles(), false);
    require(result.state == AttemptWalkObstacleState::unsupported && result.obstacles.empty(),
            "incomplete entity inventories must not produce a ready partial snapshot");
    result = build({object(20)}, 0, 0, true, {}, {},
                   RoomObstacleCandidateSet{1, 0, false, {}}, wall_obstacles());
    require(result.state == AttemptWalkObstacleState::unsupported &&
            result.reason == AttemptWalkObstacleReason::room_obstacles_incomplete,
            "incomplete RoomObs must fail closed");
    result = build({object(20)}, 0, 0, true, {}, {}, room_obstacles(),
                   WallObstacleCandidateSet{0, false, {}});
    require(result.state == AttemptWalkObstacleState::unsupported &&
            result.reason == AttemptWalkObstacleReason::wall_obstacles_incomplete,
            "incomplete WallObs must fail closed");

    AttemptWalkObstacleLimits too_small;
    too_small.rectangles = 6;
    result = build({object(20)}, 0, 0, true, {}, {}, room_obstacles(),
                   wall_obstacles(), true, too_small);
    require(result.state == AttemptWalkObstacleState::unsupported && result.obstacles.empty(),
            "rectangle budget exhaustion must not expose a partial ready obstacle list");
    AttemptWalkObstacleLimits no_ep5_budget;
    no_ep5_budget.entry_point5_calls = 1;
    auto two_call_pair = build({object(20, 0, 4108)}, 0, 4108, true, {},
                               [](std::int16_t, std::int16_t) { return std::optional<bool>(false); },
                               room_obstacles(), wall_obstacles(), true, no_ep5_budget);
    require(two_call_pair.state == AttemptWalkObstacleState::unsupported &&
            two_call_pair.reason == AttemptWalkObstacleReason::entry_point5_budget_exhausted &&
            two_call_pair.obstacles.empty(),
            "bilateral EP5 execution must respect its explicit call budget");

    std::cout << "Saved AttemptWalk obstacle snapshot checks passed\n";
}
