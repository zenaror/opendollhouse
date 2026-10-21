#pragma once

#include "odh/saved_avatar_place_audit.hpp"
#include "odh/saved_collision_candidates.hpp"
#include "odh/room_map.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

enum class SavedAvatarPlaceDiscoveryStatus : std::uint8_t { ready, unsupported };
enum class SavedAvatarPlaceDiscoveryReason : std::uint8_t {
    none,
    incomplete_inventory,
    inventory_over_budget,
    actor_identity_invalid,
    candidate_shape_invalid,
    entity_identity_invalid,
    multitile_inventory_invalid,
    entity_position_unknown,
    candidate_room_unknown,
    entity_room_unknown,
    entity_geometry_unknown,
};

struct SavedAvatarPlaceDiscovery {
    SavedAvatarPlaceDiscoveryStatus status = SavedAvatarPlaceDiscoveryStatus::unsupported;
    SavedAvatarPlaceDiscoveryReason reason = SavedAvatarPlaceDiscoveryReason::incomplete_inventory;
    std::vector<SavedAvatarPlaceBlockerCandidate> entity_snapshot_blockers;
    SavedAvatarPlaceAuditResult entity_snapshot_rule_audit;
    std::size_t entities_examined = 0;
    // This discovery uses saved entity candidates only. It never represents
    // RoomInfo static/dynamic obstacle registration or complete GetAvatarPlace.
    bool runtime_obstacle_inventory_complete = false;
};

// Finds saved entity footprints intersecting a proposed avatar footprint in
// the candidate's geometric room. Prepared floor aliases are required.
// Unknown BHAV, flags, and EP26 values stay unknown in each overlap input;
// runtime_obstacle_inventory_complete is always false.
SavedAvatarPlaceDiscovery discover_saved_avatar_place_blockers(
    const GeometricRoomMap& rooms,
    const EntityPosition& candidate_position, std::int16_t actor_id,
    const std::vector<SavedCollisionCandidate>& entities,
    const std::vector<SavedMultitileGroup>& groups,
    bool entity_inventory_complete, bool multitile_inventory_complete,
    std::size_t entity_limit = 4096);

const char* saved_avatar_place_discovery_status_name(SavedAvatarPlaceDiscoveryStatus) noexcept;
const char* saved_avatar_place_discovery_reason_name(SavedAvatarPlaceDiscoveryReason) noexcept;

} // namespace odh
