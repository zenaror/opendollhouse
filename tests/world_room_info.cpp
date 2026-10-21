#include "odh/world_room_info.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::size_t checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

odh::DefinitionIndex definitions() {
    odh::ObjectDefinition definition{};
    definition.guid = 100;
    definition.tile_width = 16;
    definition.footprint_mask = 0;
    return odh::DefinitionIndex({{"fixture", "fixture.iff", 0, definition}});
}

void add_object(odh::World& world, std::int16_t id, std::int32_t x,
                std::uint16_t flags = 0, bool is_static = true) {
    require(world.create(id, odh::EntityKind::object, {0, 0, 1}) &&
                world.define_type(id, 100) && world.declare_object_word(id, 8, static_cast<std::int16_t>(flags)) &&
                world.set_position(id, {x, 16, 1, 0}) &&
                world.set_static_footprint(id, is_static),
            "fixture object enters the live World");
}

} // namespace

int main() {
    try {
        const auto defs = definitions();
        odh::World world;
        require(world.create(1, odh::EntityKind::avatar, {0, 88, 1}) &&
                    world.declare_object_word(1, 8, 0) && world.set_position(1, {16, 16, 1, 0}) &&
                    world.set_static_footprint(1, false),
                "live avatar fixture");
        add_object(world, 2, 16, 0, false);
        add_object(world, 3, 48);
        add_object(world, 4, 16, 1U << 2); // HasZeroExtent has no obstacle.
        require(world.create(5, odh::EntityKind::object, {0, 0, 1}) &&
                    world.attach(2, 0, 5),
                "contained fixture is attached to an indexed object");

        const auto room_for = [](const odh::EntityPosition& p) -> std::optional<std::uint16_t> {
            return p.x < 32 ? std::optional<std::uint16_t>{1} : std::optional<std::uint16_t>{2};
        };
        const auto alive_avatar = [](std::int16_t, std::uint64_t) -> std::optional<std::int32_t> { return -1; };
        auto inventory = odh::snapshot_world_room_info(world, defs, room_for, alive_avatar, 1);
        require(inventory.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    inventory.obstacles.size() == 2 && inventory.obstacles[0].owner_id == 1 &&
                    inventory.obstacles[0].collection == odh::RoomInfoObstacleCollection::dynamic_list &&
                    inventory.obstacles[1].owner_id == 2,
                "live RoomInfo snapshot exposes the complete room inventory and collections");
        inventory = odh::snapshot_world_room_info(world, defs, room_for, alive_avatar, 2);
        require(inventory.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    inventory.obstacles.size() == 1 && inventory.obstacles[0].owner_id == 3,
                "live RoomInfo snapshot filters by runtime room membership");
        auto query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                               1, {13, 13, 19, 19}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 1 && query.intersections[0].owner_id == 2 &&
                    query.intersections[0].collection == odh::RoomInfoObstacleCollection::dynamic_list,
                "live query uses dynamic membership and excludes the target avatar");
        require(world.set_static_footprint(1, true), "avatar returns to static RoomInfo membership");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          1, {13, 13, 19, 19}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 2 && query.intersections[0].owner_id == 1 &&
                    query.intersections[1].owner_id == 2,
                "static RoomInfo query retains the target and sorts by owner ID");
        require(world.set_static_footprint(1, false), "avatar returns to dynamic RoomInfo membership");

        require(world.set_position(2, {40, 16, 1, 0}), "live object moves to another room");
        inventory = odh::snapshot_world_room_info(world, defs, room_for, alive_avatar, 2);
        require(inventory.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    inventory.obstacles.size() == 2 && inventory.obstacles[0].owner_id == 2 &&
                    inventory.obstacles[1].owner_id == 3,
                "next complete snapshot reflects live movement and footprint room change");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          1, {13, 13, 19, 19}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete && query.intersections.empty(),
                "each query rebuild observes live movement and geometric room change");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          2, {33, 9, 47, 23}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 2 && query.intersections[0].owner_id == 2 &&
                    query.intersections[1].owner_id == 3,
                "query returns live obstacles in the requested room sorted by owner ID");

        // Exercise a sequence of live mutations between independent queries.
        // RoomInfo is rebuilt on demand, so every next snapshot must reflect
        // the current World and must not retain an old membership or footprint.
        const auto live_timeout = [&](std::int16_t id, std::uint64_t generation)
            -> std::optional<std::int32_t> {
            const auto* entity = world.find(id);
            return entity && entity->generation == generation ? entity->kill_timeout : std::nullopt;
        };
        require(world.declare_object_word(2, 8, 1U << 2), "live HasZeroExtent toggle");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          2, {33, 9, 47, 23}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 1 && query.intersections[0].owner_id == 3,
                "next query observes a live HasZeroExtent change");
        require(world.declare_object_word(2, 8, 0), "restore live collision flags");
        require(world.attach(3, 0, 2), "move live obstacle into a container");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          2, {33, 9, 47, 23}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 1 && query.intersections[0].owner_id == 3,
                "next query omits an attached obstacle");
        require(world.detach(3, 0), "release live obstacle from its container");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          2, {33, 9, 47, 23}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 2 && query.intersections[0].owner_id == 2,
                "next query restores a detached obstacle");
        require(world.remove(2) && world.create(2, odh::EntityKind::object, {0, 0, 1}) &&
                    world.define_type(2, 100) && world.declare_object_word(2, 8, 0) &&
                    world.set_position(2, {40, 16, 1, 0}),
                "replace live obstacle using a new World generation");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          2, {33, 9, 47, 23}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 2 && query.intersections[0].owner_id == 2,
                "next query indexes a recreated entity with the reused ID");
        require(world.set_avatar_kill_timeout(1, 0), "mark live avatar as dying");
        query = odh::query_world_room_info(world, defs, room_for, live_timeout,
                                          1, {13, 13, 19, 19}, 99);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.empty(),
                "next query removes a dying avatar obstacle");
        require(world.set_avatar_kill_timeout(1, -1), "revive live avatar obstacle");
        query = odh::query_world_room_info(world, defs, room_for, live_timeout,
                                          1, {13, 13, 19, 19}, 99);
        require(query.status == odh::RoomInfoObstacleIndexStatus::complete &&
                    query.intersections.size() == 1 && query.intersections[0].owner_id == 1,
                "next query restores an alive avatar obstacle");

        const auto unknown_avatar = [](std::int16_t, std::uint64_t) -> std::optional<std::int32_t> { return {}; };
        query = odh::query_world_room_info(world, defs, room_for, unknown_avatar,
                                          1, {13, 13, 19, 19}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::unsupported &&
                    query.reason == odh::RoomInfoObstacleIndexReason::inventory_incomplete,
                "unknown avatar kill timeout fails closed");
        query = odh::query_world_room_info(world, defs, {}, alive_avatar,
                                          1, {13, 13, 19, 19}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::unsupported,
                "missing runtime room resolver fails closed");
        query = odh::query_world_room_info(world, defs, room_for, alive_avatar,
                                          1, {13, 13, 19, 19}, 1, 2);
        require(query.status == odh::RoomInfoObstacleIndexStatus::unsupported &&
                    query.reason == odh::RoomInfoObstacleIndexReason::inventory_over_budget,
                "entity count above the declared work budget fails closed");

        odh::World incomplete;
        require(incomplete.create(1, odh::EntityKind::object) &&
                    incomplete.define_type(1, 100) && incomplete.declare_object_word(1, 8, 0) &&
                    incomplete.set_position(1, {16, 16, 1, 0}),
                "incomplete object fixture");
        odh::World missing_flags;
        require(missing_flags.create(1, odh::EntityKind::object) &&
                    missing_flags.define_type(1, 100) && missing_flags.set_position(1, {16, 16, 1, 0}),
                "object with unknown flags fixture");
        query = odh::query_world_room_info(missing_flags, defs, room_for, alive_avatar,
                                          1, {0, 0, 10, 10}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::unsupported,
                "missing live flags do not become an empty obstacle");

        odh::DefinitionIndex ambiguous({
            {"one", "one.iff", 0, defs.unique(100)->definition},
            {"two", "two.iff", 0, defs.unique(100)->definition}});
        query = odh::query_world_room_info(incomplete, ambiguous, room_for, alive_avatar,
                                          1, {0, 0, 10, 10}, 1);
        require(query.status == odh::RoomInfoObstacleIndexStatus::unsupported,
                "ambiguous OBJD resolution fails closed");

        odh::World mutating_world;
        require(mutating_world.create(1, odh::EntityKind::avatar, {0, 88, 1}) &&
                    mutating_world.declare_object_word(1, 8, 0) &&
                    mutating_world.set_position(1, {16, 16, 1, 0}) &&
                    mutating_world.create(2, odh::EntityKind::object, {0, 0, 1}) &&
                    mutating_world.define_type(2, 100) &&
                    mutating_world.declare_object_word(2, 8, 0) &&
                    mutating_world.set_position(2, {16, 16, 1, 0}),
                "callback mutation fixture");
        bool mutated = false;
        const auto mutating_room_for = [&](odh::EntityPosition) -> std::optional<std::uint16_t> {
            if (!mutated) {
                mutated = true;
                require(mutating_world.remove(2), "resolver removes live object");
            }
            return 1;
        };
        query = odh::query_world_room_info(mutating_world, defs, mutating_room_for, alive_avatar,
                                          1, {13, 13, 19, 19}, 1);
        require(mutated && query.status == odh::RoomInfoObstacleIndexStatus::unsupported &&
                    query.reason == odh::RoomInfoObstacleIndexReason::inventory_incomplete,
                "World mutation during resolver invalidates the whole snapshot");
        odh::World lifecycle_world;
        require(lifecycle_world.create(1, odh::EntityKind::avatar, {0, 88, 1}) &&
                    lifecycle_world.declare_object_word(1, 8, 0) &&
                    lifecycle_world.set_position(1, {16, 16, 1, 0}),
                "avatar lifecycle query fixture");
        const auto lifecycle_timeout = [&](std::int16_t id, std::uint64_t generation)
            -> std::optional<std::int32_t> {
            const auto* entity = lifecycle_world.find(id);
            return entity && entity->generation == generation ? entity->kill_timeout : std::nullopt;
        };
        bool lifecycle_changed = false;
        const auto lifecycle_room = [&](odh::EntityPosition) -> std::optional<std::uint16_t> {
            if (!lifecycle_changed) {
                lifecycle_changed = true;
                require(lifecycle_world.set_avatar_kill_timeout(1, 0), "resolver changes avatar lifecycle");
            }
            return 1;
        };
        query = odh::query_world_room_info(lifecycle_world, defs, lifecycle_room, lifecycle_timeout,
                                          1, {13, 13, 19, 19}, 1);
        require(lifecycle_changed && query.status == odh::RoomInfoObstacleIndexStatus::unsupported,
                "avatar lifecycle mutation invalidates the captured obstacle state");

        std::cout << checks << " live World RoomInfo checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "World RoomInfo test failed: " << error.what() << '\n';
        return 1;
    }
}
