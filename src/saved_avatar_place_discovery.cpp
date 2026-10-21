#include "odh/saved_avatar_place_discovery.hpp"

#include "odh/collision.hpp"

#include <algorithm>
#include <bitset>
#include <limits>
#include <optional>
#include <utility>

namespace odh {
namespace {
constexpr std::size_t id_count =
    static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()) + 1;

SavedAvatarPlaceDiscovery unsupported(SavedAvatarPlaceDiscoveryReason reason,
                                      std::size_t examined = 0) {
    SavedAvatarPlaceDiscovery result;
    result.reason = reason;
    result.entities_examined = examined;
    return result;
}

bool valid_box(const CollisionBox& box) noexcept {
    return box.x1 < box.x2 && box.y1 < box.y2;
}
} // namespace

SavedAvatarPlaceDiscovery discover_saved_avatar_place_blockers(
    const GeometricRoomMap& rooms, const EntityPosition& candidate_position,
    std::int16_t actor_id,
    const std::vector<SavedCollisionCandidate>& entities,
    const std::vector<SavedMultitileGroup>& groups,
    bool entity_inventory_complete, bool multitile_inventory_complete,
    std::size_t entity_limit) {
    if (!entity_inventory_complete || !multitile_inventory_complete)
        return unsupported(SavedAvatarPlaceDiscoveryReason::incomplete_inventory);
    if (entity_limit == 0 || entity_limit > 4096 || entities.size() > entity_limit ||
        groups.size() > entity_limit)
        return unsupported(SavedAvatarPlaceDiscoveryReason::inventory_over_budget);
    if (actor_id <= 0)
        return unsupported(SavedAvatarPlaceDiscoveryReason::actor_identity_invalid);

    const auto footprint = avatar_candidate_collision_shape(candidate_position);
    if (footprint.state != CollisionShapeState::present)
        return unsupported(SavedAvatarPlaceDiscoveryReason::candidate_shape_invalid);
    if (!rooms.floor_resources_resolved || !rooms.width || !rooms.height ||
        rooms.width > 64 || rooms.height > 64 ||
        rooms.rooms.empty() || rooms.rooms.size() > 32768 ||
        rooms.levels[0].size() != std::size_t(rooms.width) * rooms.height ||
        rooms.levels[1].size() != std::size_t(rooms.width) * rooms.height)
        return unsupported(SavedAvatarPlaceDiscoveryReason::candidate_room_unknown);

    const auto room_at = [&](const EntityPosition& position) -> std::optional<std::uint16_t> {
        if (position.level < 1 || position.level > 2) return std::uint16_t{0};
        if (position.x < 0 || position.y < 0 ||
            static_cast<std::uint32_t>(position.x / 16) >= rooms.width ||
            static_cast<std::uint32_t>(position.y / 16) >= rooms.height)
            return std::uint16_t{0};
        return geometric_room_at(rooms, position.x, position.y, position.level);
    };
    const auto candidate_room = room_at(candidate_position);
    if (!candidate_room)
        return unsupported(SavedAvatarPlaceDiscoveryReason::candidate_room_unknown);

    std::bitset<id_count> seen;
    const SavedCollisionCandidate* actor = nullptr;
    for (const auto& entity : entities) {
        if (entity.object_id <= 0 || seen.test(static_cast<std::size_t>(entity.object_id)))
            return unsupported(SavedAvatarPlaceDiscoveryReason::entity_identity_invalid);
        seen.set(static_cast<std::size_t>(entity.object_id));
        if (entity.object_id == actor_id) actor = &entity;
    }
    if (!actor || !actor->avatar)
        return unsupported(SavedAvatarPlaceDiscoveryReason::actor_identity_invalid);

    // The serialized group inventory is authoritative only within this saved
    // snapshot. Validate it before using membership to suppress self-group hits.
    std::bitset<id_count> grouped_ids;
    std::vector<std::pair<std::int16_t, std::int32_t>> group_for_id;
    group_for_id.reserve(entity_limit);
    std::size_t group_members = 0;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        const auto& group = groups[index];
        if (group.members.empty() || group.members.size() > entity_limit - group_members)
            return unsupported(SavedAvatarPlaceDiscoveryReason::inventory_over_budget);
        group_members += group.members.size();
        bool leader_found = false;
        for (const auto id : group.members) {
            if (id <= 0 || !seen.test(static_cast<std::size_t>(id)) ||
                grouped_ids.test(static_cast<std::size_t>(id)))
                return unsupported(SavedAvatarPlaceDiscoveryReason::multitile_inventory_invalid);
            grouped_ids.set(static_cast<std::size_t>(id));
            group_for_id.emplace_back(id, static_cast<std::int32_t>(index));
            leader_found = leader_found || id == group.leader;
        }
        if (!leader_found)
            return unsupported(SavedAvatarPlaceDiscoveryReason::multitile_inventory_invalid);
    }
    std::sort(group_for_id.begin(), group_for_id.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    const auto group_index = [&](std::int16_t id) {
        const auto found = std::lower_bound(group_for_id.begin(), group_for_id.end(), id,
            [](const auto& entry, std::int16_t value) { return entry.first < value; });
        return found != group_for_id.end() && found->first == id ? found->second : -1;
    };

    std::vector<SavedAvatarPlaceBlockerCandidate> blockers;
    blockers.reserve(entities.size());
    for (const auto& entity : entities) {
        if (entity.object_id == actor_id || entity.state == SavedCollisionCandidateState::absent)
            continue;
        if (!entity.position) {
            // The paired saved sentinel is explicitly outside the world; all
            // other missing positions could hide an overlapping entity.
            if (entity.state == SavedCollisionCandidateState::unsupported &&
                entity.reason == SavedCollisionCandidateReason::position_unavailable)
                continue;
            return unsupported(SavedAvatarPlaceDiscoveryReason::entity_position_unknown,
                               entities.size());
        }
        if (entity.position->level != candidate_position.level) continue;
        const auto entity_room = room_at(*entity.position);
        if (!entity_room)
            return unsupported(SavedAvatarPlaceDiscoveryReason::entity_room_unknown,
                               entities.size());
        if (*entity_room != *candidate_room) continue;
        if (!entity.box || !valid_box(*entity.box))
            return unsupported(SavedAvatarPlaceDiscoveryReason::entity_geometry_unknown,
                               entities.size());
        if (!collision_boxes_intersect(footprint.box, *entity.box)) continue;

        AvatarPlaceOverlapInput overlap;
        const auto actor_group = group_index(actor_id);
        overlap.same_multitile_group = actor_group >= 0 && actor_group == group_index(entity.object_id);
        overlap.blocker_kind = entity.avatar ? AvatarPlaceBlockerKind::avatar
                                             : AvatarPlaceBlockerKind::object;
        // EntryPoint 5, live intersection flags, ghost state and EP26 are
        // runtime/BHAV inputs; defaults intentionally preserve unknown.
        blockers.push_back({entity.object_id, overlap});
    }

    std::sort(blockers.begin(), blockers.end(), [](const auto& left, const auto& right) {
        return left.blocker_id < right.blocker_id;
    });
    SavedAvatarPlaceDiscovery result;
    result.status = SavedAvatarPlaceDiscoveryStatus::ready;
    result.reason = SavedAvatarPlaceDiscoveryReason::none;
    result.entity_snapshot_blockers = std::move(blockers);
    result.entity_snapshot_rule_audit = evaluate_saved_avatar_place_blockers(
        result.entity_snapshot_blockers, true, entity_limit);
    result.entities_examined = entities.size();
    result.runtime_obstacle_inventory_complete = false;
    return result;
}

