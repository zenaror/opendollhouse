#include "odh/room_score.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::GeometricRoomMap map_with_rooms(bool outside = false) {
    odh::GeometricRoomMap map;
    map.width = 1;
    map.height = 1;
    map.floor_resources_resolved = true;
    map.light_base_rooms_resolved = true;
    map.levels[0] = {1U | (1U << 16)};
    map.levels[1] = {2U | (2U << 16)};
    map.rooms = {
        {0,0,0,{},false,false,{},0},
        {1,0,6,{0,0,1,1},outside,false,{2},1},
        {2,1,6,{0,0,1,1},outside,false,{1},1}
    };
    return map;
}
}

int main() {
    try {
        const std::vector<odh::RoomScoreContribution> impacts{
            {0,0,1,20}, {0,0,2,16}
        };
        auto map = map_with_rooms();
        auto result = odh::calculate_room_score(map, impacts, 0, 0, 2);
        require(result && result->updated && result->room == 2 && result->light_base_room == 1 &&
                result->score == 26,
                "connected support rooms share area and sum their RoomImpact values");

        map = map_with_rooms(true);
        result = odh::calculate_room_score(map, {{0,0,1,40}, {0,0,2,20}}, 0, 0, 1);
        require(result && result->score == -13,
                "outside room uses fixed divisor 30 and subtracts the outside baseline");

        result = odh::calculate_room_score(map_with_rooms(), {{0,0,1,-13}}, 0, 0, 1);
        require(result && result->score == -23,
                "negative RoomImpact truncates toward zero before subtracting the inside baseline");

        result = odh::calculate_room_score(map_with_rooms(), {{0,0,1,4000}}, 0, 0, 1);
        require(result && result->score == 100,
                "room score clamps after applying the inside baseline");

        auto unresolved = map_with_rooms();
        unresolved.floor_resources_resolved = false;
        require(!odh::calculate_room_score(unresolved, impacts, 0, 0, 1),
                "unresolved floor identities cannot produce a room score");

        result = odh::calculate_room_score(map_with_rooms(), impacts, -16, -16, 0);
        require(result && !result->updated && result->room == 0,
                "outside-lot target is the reference's room-zero no-op");
        std::cout << "room_score: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "room_score: " << error.what() << '\n';
        return 1;
    }
}
