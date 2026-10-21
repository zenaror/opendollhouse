#include "odh/saved_slot_goal_selection.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::GeometricRoomMap room_map() {
    odh::GeometricRoomMap map;
    map.width = map.height = 5;
    map.floor_resources_resolved = true;
    map.rooms.resize(4);
    map.rooms[1] = {1, 0, 24, {0, 0, 5, 5}, false, false, {}};
    map.rooms[2] = {2, 1, 25, {0, 0, 5, 5}, false, false, {}};
    map.rooms[3] = {3, 0, 1, {0, 0, 1, 1}, false, false, {}};
    map.levels[0].assign(25, 0x00010001U);
    map.levels[0][0] = 0x00030003U;
    map.levels[1].assign(25, 0x00020002U);
    return map;
}
odh::SlotTable slot_table() {
    odh::SlotRecord slot{};
    slot.type = 3;
    slot.standing = 1;
    slot.sitting = slot.ground = 0;
    slot.snap_target_slot = -1;
    return {0, 10, {slot}, 0};
}
odh::SavedCollisionCandidate candidate(std::int16_t id, bool avatar,
                                       odh::CollisionBox box = {37, 37, 43, 43},
                                       odh::EntityPosition pos = {40, 40, 1, 0}) {
    return {id, avatar, odh::SavedCollisionCandidateState::candidate,
            odh::SavedCollisionCandidateReason::none, box, pos};
}
}