const char* saved_avatar_place_discovery_status_name(
    SavedAvatarPlaceDiscoveryStatus status) noexcept {
    switch (status) {
    case SavedAvatarPlaceDiscoveryStatus::ready: return "ready";
    case SavedAvatarPlaceDiscoveryStatus::unsupported: return "unsupported";
    }
    return "invalid_status";
}

const char* saved_avatar_place_discovery_reason_name(
    SavedAvatarPlaceDiscoveryReason reason) noexcept {
    switch (reason) {
    case SavedAvatarPlaceDiscoveryReason::none: return "none";
    case SavedAvatarPlaceDiscoveryReason::incomplete_inventory: return "incomplete_inventory";
    case SavedAvatarPlaceDiscoveryReason::inventory_over_budget: return "inventory_over_budget";
    case SavedAvatarPlaceDiscoveryReason::actor_identity_invalid: return "actor_identity_invalid";
    case SavedAvatarPlaceDiscoveryReason::candidate_shape_invalid: return "candidate_shape_invalid";
    case SavedAvatarPlaceDiscoveryReason::entity_identity_invalid: return "entity_identity_invalid";
    case SavedAvatarPlaceDiscoveryReason::multitile_inventory_invalid: return "multitile_inventory_invalid";
    case SavedAvatarPlaceDiscoveryReason::entity_position_unknown: return "entity_position_unknown";
    case SavedAvatarPlaceDiscoveryReason::candidate_room_unknown: return "candidate_room_unknown";
    case SavedAvatarPlaceDiscoveryReason::entity_room_unknown: return "entity_room_unknown";
    case SavedAvatarPlaceDiscoveryReason::entity_geometry_unknown: return "entity_geometry_unknown";
    }
    return "invalid_reason";
}

} // namespace odh
