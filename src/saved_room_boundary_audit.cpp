#include "odh/saved_room_boundary_audit.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace odh {
namespace {
constexpr std::int64_t route_margin = 3;

SavedRoomBoundaryAudit unsupported(
    const SavedCollisionCandidate& candidate, SavedRoomBoundaryAuditReason reason,
    std::optional<std::uint16_t> room = std::nullopt) {
    return {candidate.object_id, SavedRoomBoundaryAuditState::unsupported, reason, room, 0, 0};
}

bool intersects(const CollisionBox& a, const RoomObstacleRect& b) {
    // Both APIs use strict rectangle overlap; touching edges are not overlap.
    return a.x1 < b.x2 && a.x2 > b.x1 && a.y1 < b.y2 && a.y2 > b.y1;
}

std::array<RoomObstacleRect, 4> outer_room_bands(const GeometricRoomBounds& bounds) {
    const auto bx = (static_cast<std::int64_t>(bounds.x) - 1) * 16;
    const auto by = (static_cast<std::int64_t>(bounds.y) - 1) * 16;
    const auto width = (static_cast<std::int64_t>(bounds.width) + 2) * 16;
    const auto height = (static_cast<std::int64_t>(bounds.height) + 2) * 16;
    return {{{static_cast<std::int32_t>(bx - 16), static_cast<std::int32_t>(by - 16),
              static_cast<std::int32_t>(bx + width + 16), static_cast<std::int32_t>(by)},
             {static_cast<std::int32_t>(bx - 16), static_cast<std::int32_t>(by + height),
              static_cast<std::int32_t>(bx + width + 16), static_cast<std::int32_t>(by + height + 16)},
             {static_cast<std::int32_t>(bx - 16), static_cast<std::int32_t>(by - 16),
              static_cast<std::int32_t>(bx), static_cast<std::int32_t>(by + height + 16)},
             {static_cast<std::int32_t>(bx + width), static_cast<std::int32_t>(by - 16),
              static_cast<std::int32_t>(bx + width + 16), static_cast<std::int32_t>(by + height + 16)}}};
}
}

SavedRoomBoundaryAudit audit_saved_room_boundary(
    const SavedCollisionCandidate& candidate,
    const GeometricRoomMap& room_map,
    const std::vector<RoomObstacleCandidateSet>& room_obstacles) {
    if (candidate.state == SavedCollisionCandidateState::absent)
        return {candidate.object_id, SavedRoomBoundaryAuditState::not_applicable,
                SavedRoomBoundaryAuditReason::collision_candidate_absent, std::nullopt, 0, 0};
    if (candidate.state != SavedCollisionCandidateState::candidate)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::collision_candidate_unsupported);
    if (!candidate.box)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::collision_box_missing);
    if (!candidate.position)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::entity_position_unavailable);

    const auto& position = *candidate.position;
    const auto room = geometric_room_at(room_map, position.x, position.y, position.level);
    if (!room)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::room_unmapped);
    if (*room == 0)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::outside_room, room);
    if (*room >= room_map.rooms.size() || room_map.rooms[*room].id != *room ||
        position.level < 1 || position.level > 2 ||
        room_map.rooms[*room].level != static_cast<std::uint8_t>(position.level - 1))
        return unsupported(candidate, SavedRoomBoundaryAuditReason::room_identity_invalid, room);
    const auto& bounds = room_map.rooms[*room].bounds;
    if (room_map.width == 0 || room_map.height == 0 || room_map.width > 64 || room_map.height > 64 ||
        bounds.width == 0 || bounds.height == 0 || bounds.x >= room_map.width ||
        bounds.y >= room_map.height || bounds.width > room_map.width - bounds.x ||
        bounds.height > room_map.height - bounds.y)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::room_bounds_invalid, room);

    const RoomObstacleCandidateSet* obstacles = nullptr;
    for (const auto& possible : room_obstacles) {
        if (possible.room != *room || possible.level != room_map.rooms[*room].level) continue;
        if (obstacles)
            return unsupported(candidate, SavedRoomBoundaryAuditReason::room_obstacles_ambiguous, room);
        obstacles = &possible;
    }
    if (!obstacles)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::room_obstacles_missing, room);
    if (!obstacles->complete)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::room_obstacles_incomplete, room);

    const auto& box = *candidate.box;
    if (box.x1 >= box.x2 || box.y1 >= box.y2)
        return unsupported(candidate, SavedRoomBoundaryAuditReason::collision_box_invalid, room);
    const std::int64_t x1 = std::int64_t(box.x1) - route_margin;
    const std::int64_t y1 = std::int64_t(box.y1) - route_margin;
    const std::int64_t x2 = std::int64_t(box.x2) + route_margin;
    const std::int64_t y2 = std::int64_t(box.y2) + route_margin;
    if (x1 < std::numeric_limits<std::int32_t>::min() ||
        y1 < std::numeric_limits<std::int32_t>::min() ||
        x2 > std::numeric_limits<std::int32_t>::max() ||
        y2 > std::numeric_limits<std::int32_t>::max())
        return unsupported(candidate, SavedRoomBoundaryAuditReason::collision_box_invalid, room);
    const CollisionBox route_box{static_cast<std::int32_t>(x1), static_cast<std::int32_t>(y1),
                                 static_cast<std::int32_t>(x2), static_cast<std::int32_t>(y2)};

    std::size_t overlaps = 0;
    std::size_t outer_overlaps = 0;
    for (const auto& rectangle : obstacles->rectangles) {
        if (rectangle.x1 >= rectangle.x2 || rectangle.y1 >= rectangle.y2)
            return unsupported(candidate, SavedRoomBoundaryAuditReason::room_obstacles_incomplete, room);
        if (intersects(route_box, rectangle)) ++overlaps;
    }
    for (const auto& band : outer_room_bands(bounds))
        if (intersects(route_box, band)) ++outer_overlaps;
    return {candidate.object_id,
            overlaps ? SavedRoomBoundaryAuditState::intersects_boundary
                     : outer_overlaps ? SavedRoomBoundaryAuditState::intersects_outer_room_bounds
                                      : SavedRoomBoundaryAuditState::no_intersection,
            SavedRoomBoundaryAuditReason::none, room, overlaps, outer_overlaps};
}

