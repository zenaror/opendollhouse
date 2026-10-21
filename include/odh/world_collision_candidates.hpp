#pragma once

#include "odh/collision.hpp"
#include "odh/definition_index.hpp"
#include "odh/world_room_info.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

enum class WorldCollisionCandidatesState : std::uint8_t { complete, unsupported };
enum class WorldCollisionCandidatesReason : std::uint8_t {
    none, inventory_incomplete, inventory_over_budget
};
enum class WorldCollisionCandidateState : std::uint8_t { candidate, absent };

struct WorldCollisionCandidate {
    std::int16_t entity_id = 0;
    EntityKind kind = EntityKind::object;
    WorldCollisionCandidateState state = WorldCollisionCandidateState::absent;
    std::optional<CollisionBox> box;
    std::optional<EntityPosition> position;
    bool static_footprint = true;
};

struct WorldCollisionCandidates {
    WorldCollisionCandidatesState state = WorldCollisionCandidatesState::unsupported;
    WorldCollisionCandidatesReason reason = WorldCollisionCandidatesReason::inventory_incomplete;
    std::vector<WorldCollisionCandidate> entities;
};

// Enumerates a bounded live World snapshot into per-entity geometry. This is
// candidate data only: it does not prove RoomInfo registration or route clearance.
WorldCollisionCandidates world_collision_candidates(
    const World&, const DefinitionIndex&, const AvatarKillTimeoutResolver&,
    std::size_t entity_limit = 4096);

const char* world_collision_candidates_state_name(WorldCollisionCandidatesState) noexcept;
const char* world_collision_candidates_reason_name(WorldCollisionCandidatesReason) noexcept;
const char* world_collision_candidate_state_name(WorldCollisionCandidateState) noexcept;

} // namespace odh
