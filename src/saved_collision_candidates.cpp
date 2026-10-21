#include "odh/saved_collision_candidates.hpp"

namespace odh {
namespace {
constexpr std::uint16_t has_zero_extent = 1U << 2;
constexpr std::uint16_t architectural_door = 1U << 15;

SavedCollisionCandidate unsupported(const SavedWorldEntity& record,
                                   SavedCollisionCandidateReason reason,
                                   std::optional<CollisionBox> box = std::nullopt,
                                   std::optional<EntityPosition> position = std::nullopt) {
    return {static_cast<std::int16_t>(record.saved.object_id),
            record.tail.person.has_value(), SavedCollisionCandidateState::unsupported,
            reason, box, position};
}

SavedCollisionCandidate candidate(const SavedWorldEntity& record, bool avatar,
                                  CollisionBox box, EntityPosition position) {
    return {static_cast<std::int16_t>(record.saved.object_id), avatar,
            SavedCollisionCandidateState::candidate, SavedCollisionCandidateReason::none,
            box, position};
}
}

SavedCollisionCandidate saved_collision_candidate(
    const SavedWorldEntity& record, const Entity* entity) {
    const bool avatar = record.tail.person.has_value();
    if (!entity) return unsupported(record, SavedCollisionCandidateReason::entity_missing);
    if (record.saved.object_id != entity->id ||
        entity->kind != (avatar ? EntityKind::avatar : EntityKind::object) ||
        !entity->definition || entity->definition->guid != record.saved.guid)
        return unsupported(record, SavedCollisionCandidateReason::entity_identity_mismatch);

    if (entity->attachment)
        return {static_cast<std::int16_t>(record.saved.object_id), avatar,
                SavedCollisionCandidateState::absent, SavedCollisionCandidateReason::none,
                std::nullopt, std::nullopt};

    const auto& saved = record.saved.prefix;
    if (saved.x == -16 && saved.y == -16)
        return unsupported(record, SavedCollisionCandidateReason::position_unavailable);
    const EntityPosition position{saved.x, saved.y, static_cast<std::int16_t>(saved.level),
                                  static_cast<std::uint8_t>(saved.object_data[1])};
    if (!entity->position || entity->position->x != position.x ||
        entity->position->y != position.y || entity->position->level != position.level ||
        entity->position->direction != position.direction)
        return unsupported(record, SavedCollisionCandidateReason::world_position_mismatch);

    const auto flags = static_cast<std::uint16_t>(saved.object_data[8]);
    const bool is_architectural_door =
        (static_cast<std::uint16_t>(saved.object_data[40]) & architectural_door) != 0;
    if (avatar) {
        const auto shape = avatar_candidate_collision_shape(position);
        if (shape.state != CollisionShapeState::present)
            return unsupported(record, SavedCollisionCandidateReason::invalid_shape);
        if ((flags & has_zero_extent) == 0)
            return unsupported(record, SavedCollisionCandidateReason::avatar_kill_timeout_unavailable,
                               shape.box, position);
        return candidate(record, true, shape.box, position);
    }

    if ((flags & has_zero_extent) && !is_architectural_door)
        return {static_cast<std::int16_t>(record.saved.object_id), false,
                SavedCollisionCandidateState::absent, SavedCollisionCandidateReason::none,
                std::nullopt, position};

    if ((flags & has_zero_extent) && is_architectural_door)
        return unsupported(record,
            SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable);

    const auto shape = object_collision_shape(record.definition, position, flags, false);
    if (shape.state != CollisionShapeState::present) {
        const auto reason = !record.definition.tile_width || !record.definition.footprint_mask
            ? SavedCollisionCandidateReason::collision_metadata_missing
            : SavedCollisionCandidateReason::invalid_shape;
        return unsupported(record, reason);
    }

    if (is_architectural_door)
        return unsupported(record,
            SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable, shape.box, position);
    return candidate(record, false, shape.box, position);
}

const char* saved_collision_candidate_state_name(SavedCollisionCandidateState state) noexcept {
    switch (state) {
    case SavedCollisionCandidateState::candidate: return "candidate";
    case SavedCollisionCandidateState::absent: return "absent";
    case SavedCollisionCandidateState::unsupported: return "unsupported";
    }
    return "unsupported";
}

const char* saved_collision_candidate_reason_name(SavedCollisionCandidateReason reason) noexcept {
    switch (reason) {
    case SavedCollisionCandidateReason::none: return "none";
    case SavedCollisionCandidateReason::entity_missing: return "entity_missing";
    case SavedCollisionCandidateReason::entity_identity_mismatch: return "entity_identity_mismatch";
    case SavedCollisionCandidateReason::position_unavailable: return "position_unavailable";
    case SavedCollisionCandidateReason::world_position_mismatch: return "world_position_mismatch";
    case SavedCollisionCandidateReason::collision_metadata_missing: return "collision_metadata_missing";
    case SavedCollisionCandidateReason::invalid_shape: return "invalid_shape";
    case SavedCollisionCandidateReason::avatar_kill_timeout_unavailable: return "avatar_kill_timeout_unavailable";
    case SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable:
        return "architectural_door_runtime_state_unavailable";
    }
    return "invalid_reason";
}

} // namespace odh
