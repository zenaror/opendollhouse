#include "odh/saved_room_boundary_audit.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::GeometricRoomMap make_room_map() {
    odh::GeometricRoomMap map;
    map.width = map.height = 5;
    map.floor_resources_resolved = true;
    for (auto& level : map.levels) level.assign(25, 0U);
    map.rooms.resize(2);
    map.rooms[1] = {1, 0, 9, {1, 1, 3, 3}, false, false, {}};
    for (std::uint32_t y = 1; y <= 3; ++y)
        for (std::uint32_t x = 1; x <= 3; ++x)
            map.levels[0][std::size_t(y) * map.width + x] = 0x00010001U;
    return map;
}
odh::GeometricRoomMap make_full_room_map() {
    auto map = make_room_map();
    map.rooms[1] = {1, 0, 25, {0, 0, 5, 5}, false, false, {}};
    for (std::uint32_t y = 0; y < 5; ++y)
        for (std::uint32_t x = 0; x < 5; ++x)
            map.levels[0][std::size_t(y) * map.width + x] = 0x00010001U;
    return map;
}
odh::SavedCollisionCandidate candidate(odh::CollisionBox box,
                                       odh::EntityPosition position = {40, 40, 1, 0},
                                       bool avatar = false) {
    return {17, avatar, odh::SavedCollisionCandidateState::candidate,
            odh::SavedCollisionCandidateReason::none, box, position};
}
}

