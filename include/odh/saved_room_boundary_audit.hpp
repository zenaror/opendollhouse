#pragma once

#include "odh/room_obstacles.hpp"
#include "odh/saved_collision_candidates.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {

enum class SavedRoomBoundaryAuditState : std::uint8_t {
    not_applicable,
    no_intersection,
    intersects_boundary,
    intersects_outer_room_bounds,
    unsupported
};

enum class SavedRoomBoundaryAuditReason : std::uint8_t {
    none,
    collision_candidate_absent,
    collision_candidate_unsupported,
    entity_position_unavailable,
    room_unmapped,
    outside_room,
    room_identity_invalid,
    room_bounds_invalid,
    room_obstacles_missing,
    room_obstacles_ambiguous,
    room_obstacles_incomplete,
    collision_box_missing,
    collision_box_invalid
};

struct SavedRoomBoundaryAudit {
    std::int16_t object_id = 0;
    SavedRoomBoundaryAuditState state = SavedRoomBoundaryAuditState::unsupported;
    SavedRoomBoundaryAuditReason reason = SavedRoomBoundaryAuditReason::collision_candidate_unsupported;
    std::optional<std::uint16_t> room;
    std::size_t intersecting_rectangles = 0;
    std::size_t intersecting_outer_bounds = 0;
};

// Checks only whether a supported entity box, expanded by the Simitone avatar
// route margin, intersects candidate RoomObs rectangles or AttemptWalk's
// one-tile outer room bands for its center's geometric room. This is not
// wallObs, dynamic collision eligibility, a route, or clearance; missing or
// incomplete inputs remain unsupported.
SavedRoomBoundaryAudit audit_saved_room_boundary(
    const SavedCollisionCandidate&, const GeometricRoomMap&,
    const std::vector<RoomObstacleCandidateSet>&);

const char* saved_room_boundary_audit_state_name(SavedRoomBoundaryAuditState) noexcept;
const char* saved_room_boundary_audit_reason_name(SavedRoomBoundaryAuditReason) noexcept;

} // namespace odh