const char* saved_room_boundary_audit_state_name(SavedRoomBoundaryAuditState state) noexcept {
    switch (state) {
    case SavedRoomBoundaryAuditState::not_applicable: return "not_applicable";
    case SavedRoomBoundaryAuditState::no_intersection: return "no_intersection";
    case SavedRoomBoundaryAuditState::intersects_boundary: return "intersects_boundary";
    case SavedRoomBoundaryAuditState::intersects_outer_room_bounds: return "intersects_outer_room_bounds";
    case SavedRoomBoundaryAuditState::unsupported: return "unsupported";
    }
    return "unsupported";
}

const char* saved_room_boundary_audit_reason_name(SavedRoomBoundaryAuditReason reason) noexcept {
    switch (reason) {
    case SavedRoomBoundaryAuditReason::none: return "none";
    case SavedRoomBoundaryAuditReason::collision_candidate_absent: return "collision_candidate_absent";
    case SavedRoomBoundaryAuditReason::collision_candidate_unsupported: return "collision_candidate_unsupported";
    case SavedRoomBoundaryAuditReason::entity_position_unavailable: return "entity_position_unavailable";
    case SavedRoomBoundaryAuditReason::room_unmapped: return "room_unmapped";
    case SavedRoomBoundaryAuditReason::outside_room: return "outside_room";
    case SavedRoomBoundaryAuditReason::room_identity_invalid: return "room_identity_invalid";
    case SavedRoomBoundaryAuditReason::room_bounds_invalid: return "room_bounds_invalid";
    case SavedRoomBoundaryAuditReason::room_obstacles_missing: return "room_obstacles_missing";
    case SavedRoomBoundaryAuditReason::room_obstacles_ambiguous: return "room_obstacles_ambiguous";
    case SavedRoomBoundaryAuditReason::room_obstacles_incomplete: return "room_obstacles_incomplete";
    case SavedRoomBoundaryAuditReason::collision_box_missing: return "collision_box_missing";
    case SavedRoomBoundaryAuditReason::collision_box_invalid: return "collision_box_invalid";
    }
    return "invalid_reason";
}

} // namespace odh