int main() {
    try {
        using AuditState = odh::SavedRoomBoundaryAuditState;
        using AuditReason = odh::SavedRoomBoundaryAuditReason;
        auto map = make_room_map();
        auto obstacles = std::vector<odh::RoomObstacleCandidateSet>{
            odh::generate_room_obstacle_candidates(map, 1, 0, {}, true)};
        require(obstacles[0].complete && !obstacles[0].rectangles.empty(),
                "fixture provides a complete room boundary candidate");

        auto audit = odh::audit_saved_room_boundary(
            candidate({36, 36, 44, 44}), map, obstacles);
        require(audit.state == AuditState::no_intersection && audit.room == 1 &&
                audit.intersecting_rectangles == 0,
                "interior object box avoids the route-expanded room boundary");

        audit = odh::audit_saved_room_boundary(
            candidate({16, 36, 24, 44}, {20, 40, 1, 0}), map, obstacles);
        require(audit.state == AuditState::intersects_boundary && audit.room == 1 &&
                audit.intersecting_rectangles > 0,
                "object box near the room edge intersects its expanded boundary candidates");

        for (const auto& [box, side] : std::vector<std::pair<odh::CollisionBox, const char*>>{
                 {{36, -5, 44, 44}, "top"}, {{36, 36, 44, 85}, "bottom"},
                 {{-5, 36, 44, 44}, "left"}, {{36, 36, 85, 44}, "right"}}) {
            audit = odh::audit_saved_room_boundary(candidate(box), map, obstacles);
            require(audit.intersecting_outer_bounds > 0,
                    side);
        }
        audit = odh::audit_saved_room_boundary(
            candidate({36, 3, 44, 44}), map, obstacles);
        require(audit.intersecting_outer_bounds == 0,
                "strict overlap treats a route box touching the outer top band as non-intersecting");

        auto invalid_bounds = map;
        invalid_bounds.rooms[1].bounds.x = invalid_bounds.width;
        audit = odh::audit_saved_room_boundary(
            candidate({36, 36, 44, 44}), invalid_bounds, obstacles);
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::room_bounds_invalid,
                "malformed geometric room bounds remain unsupported");

        const auto full_room = make_full_room_map();
        const auto full_room_obstacles = std::vector<odh::RoomObstacleCandidateSet>{
            odh::generate_room_obstacle_candidates(full_room, 1, 0, {}, true)};
        require(full_room_obstacles.front().complete && full_room_obstacles.front().rectangles.empty(),
                "full-map room has no RoomObs rectangles, isolating AttemptWalk outer bands");
        for (const auto& [box, position, side] : std::vector<std::tuple<odh::CollisionBox, odh::EntityPosition, const char*>>{
                 {{36, -14, 44, 12}, {40, 8, 1, 0}, "top"},
                 {{36, 68, 44, 101}, {40, 72, 1, 0}, "bottom"},
                 {{-14, 36, 12, 44}, {8, 40, 1, 0}, "left"},
                 {{68, 36, 101, 44}, {72, 40, 1, 0}, "right"}}) {
            audit = odh::audit_saved_room_boundary(candidate(box, position), full_room, full_room_obstacles);
            if (!(audit.state == AuditState::intersects_outer_room_bounds &&
                  audit.intersecting_rectangles == 0 && audit.intersecting_outer_bounds > 0))
                throw std::runtime_error(std::string("outer-room band classification failed: ") + side +
                    " state=" + odh::saved_room_boundary_audit_state_name(audit.state) +
                    " roomObs=" + std::to_string(audit.intersecting_rectangles) +
                    " outer=" + std::to_string(audit.intersecting_outer_bounds));
            ++checks;
        }
        audit = odh::audit_saved_room_boundary(
            candidate({36, -13, 44, 12}, {40, 8, 1, 0}), full_room, full_room_obstacles);
        require(audit.state == AuditState::no_intersection && audit.intersecting_outer_bounds == 0,
                "strict overlap treats a box touching the outer-room band as clear of that band");

        audit = odh::audit_saved_room_boundary(
            candidate({22, 36, 25, 44}, {24, 40, 1, 0}), map, obstacles);
        require(audit.state == AuditState::no_intersection,
                "strict overlap treats a route box touching the boundary edge as non-intersecting");

        audit = odh::audit_saved_room_boundary(
            candidate({21, 37, 27, 43}, {24, 40, 1, 0}, true), map, obstacles);
        require(audit.state == AuditState::intersects_boundary,
                "avatar center±3 shape plus route margin reaches the boundary at total radius six");

        auto unsupported = candidate({36, 36, 44, 44});
        unsupported.state = odh::SavedCollisionCandidateState::unsupported;
        audit = odh::audit_saved_room_boundary(
            unsupported, map, obstacles);
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::collision_candidate_unsupported,
                "unsupported dynamic entity state never becomes a negative boundary result");

        auto absent = candidate({36, 36, 44, 44});
        absent.state = odh::SavedCollisionCandidateState::absent;
        audit = odh::audit_saved_room_boundary(
            absent, map, obstacles);
        require(audit.state == AuditState::not_applicable &&
                audit.reason == AuditReason::collision_candidate_absent,
                "absent persistent obstacle is explicitly not applicable");

        auto missing_position = candidate({36, 36, 44, 44});
        missing_position.position.reset();
        audit = odh::audit_saved_room_boundary(missing_position, map, obstacles);
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::entity_position_unavailable,
                "missing entity position remains unsupported");

        audit = odh::audit_saved_room_boundary(
            candidate({-4, -4, 4, 4}, {0, 0, 1, 0}), map, obstacles);
        require(audit.state == AuditState::unsupported && audit.room == 0 &&
                audit.reason == AuditReason::outside_room,
                "outside room is not assigned internal-room boundary evidence");

        auto incomplete = obstacles;
        incomplete[0].complete = false;
        audit = odh::audit_saved_room_boundary(
            candidate({36, 36, 44, 44}), map, incomplete);
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::room_obstacles_incomplete,
                "incomplete room obstacles do not produce a negative intersection result");

        audit = odh::audit_saved_room_boundary(
            candidate({36, 36, 44, 44}), map, {});
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::room_obstacles_missing,
                "missing room obstacle set remains unsupported");

        auto ambiguous = obstacles;
        ambiguous.push_back(obstacles.front());
        audit = odh::audit_saved_room_boundary(
            candidate({36, 36, 44, 44}), map, ambiguous);
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::room_obstacles_ambiguous,
                "duplicate room identity does not select an arbitrary obstacle set");

        audit = odh::audit_saved_room_boundary(
            candidate({44, 40, 36, 48}), map, obstacles);
        require(audit.state == AuditState::unsupported &&
                audit.reason == AuditReason::collision_box_invalid,
                "invalid collision shape remains unsupported");

        std::cout << checks << " saved room boundary audit checks passed (not route clearance)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Saved room boundary audit test failed: " << error.what() << '\n';
        return 1;
    }
}
