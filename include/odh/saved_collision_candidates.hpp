#pragma once

#include "odh/collision.hpp"
#include "odh/saved_world.hpp"

namespace odh {

enum class SavedCollisionCandidateState : std::uint8_t { candidate, absent, unsupported };
enum class SavedCollisionCandidateReason : std::uint8_t {
    none,
    entity_missing,
    entity_identity_mismatch,
    position_unavailable,
    world_position_mismatch,
    collision_metadata_missing,
    invalid_shape,
    avatar_kill_timeout_unavailable,
    architectural_door_runtime_state_unavailable
};

struct SavedCollisionCandidate {
    std::int16_t object_id = 0;
    bool avatar = false;
    SavedCollisionCandidateState state = SavedCollisionCandidateState::unsupported;
    SavedCollisionCandidateReason reason = SavedCollisionCandidateReason::entity_missing;
    // Present even for some unsupported cases when geometry is computable;
    // this is a possible static shape, not proof of a registered obstacle.
    std::optional<CollisionBox> box;
    // Saved and restored positions were checked together when this candidate
    // was derived; consumers must use this position with its geometry.
    std::optional<EntityPosition> position;
};

// Derives per-entity geometry from a saved record and its restored entity.
// Missing metadata/behavior remains unsupported; no aggregate result here
// certifies route clearance, dynamic obstacles, or runtime registration.
SavedCollisionCandidate saved_collision_candidate(
    const SavedWorldEntity&, const Entity* restored_entity);

const char* saved_collision_candidate_state_name(SavedCollisionCandidateState) noexcept;
const char* saved_collision_candidate_reason_name(SavedCollisionCandidateReason) noexcept;

} // namespace odh