int main() {
    try {
        using State = odh::SavedSlotGoalSelectionState;
        using Reason = odh::SavedSlotGoalSelectionReason;
        const auto map = room_map();
        const auto slots = slot_table();
        const odh::EntityPosition goal{40, 40, 1, 0};
        const auto actor = candidate(1, true);
        const auto assess = [&](const std::vector<odh::SavedCollisionCandidate>& entities,
                                const std::vector<odh::SavedMultitileGroup>& groups = {}) {
            return odh::assess_saved_slot_goal_selection(slots, 0, map, 1, goal,
                                                          entities, groups, true);
        };

        auto result = assess({actor});
        require(result.state == State::snapshot_clear && result.snapshot_eligible() == true &&
                    !result.blocker, "caller is excluded from its destination footprint test");
        auto object = candidate(2, false, {43, 37, 49, 43});
        result = assess({actor, object});
        require(result.state == State::snapshot_clear,
                "strictly touching avatar/object edges do not overlap");
        object.box->x1 = 42;
        result = assess({actor, object});
        require(result.state == State::object_overlap_rejected && result.snapshot_eligible() == false &&
                    result.object_overlaps == 1 && result.blocker == 2,
                "sitting=0 rejects an overlapping solid object in the snapshot model");

        const auto avatar = candidate(3, true);
        result = assess({actor, avatar});
        require(result.state == State::avatar_overlap_score_penalty && result.snapshot_eligible() == true &&
                    result.avatar_overlaps == 1,
                "known solid avatar retains a destination with a score penalty");
        result = assess({avatar, object, actor});
        require(result.state == State::object_overlap_rejected && result.object_overlaps == 1 &&
                    result.avatar_overlaps == 1 && result.blocker == 2,
                "an object overlap rejects a candidate also intersecting an avatar");

        auto unresolved_avatar = avatar;
        unresolved_avatar.state = odh::SavedCollisionCandidateState::unsupported;
        unresolved_avatar.reason = odh::SavedCollisionCandidateReason::avatar_kill_timeout_unavailable;
        result = assess({actor, unresolved_avatar});
        require(result.state == State::unsupported && !result.snapshot_eligible() &&
                    result.reason == Reason::entity_collision_unresolved,
                "uncertain avatar registration cannot be called retained with a penalty");
        unresolved_avatar.state = odh::SavedCollisionCandidateState::candidate;
        require(assess({actor, unresolved_avatar}).reason == Reason::entity_collision_unresolved,
                "an unresolved reason cannot be hidden behind a candidate state");
        unresolved_avatar.state = odh::SavedCollisionCandidateState::unsupported;
        unresolved_avatar.position = odh::EntityPosition{70, 70, 1, 0};
        unresolved_avatar.box = odh::CollisionBox{67, 67, 73, 73};
        require(assess({actor, unresolved_avatar}).state == State::snapshot_clear,
                "a known nonintersecting potential avatar box does not affect this snapshot goal");

        auto unknown = object;
        unknown.box.reset();
        result = assess({actor, unknown});
        require(result.state == State::unsupported && result.reason == Reason::entity_geometry_unknown &&
                    !result.snapshot_eligible(), "same-room missing geometry fails closed");
        unknown.position = odh::EntityPosition{8, 8, 1, 0};
        require(assess({actor, unknown}).state == State::snapshot_clear,
                "known opposite-room entity is outside the room-local query");
        unknown.position = odh::EntityPosition{40, 40, 2, 0};
        require(assess({actor, unknown}).state == State::snapshot_clear,
                "known opposite-level entity is outside the room-local query");
        unknown.position.reset();
        require(assess({actor, unknown}).reason == Reason::room_identity_unknown,
                "missing entity position cannot be discarded as another room");
        unknown.state = odh::SavedCollisionCandidateState::absent;
        require(assess({actor, unknown}).state == State::snapshot_clear,
                "known absent or contained geometry contributes no persistent footprint");
        unknown.state = odh::SavedCollisionCandidateState::unsupported;
        unknown.reason = odh::SavedCollisionCandidateReason::position_unavailable;
        require(assess({actor, unknown}).state == State::snapshot_clear,
                "validated saved out-of-world sentinel is outside the room-local query");

        auto door = candidate(4, false, {45, 45, 49, 49}, {47, 47, 1, 0});
        door.state = odh::SavedCollisionCandidateState::unsupported;
        door.reason = odh::SavedCollisionCandidateReason::architectural_door_runtime_state_unavailable;
        result = assess({actor, door});
        require(result.state == State::unsupported && result.reason == Reason::architectural_door_unmodeled,
                "door query at the destination tile is unsupported even without footprint overlap");
        door.position = odh::EntityPosition{70, 70, 1, 0};
        door.box = odh::CollisionBox{67, 67, 73, 73};
        require(assess({actor, door}).state == State::snapshot_clear,
                "nonintersecting door on a distinct tile does not affect snapshot geometry");

        const auto member = candidate(5, false, {67, 67, 73, 73}, {70, 70, 1, 0});
        result = assess({actor, object, member}, {{2, {}, {2, 5}}});
        require(result.state == State::unsupported && result.reason == Reason::multitile_unmodeled,
                "intersecting multipart identity is not treated as a single resolved blocker");
        result = assess({actor, object, member}, {{1, {}, {1, 5}}});
        require(result.state == State::unsupported && result.reason == Reason::multitile_unmodeled,
                "multipart caller exclusion requires the unmodeled group policy");
        result = assess({actor, member}, {{5, {}, {5}}});
        require(result.state == State::snapshot_clear,
                "a validated single-member group does not imply multipart uncertainty");
        require(assess({actor, object}, {{2, {}, {2, 2}}}).reason == Reason::entity_identity_invalid,
                "duplicate group membership fails identity validation");
        require(assess({actor, object}, {{5, {}, {2}}}).reason == Reason::entity_identity_invalid,
                "group leader must belong to its supplied group");

        require(assess({actor, actor}).reason == Reason::entity_identity_invalid,
                "duplicate saved IDs fail closed before caller exclusion");
        require(assess({object}).reason == Reason::actor_unavailable,
                "caller must be present in the complete snapshot inventory");
        require(odh::assess_saved_slot_goal_selection(slots, 0, map, 1, goal, {actor}, {}, false).reason ==
                    Reason::incomplete_inventory, "incomplete inventory cannot certify a snapshot goal");
        require(odh::assess_saved_slot_goal_selection(slots, 0, map, 1, goal, {actor, object}, {}, true, 1).reason ==
                    Reason::inventory_over_budget, "entity budget is applied before traversal");
        require(odh::assess_saved_slot_goal_selection(slots, 0, map, 1, {-1, 40, 1, 0}, {actor}, {}, true).reason ==
                    Reason::candidate_shape_invalid, "invalid destination shape is unsupported");
        require(odh::assess_saved_slot_goal_selection(slots, 0, map, 1, {8, 8, 1, 0}, {actor}, {}, true).reason ==
                    Reason::cross_room_unmodeled, "cross-room candidate remains outside this local model");
        auto unresolved_map = map;
        unresolved_map.floor_resources_resolved = false;
        require(odh::assess_saved_slot_goal_selection(slots, 0, unresolved_map, 1, goal, {actor}, {}, true).reason ==
                    Reason::room_identity_unknown, "room identity requires resolved floor resources");
        for (int variant = 0; variant < 4; ++variant) {
            auto unsupported_slots = slots;
            if (variant == 0) unsupported_slots.version = 9;
            if (variant == 1) unsupported_slots.records[0].type = 0;
            if (variant == 2) unsupported_slots.records[0].sitting = 1;
            if (variant == 3) unsupported_slots.records[0].snap_target_slot = 0;
            require(odh::assess_saved_slot_goal_selection(unsupported_slots, 0, map, 1, goal, {actor}, {}, true).reason ==
                        Reason::slot_profile_unimplemented,
                    "unverified slot version/type/sitting/snap profile is unsupported");
        }
        std::cout << checks << " saved SLOT goal selection checks passed (snapshot only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Saved SLOT goal selection test failed: " << error.what() << '\n';
        return 1;
    }
}
