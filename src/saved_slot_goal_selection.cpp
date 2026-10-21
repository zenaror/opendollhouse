#include "odh/saved_slot_goal_selection.hpp"

#include <bitset>
#include <limits>

namespace odh {
namespace {
SavedSlotGoalSelection unsupported(SavedSlotGoalSelectionReason reason,
                                  std::optional<std::int16_t> entity = {}) {
    SavedSlotGoalSelection result;
    result.reason = reason;
    result.blocker = entity;
    return result;
}
bool valid_box(const CollisionBox& box) {
    return box.x1 < box.x2 && box.y1 < box.y2;
}
void lowest_id(std::optional<std::int16_t>& current, std::int16_t id) {
    if (!current || id < *current) current = id;
}
}

std::optional<bool> SavedSlotGoalSelection::snapshot_eligible() const noexcept {
    switch (state) {
    case SavedSlotGoalSelectionState::snapshot_clear:
    case SavedSlotGoalSelectionState::avatar_overlap_score_penalty: return true;
    case SavedSlotGoalSelectionState::object_overlap_rejected: return false;
    case SavedSlotGoalSelectionState::unsupported: return std::nullopt;
    }
    return std::nullopt;
}

SavedSlotGoalSelection assess_saved_slot_goal_selection(
    const SlotTable& slots, std::size_t type3_index,
    const GeometricRoomMap& rooms, std::int16_t actor_id, const EntityPosition& goal,
    const std::vector<SavedCollisionCandidate>& entities,
    const std::vector<SavedMultitileGroup>& groups,
    bool inventory_complete, std::size_t entity_limit) {
    const auto* slot = slot_of_type(slots, 3, type3_index);
    if (slots.version != 10 || !slot || slot->standing != 1 || slot->sitting != 0 ||
        slot->ground != 0 || slot->snap_target_slot != -1)
        return unsupported(SavedSlotGoalSelectionReason::slot_profile_unimplemented);
    if (!inventory_complete)
        return unsupported(SavedSlotGoalSelectionReason::incomplete_inventory);
    if (entities.size() > entity_limit || groups.size() > entity_limit)
        return unsupported(SavedSlotGoalSelectionReason::inventory_over_budget);
    if (actor_id <= 0)
        return unsupported(SavedSlotGoalSelectionReason::actor_unavailable);
    const auto shape = avatar_candidate_collision_shape(goal);
    if (shape.state != CollisionShapeState::present)
        return unsupported(SavedSlotGoalSelectionReason::candidate_shape_invalid);
    const auto room = geometric_room_at(rooms, goal.x, goal.y, goal.level);
    if (!rooms.floor_resources_resolved || !room || *room == 0)
        return unsupported(SavedSlotGoalSelectionReason::room_identity_unknown);

    // Three fixed 4 KiB membership sets (12 KiB total) bound auxiliary
    // storage independently of vector capacity and avoid group searches per box.
    constexpr auto id_count = std::size_t(std::numeric_limits<std::int16_t>::max()) + 1;
    std::bitset<id_count> seen, grouped, multipart;
    const SavedCollisionCandidate* actor = nullptr;
    for (const auto& entity : entities) {
        if (entity.object_id <= 0 || seen.test(static_cast<std::size_t>(entity.object_id)))
            return unsupported(SavedSlotGoalSelectionReason::entity_identity_invalid);
        seen.set(static_cast<std::size_t>(entity.object_id));
        if (entity.object_id == actor_id) actor = &entity;
    }
    if (!actor || !actor->avatar || !actor->position)
        return unsupported(SavedSlotGoalSelectionReason::actor_unavailable);
    const auto actor_room = geometric_room_at(rooms, actor->position->x,
                                              actor->position->y, actor->position->level);
    if (!actor_room)
        return unsupported(SavedSlotGoalSelectionReason::room_identity_unknown);
    if (*actor_room != *room || actor->position->level != goal.level)
        return unsupported(SavedSlotGoalSelectionReason::cross_room_unmodeled);
    std::size_t group_members = 0;
    for (const auto& group : groups) {
        if (group.members.empty() || group.members.size() > entity_limit - group_members)
            return unsupported(SavedSlotGoalSelectionReason::inventory_over_budget);
        group_members += group.members.size();
        bool leader_found = false;
        for (const auto id : group.members) {
            if (id <= 0 || !seen.test(static_cast<std::size_t>(id)) ||
                grouped.test(static_cast<std::size_t>(id)))
                return unsupported(SavedSlotGoalSelectionReason::entity_identity_invalid);
            grouped.set(static_cast<std::size_t>(id));
            if (group.members.size() > 1) multipart.set(static_cast<std::size_t>(id));
            leader_found = leader_found || id == group.leader;
        }
        if (!leader_found)
            return unsupported(SavedSlotGoalSelectionReason::entity_identity_invalid);
    }
    if (multipart.test(static_cast<std::size_t>(actor_id)))
        return unsupported(SavedSlotGoalSelectionReason::multitile_unmodeled, actor_id);

    SavedSlotGoalSelection result;
    result.state = SavedSlotGoalSelectionState::snapshot_clear;
    result.reason = SavedSlotGoalSelectionReason::none;
    std::optional<std::int16_t> object_blocker, avatar_blocker;
    for (const auto& entity : entities) {
        if (entity.object_id == actor_id || entity.state == SavedCollisionCandidateState::absent)
            continue;
        // The paired saved sentinel is outside the room-local entity set.
        if (entity.state == SavedCollisionCandidateState::unsupported &&
            entity.reason == SavedCollisionCandidateReason::position_unavailable) continue;
        if (!entity.position)
            return unsupported(SavedSlotGoalSelectionReason::room_identity_unknown, entity.object_id);
        const auto entity_room = geometric_room_at(rooms, entity.position->x,
                                                   entity.position->y, entity.position->level);
        if (!entity_room)
            return unsupported(SavedSlotGoalSelectionReason::room_identity_unknown, entity.object_id);
        if (*entity_room != *room || entity.position->level != goal.level) continue;
        if (!entity.box || !valid_box(*entity.box))
            return unsupported(SavedSlotGoalSelectionReason::entity_geometry_unknown, entity.object_id);
        const bool intersects = collision_boxes_intersect(shape.box, *entity.box);
        const bool door_tile = entity.reason == SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable &&
            entity.position->x / 16 == goal.x / 16 && entity.position->y / 16 == goal.y / 16;
        if (!intersects && !door_tile) continue;
        if (entity.reason == SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable)
            return unsupported(SavedSlotGoalSelectionReason::architectural_door_unmodeled, entity.object_id);
        if (multipart.test(static_cast<std::size_t>(entity.object_id)))
            return unsupported(SavedSlotGoalSelectionReason::multitile_unmodeled, entity.object_id);
        if (entity.state != SavedCollisionCandidateState::candidate ||
            entity.reason != SavedCollisionCandidateReason::none)
            return unsupported(SavedSlotGoalSelectionReason::entity_collision_unresolved, entity.object_id);
        if (entity.avatar) {
            ++result.avatar_overlaps;
            lowest_id(avatar_blocker, entity.object_id);
        } else {
            ++result.object_overlaps;
            lowest_id(object_blocker, entity.object_id);
        }
    }
    if (object_blocker) {
        result.state = SavedSlotGoalSelectionState::object_overlap_rejected;
        result.blocker = object_blocker;
    } else if (avatar_blocker) {
        result.state = SavedSlotGoalSelectionState::avatar_overlap_score_penalty;
        result.blocker = avatar_blocker;
    }
    return result;
}

const char* saved_slot_goal_selection_state_name(SavedSlotGoalSelectionState state) noexcept {
    switch (state) {
    case SavedSlotGoalSelectionState::snapshot_clear: return "snapshot_clear";
    case SavedSlotGoalSelectionState::object_overlap_rejected: return "object_overlap_rejected";
    case SavedSlotGoalSelectionState::avatar_overlap_score_penalty: return "avatar_overlap_score_penalty";
    case SavedSlotGoalSelectionState::unsupported: return "unsupported";
    }
    return "invalid_state";
}
const char* saved_slot_goal_selection_reason_name(SavedSlotGoalSelectionReason reason) noexcept {
    switch (reason) {
    case SavedSlotGoalSelectionReason::none: return "none";
    case SavedSlotGoalSelectionReason::slot_profile_unimplemented: return "slot_profile_unimplemented";
    case SavedSlotGoalSelectionReason::incomplete_inventory: return "incomplete_inventory";
    case SavedSlotGoalSelectionReason::inventory_over_budget: return "inventory_over_budget";
    case SavedSlotGoalSelectionReason::entity_identity_invalid: return "entity_identity_invalid";
    case SavedSlotGoalSelectionReason::actor_unavailable: return "actor_unavailable";
    case SavedSlotGoalSelectionReason::room_identity_unknown: return "room_identity_unknown";
    case SavedSlotGoalSelectionReason::cross_room_unmodeled: return "cross_room_unmodeled";
    case SavedSlotGoalSelectionReason::candidate_shape_invalid: return "candidate_shape_invalid";
    case SavedSlotGoalSelectionReason::entity_geometry_unknown: return "entity_geometry_unknown";
    case SavedSlotGoalSelectionReason::entity_collision_unresolved: return "entity_collision_unresolved";
    case SavedSlotGoalSelectionReason::architectural_door_unmodeled: return "architectural_door_unmodeled";
    case SavedSlotGoalSelectionReason::multitile_unmodeled: return "multitile_unmodeled";
    }
    return "invalid_reason";
}
} // namespace odh
