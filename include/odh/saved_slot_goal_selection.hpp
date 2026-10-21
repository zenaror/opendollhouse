#pragma once

#include "odh/formats.hpp"
#include "odh/room_map.hpp"
#include "odh/saved_collision_candidates.hpp"

namespace odh {

enum class SavedSlotGoalSelectionState : std::uint8_t {
    snapshot_clear,
    object_overlap_rejected,
    avatar_overlap_score_penalty,
    unsupported
};
enum class SavedSlotGoalSelectionReason : std::uint8_t {
    none,
    slot_profile_unimplemented,
    incomplete_inventory,
    inventory_over_budget,
    entity_identity_invalid,
    actor_unavailable,
    room_identity_unknown,
    cross_room_unmodeled,
    candidate_shape_invalid,
    entity_geometry_unknown,
    entity_collision_unresolved,
    architectural_door_unmodeled,
    multitile_unmodeled
};
struct SavedSlotGoalSelection {
    SavedSlotGoalSelectionState state = SavedSlotGoalSelectionState::unsupported;
    SavedSlotGoalSelectionReason reason = SavedSlotGoalSelectionReason::slot_profile_unimplemented;
    // Lowest overlapping object/avatar ID for known classifications, or the
    // entity that made the snapshot unsupported; not PositionValid's blocker.
    std::optional<std::int16_t> blocker;
    std::size_t object_overlaps = 0;
    std::size_t avatar_overlaps = 0;
    // Eligibility within the conservative solid-footprint snapshot only.
    // Absence means the snapshot classification itself is unsupported.
    std::optional<bool> snapshot_eligible() const noexcept;
};

// Models only v10 type-3 standing=1/sitting=0, unsnapped SLOT selection over
// saved candidate footprints. Simitone's SlotParser rejects a solid object
// for this profile but retains a solid avatar with a score penalty. Present
// boxes are treated conservatively as solid; this does not execute flags,
// EntryPoint 5, dynamic registrations, door queries, or routing. Intersecting
// unresolved candidates and multitile members remain unsupported. Complete
// means the entity and saved-group inventories are both complete snapshots.
// No result establishes runtime PositionValid or route clearance.
SavedSlotGoalSelection assess_saved_slot_goal_selection(
    const SlotTable&, std::size_t type3_index,
    const GeometricRoomMap&, std::int16_t actor_id, const EntityPosition& goal,
    const std::vector<SavedCollisionCandidate>&,
    const std::vector<SavedMultitileGroup>&,
    bool inventory_complete, std::size_t entity_limit = 4096);

const char* saved_slot_goal_selection_state_name(SavedSlotGoalSelectionState) noexcept;
const char* saved_slot_goal_selection_reason_name(SavedSlotGoalSelectionReason) noexcept;

} // namespace odh
