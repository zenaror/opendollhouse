#include "odh/room_obstacles.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class Action>
void bad(const Action& action) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid room obstacle input accepted");
}
odh::GeometricRoomMap map(std::uint32_t width, std::uint32_t height) {
    odh::GeometricRoomMap result;
    result.width = width;
    result.height = height;
    const auto cells = std::size_t(width) * height;
    for (auto& level : result.levels) level.resize(cells);
    result.rooms.push_back({});
    return result;
}
void add_room(odh::GeometricRoomMap& result, std::uint16_t id,
              std::uint8_t level, odh::GeometricRoomBounds bounds) {
    if (result.rooms.size() <= id) result.rooms.resize(std::size_t(id) + 1);
    result.rooms[id].id = id;
    result.rooms[id].level = level;
    result.rooms[id].bounds = bounds;
}
}

int main() {
    try {
        // A 3x3 expanded room bound around the center tile. The four runs are
        // conservatively blocked, then merged with the reference's ordered
        // inclusive rectangle rules.
        auto rooms = map(5, 5);
        add_room(rooms, 1, 0, {2, 2, 1, 1});
        add_room(rooms, 2, 0, {0, 0, 5, 5});
        std::fill(rooms.levels[0].begin(), rooms.levels[0].end(), 0x00020002U);
        rooms.levels[0][2 * 5 + 2] = 0x00010001U;
        auto result = odh::generate_room_obstacle_candidates(rooms, 1, 0);
        require(!result.complete, "missing doors and portals remain explicitly incomplete");
        require(result.rectangles.size() == 4, "source optimizer preserves separate obstacle runs");
        require(result.rectangles[0].x1 == 13 && result.rectangles[0].y1 == 13 &&
                result.rectangles[0].x2 == 67 && result.rectangles[0].y2 == 35,
                "top run uses expanded VMObstacle tile coordinates");
        require(result.rectangles[1].x1 == 13 && result.rectangles[1].y1 == 29 &&
                result.rectangles[1].x2 == 35 && result.rectangles[1].y2 == 51,
                "middle-left run stays blocked");
        require(result.rectangles[2].x1 == 45 && result.rectangles[2].y1 == 29 &&
                result.rectangles[2].x2 == 67 && result.rectangles[2].y2 == 51,
                "middle-right run stays separate");
        require(result.rectangles[3].x1 == 13 && result.rectangles[3].y1 == 45 &&
                result.rectangles[3].x2 == 67 && result.rectangles[3].y2 == 67,
                "bottom run stays separate when source merge cannot join it");
        rooms.floor_resources_resolved = true;
        const std::vector<odh::RoomPortalOpening> door_opening{{1, 2, 1, 0}};
        result = odh::generate_room_obstacle_candidates(rooms, 1, 0, door_opening, true);
        require(result.complete, "prepared map and complete portal inventory complete room-obstacle generation");
        require(result.rectangles.size() == 3 &&
                result.rectangles[0].x1 == 13 && result.rectangles[0].x2 == 35 &&
                result.rectangles[0].y1 == 13 && result.rectangles[0].y2 == 51 &&
                result.rectangles[1].x1 == 45 && result.rectangles[1].x2 == 67 &&
                result.rectangles[1].y1 == 13 && result.rectangles[1].y2 == 51,
                "portal tile splits the boundary run exactly as GenerateRoomObs");
        require(!odh::generate_room_obstacle_candidates(rooms, 1, 0, door_opening, false).complete,
                "unknown portal inventory keeps room-obstacle generation incomplete");
        rooms.floor_resources_resolved = false;
        require(!odh::generate_room_obstacle_candidates(rooms, 1, 0, door_opening, true).complete,
                "unprepared floor resources keep room-obstacle generation incomplete");

        auto portal_rooms = map(4, 3);
        add_room(portal_rooms, 1, 0, {0, 0, 4, 3});
        add_room(portal_rooms, 2, 0, {0, 0, 4, 3});
        std::fill(portal_rooms.levels[0].begin(), portal_rooms.levels[0].end(), 0x00010001U);
        portal_rooms.levels[0][1 * 4 + 2] = 0x00020002U;
        const std::vector<odh::RoomPortalMemberSnapshot> portal_group{
            {10, 10, true, false, false, odh::EntityPosition{16, 16, 1, 0}},
            {11, 10, false, true, false, odh::EntityPosition{32, 16, 1, 0}}};
        auto inventory = odh::build_room_portal_inventory(portal_rooms, portal_group);
        require(inventory.complete && inventory.overlapping_door_tiles == 0 &&
                inventory.openings.size() == 1 && inventory.openings[0].target_room == 2 &&
                inventory.openings[0].tile_x == 1 && inventory.openings[0].tile_y == 1,
                "portal group opens its positioned door tile toward the member's geometric room");

        auto missing_door = portal_group;
        missing_door[0].position.reset();
        inventory = odh::build_room_portal_inventory(portal_rooms, missing_door);
        require(!inventory.complete && inventory.openings.empty(),
                "architectural door without position makes the inventory incomplete");
        auto outside_door = portal_group;
        outside_door[0].position->x = 64;
        inventory = odh::build_room_portal_inventory(portal_rooms, outside_door);
        require(!inventory.complete && inventory.openings.empty(),
                "architectural door outside the room map makes the inventory incomplete");
        auto missing_portal = portal_group;
        missing_portal[1].position.reset();
        inventory = odh::build_room_portal_inventory(portal_rooms, missing_portal);
        require(!inventory.complete && inventory.openings.empty(),
                "Portal15 member without a geometric room makes the inventory incomplete");

        auto overlapping = portal_group;
        overlapping.push_back({20, 20, true, true, false, odh::EntityPosition{16, 16, 1, 0}});
        inventory = odh::build_room_portal_inventory(portal_rooms, overlapping);
        require(!inventory.complete && inventory.overlapping_door_tiles == 1,
                "different door groups overlapping a tile invalidate completeness");
        auto same_group_overlap = portal_group;
        same_group_overlap.push_back({12, 10, true, false, false, odh::EntityPosition{16, 16, 1, 0}});
        inventory = odh::build_room_portal_inventory(portal_rooms, same_group_overlap);
        require(inventory.complete && inventory.overlapping_door_tiles == 0,
                "members of one door group sharing a tile are not an ordering ambiguity");
        auto duplicate_member = portal_group;
        duplicate_member.push_back(portal_group.front());
        inventory = odh::build_room_portal_inventory(portal_rooms, duplicate_member);
        require(!inventory.complete,
                "duplicate saved entity identity invalidates portal inventory");

        // Room zero uses the expanded interior, which covers every tile. A
        // contiguous outside map collapses to a single border candidate.
        rooms = map(3, 2);
        std::fill(rooms.levels[0].begin(), rooms.levels[0].end(), 0x00010001U);
        result = odh::generate_room_obstacle_candidates(rooms, 0, 0);
        require(result.rectangles.size() == 1, "outside-room rows merge vertically");
        require(result.rectangles[0].x1 == -3 && result.rectangles[0].y1 == -3 &&
                result.rectangles[0].x2 == 51 && result.rectangles[0].y2 == 35,
                "outside-room scan matches expanded-interior bounds");

        // The packed horizontal-diagonal flag occupies bit 31 and must not
        // contaminate the second room ID. Either half belongs to its room.
        rooms = map(1, 1);
        add_room(rooms, 1, 0, {0, 0, 1, 1});
        add_room(rooms, 2, 0, {0, 0, 1, 1});
        rooms.levels[0][0] = 0x80020001U;
        require(odh::generate_room_obstacle_candidates(rooms, 1, 0).rectangles.empty(),
                "first diagonal half is room membership");
        require(odh::generate_room_obstacle_candidates(rooms, 2, 0).rectangles.empty(),
                "second diagonal half masks orientation bit");
        result = odh::generate_room_obstacle_candidates(rooms, 0, 0);
        require(result.rectangles.size() == 1 && result.rectangles[0].x1 == -3,
                "room zero treats both nonzero halves as outside");

        bad([&] { (void)odh::generate_room_obstacle_candidates(rooms, 3, 0); });
        bad([&] { (void)odh::generate_room_obstacle_candidates(rooms, 2, 1); });
        bad([&] { auto broken = rooms; broken.levels[1].pop_back();
            (void)odh::generate_room_obstacle_candidates(broken, 1, 0); });
        bad([&] { auto broken = rooms; broken.width = 65;
            (void)odh::generate_room_obstacle_candidates(broken, 1, 0); });
        bad([&] { (void)odh::generate_room_obstacle_candidates(rooms, 1, 0, {{1, 3, 0, 0}}, true); });
        std::cout << checks << " room obstacle checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
