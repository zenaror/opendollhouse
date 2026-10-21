#include "odh/saved_avatar_place_discovery.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::size_t checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

odh::SavedCollisionCandidate entity(std::int16_t id, bool avatar,
                                     odh::CollisionBox box,
                                     odh::EntityPosition position) {
    return {id, avatar, odh::SavedCollisionCandidateState::candidate,
            odh::SavedCollisionCandidateReason::none, box, position};
}

odh::SavedAvatarPlaceDiscovery discover(
    const std::vector<odh::SavedCollisionCandidate>& entities,
    const std::vector<odh::SavedMultitileGroup>& groups = {},
    bool complete = true, bool groups_complete = true, std::size_t limit = 4096) {
    return odh::discover_saved_avatar_place_blockers(
        {16, 16, 1, 0}, 5, entities, groups, complete, groups_complete, limit);
}
} // namespace

int main() {
    try {
        const auto actor = entity(5, true, {13, 13, 19, 19}, {16, 16, 1, 0});
        const auto clear = discover({actor, entity(8, false, {20, 20, 30, 30}, {25, 25, 1, 0})});
        require(clear.status == odh::SavedAvatarPlaceDiscoveryStatus::ready &&
                    clear.reason == odh::SavedAvatarPlaceDiscoveryReason::none,
                "complete entity snapshot is discovered");
        require(clear.entity_snapshot_blockers.empty() &&
                    clear.entity_snapshot_rule_audit.status == odh::SavedAvatarPlaceAuditStatus::allowed,
                "no saved entity footprint overlap passes only the snapshot audit");
        require(!clear.runtime_obstacle_inventory_complete,
                "entity snapshot does not claim runtime obstacle completeness");

        const auto ordered = discover({actor,
            entity(9, false, {17, 14, 20, 18}, {18, 16, 1, 0}),
            entity(2, true, {12, 12, 14, 14}, {13, 13, 1, 0})});
        require(ordered.status == odh::SavedAvatarPlaceDiscoveryStatus::ready &&
                    ordered.entity_snapshot_blockers.size() == 2,
                "both intersecting saved entity footprints are discovered");
        require(ordered.entity_snapshot_blockers[0].blocker_id == 2 &&
                    ordered.entity_snapshot_blockers[1].blocker_id == 9,
                "discovered blockers are ordered by ObjectID");
        require(ordered.entity_snapshot_rule_audit.status == odh::SavedAvatarPlaceAuditStatus::unsupported,
                "unknown BHAV overlap rules remain unsupported");

        const auto same_group = discover({actor,
            entity(7, false, {14, 14, 18, 18}, {16, 16, 1, 0})},
            {{5, std::nullopt, {5, 7}}});
        require(same_group.status == odh::SavedAvatarPlaceDiscoveryStatus::ready &&
                    same_group.entity_snapshot_blockers.size() == 1 &&
                    same_group.entity_snapshot_blockers[0].overlap.same_multitile_group,
                "multitile self-group relationship is preserved");
        require(same_group.entity_snapshot_rule_audit.status == odh::SavedAvatarPlaceAuditStatus::allowed,
                "the reference audit ignores same-group overlap");

        auto unresolved = entity(7, false, {14, 14, 18, 18}, {16, 16, 1, 0});
        unresolved.state = odh::SavedCollisionCandidateState::unsupported;
        unresolved.reason = odh::SavedCollisionCandidateReason::collision_metadata_missing;
        const auto uncertain_overlap = discover({actor, unresolved});
        require(uncertain_overlap.status == odh::SavedAvatarPlaceDiscoveryStatus::ready &&
                    uncertain_overlap.entity_snapshot_rule_audit.status ==
                        odh::SavedAvatarPlaceAuditStatus::unsupported,
                "a geometrically possible unresolved entity cannot become free space");

        unresolved.box.reset();
        const auto unknown_geometry = discover({actor, unresolved});
        require(unknown_geometry.status == odh::SavedAvatarPlaceDiscoveryStatus::unsupported &&
                    unknown_geometry.reason == odh::SavedAvatarPlaceDiscoveryReason::entity_geometry_unknown,
                "unknown geometry on the candidate level fails closed");

        auto unknown_position = entity(7, false, {14, 14, 18, 18}, {16, 16, 1, 0});
        unknown_position.position.reset();
        const auto missing_position = discover({actor, unknown_position});
        require(missing_position.status == odh::SavedAvatarPlaceDiscoveryStatus::unsupported &&
                    missing_position.reason == odh::SavedAvatarPlaceDiscoveryReason::entity_position_unknown,
                "missing entity position could hide a blocker");
        auto paired_sentinel = unknown_position;
        paired_sentinel.state = odh::SavedCollisionCandidateState::unsupported;
        paired_sentinel.reason = odh::SavedCollisionCandidateReason::position_unavailable;
        paired_sentinel.box.reset();
        const auto outside = discover({actor, paired_sentinel});
        require(outside.status == odh::SavedAvatarPlaceDiscoveryStatus::ready &&
                    outside.entity_snapshot_blockers.empty(),
                "the paired out-of-world sentinel cannot overlap the candidate");

        require(discover({actor}, {}, false).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::incomplete_inventory,
                "incomplete entity snapshot fails closed");
        require(discover({actor}, {}, true, false).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::incomplete_inventory,
                "incomplete multitile inventory fails closed");
        require(discover({actor, actor}).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::entity_identity_invalid,
                "duplicate entity IDs fail closed");
        require(discover({actor, entity(8, false, {20, 20, 30, 30}, {25, 25, 1, 0})}, {}, true, true, 1).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::inventory_over_budget,
                "entity discovery enforces the caller budget");
        require(discover({actor}, {}, true, true, 4097).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::inventory_over_budget,
                "entity discovery never accepts a budget above 4096");
        require(discover({actor, entity(7, false, {14, 14, 18, 18}, {16, 16, 1, 0})},
                         {{5, std::nullopt, {5, 7}}, {7, std::nullopt, {7}}}).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::multitile_inventory_invalid,
                "duplicate multitile membership fails closed");
        require(odh::discover_saved_avatar_place_blockers({16,16,1,0}, 0, {actor}, {}, true, true).reason ==
                    odh::SavedAvatarPlaceDiscoveryReason::actor_identity_invalid,
                "invalid actor identity fails closed");
        require(checks == 19, "all discovery assertions were counted");
        std::cout << "saved avatar place discovery: " << checks << " checks passed (entity snapshot only)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
