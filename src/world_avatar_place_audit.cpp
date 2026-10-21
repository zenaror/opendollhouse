#include "odh/world_avatar_place_audit.hpp"

#include <limits>
#include <vector>

namespace odh {
namespace {
WorldAvatarPlaceAuditResult unsupported(WorldAvatarPlaceAuditReason reason) {
    WorldAvatarPlaceAuditResult result;
    result.reason = reason;
    result.rules.status = SavedAvatarPlaceAuditStatus::unsupported;
    return result;
}

std::optional<std::int16_t> flags_for(const World& world, std::int16_t id) {
    const VmFrame frame{routine_key(4096), 0, {id, id, id, 0}, {}, {}};
    return world.read(frame, 3, 8);
}

std::optional<bool> entry_point5(const Entity& entity, std::int16_t caller,
                                std::int16_t other,
                                const WorldAvatarPlaceEntryPoint5Resolver& resolve,
                                bool& requires_bhav,
                                bool& resolver_unavailable) {
    requires_bhav = false;
    resolver_unavailable = false;
    if (!entity.entry_points) return std::nullopt;
    if (entity.entry_points->size() <= 5) return false;
    const auto action = (*entity.entry_points)[5].action;
    // Pinned Simitone returns false for action-function IDs <= 255. Larger
    // values require executing the referenced BHAV, which this adapter cannot do.
    if (action <= 255) return false;
    requires_bhav = true;
    if (resolve) {
        const auto value = resolve(caller, other);
        resolver_unavailable = !value.has_value();
        return value;
    }
    return std::nullopt;
}

WorldAvatarPlaceAuditReason ep5_failure(bool requires_bhav,
                                        bool resolver_unavailable) {
    if (!requires_bhav) return WorldAvatarPlaceAuditReason::entry_point_table_unavailable;
    return resolver_unavailable ? WorldAvatarPlaceAuditReason::entry_point5_result_unavailable
                                : WorldAvatarPlaceAuditReason::entry_point5_bhav_unimplemented;
}

std::optional<bool> has_entry_point26(const Entity& entity) {
    if (!entity.entry_points || entity.entry_points->size() <= 26)
        return std::nullopt;
    return (*entity.entry_points)[26].action != 0;
}
} // namespace

WorldAvatarPlaceAuditResult audit_world_avatar_place_overlaps(
    const World& world, std::int16_t actor_id, const RoomInfoObstacleQuery& query,
    std::optional<bool> all_avatars_solid,
    const std::function<std::optional<bool>(std::int16_t, std::int16_t)>& same_group,
    const WorldAvatarPlaceEntryPoint5Resolver& resolve_ep5) {
    if (query.status != RoomInfoObstacleIndexStatus::complete)
        return unsupported(WorldAvatarPlaceAuditReason::room_info_incomplete);
    if (actor_id <= 0)
        return unsupported(WorldAvatarPlaceAuditReason::actor_missing);

    const auto* actor = world.find(actor_id);
    if (!actor) return unsupported(WorldAvatarPlaceAuditReason::actor_missing);
    if (actor->kind != EntityKind::avatar)
        return unsupported(WorldAvatarPlaceAuditReason::actor_not_avatar);

    std::vector<SavedAvatarPlaceBlockerCandidate> blockers;
    blockers.reserve(query.intersections.size());
    std::int32_t previous_id = 0;
    for (std::size_t obstacle_index = 0;
         obstacle_index < query.intersections.size(); ++obstacle_index) {
        const auto& obstacle = query.intersections[obstacle_index];
        if (obstacle.owner_id <= 0 ||
            obstacle.owner_id > std::numeric_limits<std::int16_t>::max() ||
            obstacle.owner_id <= previous_id)
            return unsupported(WorldAvatarPlaceAuditReason::blocker_id_invalid);
        previous_id = obstacle.owner_id;
        const auto id = static_cast<std::int16_t>(obstacle.owner_id);
        // RoomInfo's static set includes the target itself; its dynamic list
        // excludes it. Both collections can be represented in this query.
        if (id == actor_id) continue;

        const auto* blocker = world.find(id);
        if (!blocker) return unsupported(WorldAvatarPlaceAuditReason::blocker_missing);
        if (!same_group) return unsupported(WorldAvatarPlaceAuditReason::multitile_identity_unavailable);
        const auto same_group_value = same_group(actor_id, id);
        if (!same_group_value)
            return unsupported(WorldAvatarPlaceAuditReason::multitile_identity_unavailable);

        if (*same_group_value) {
            AvatarPlaceOverlapInput overlap;
            overlap.same_multitile_group = true;
            blockers.push_back({id, overlap});
            continue;
        }

        const bool avatar_blocker = blocker->kind == EntityKind::avatar;
        AvatarPlaceOverlapInput overlap;
        overlap.same_multitile_group = *same_group_value;
        overlap.blocker_kind = avatar_blocker ? AvatarPlaceBlockerKind::avatar
                                               : AvatarPlaceBlockerKind::object;
        overlap.all_avatars_solid = all_avatars_solid;

        bool actor_requires_bhav = false;
        bool actor_resolver_unavailable = false;
        const auto actor_ep5 = entry_point5(*actor, actor_id, id, resolve_ep5,
                                             actor_requires_bhav, actor_resolver_unavailable);
        if (!actor_ep5) {
            return unsupported(ep5_failure(actor_requires_bhav, actor_resolver_unavailable));
        }
        std::optional<bool> blocker_ep5 = false;
        bool blocker_requires_bhav = false;
        bool blocker_resolver_unavailable = false;
        if (!*actor_ep5) {
            blocker_ep5 = entry_point5(*blocker, id, actor_id, resolve_ep5,
                                       blocker_requires_bhav, blocker_resolver_unavailable);
            if (!blocker_ep5) {
                return unsupported(ep5_failure(blocker_requires_bhav,
                                               blocker_resolver_unavailable));
            }
        }
        overlap.actor_entry_point5 = *actor_ep5 ? AvatarPlaceKnowledge::true_value
                                                : AvatarPlaceKnowledge::false_value;
        if (blocker_ep5)
            overlap.blocker_entry_point5 = *blocker_ep5 ? AvatarPlaceKnowledge::true_value
                                                        : AvatarPlaceKnowledge::false_value;

        if (*actor_ep5 || (blocker_ep5 && *blocker_ep5)) {
            blockers.push_back({id, overlap});
            continue;
        }

        const auto ep26 = has_entry_point26(*blocker);
        overlap.blocker_has_entry_point26 = ep26;
        const auto blocker_flags = flags_for(world, id);
        if (blocker_flags) {
            overlap.blocker_flags = static_cast<std::uint8_t>(
                static_cast<std::uint16_t>(*blocker_flags) & 0xffU);
        } else if (!avatar_blocker) {
            return unsupported(WorldAvatarPlaceAuditReason::blocker_flags_unavailable);
        }
        if (avatar_blocker) {
            if (!all_avatars_solid)
                return unsupported(WorldAvatarPlaceAuditReason::place_flags_unavailable);
            if (!*all_avatars_solid) {
                const auto actor_flags = flags_for(world, actor_id);
                if (!actor_flags)
                    return unsupported(WorldAvatarPlaceAuditReason::actor_flags_unavailable);
                overlap.actor_allow_person_intersection = ((*actor_flags & 16) != 0);
            }
        }
        blockers.push_back({id, overlap});
        // The underlying Simitone loop stops at the first known blocking
        // object with EP26. Do not resolve any later blocker inputs first:
        // they may be unavailable and their callbacks would not run there.
        auto prefix = evaluate_saved_avatar_place_blockers(blockers, true, 4096);
        if (prefix.early_stop_known == true) {
            WorldAvatarPlaceAuditResult result;
            result.rules = std::move(prefix);
            result.reason = WorldAvatarPlaceAuditReason::none;
            return result;
        }
        if (prefix.status == SavedAvatarPlaceAuditStatus::blocked &&
            !prefix.early_stop_known &&
            obstacle_index + 1 < query.intersections.size())
            return unsupported(WorldAvatarPlaceAuditReason::rule_audit_unsupported);
        if (prefix.status == SavedAvatarPlaceAuditStatus::unsupported)
            return unsupported(WorldAvatarPlaceAuditReason::rule_audit_unsupported);
    }

    WorldAvatarPlaceAuditResult result;
    result.rules = evaluate_saved_avatar_place_blockers(blockers, true, 4096);
    result.reason = result.rules.status == SavedAvatarPlaceAuditStatus::unsupported
        ? WorldAvatarPlaceAuditReason::rule_audit_unsupported
        : WorldAvatarPlaceAuditReason::none;
    return result;
}

const char* world_avatar_place_audit_reason_name(WorldAvatarPlaceAuditReason reason) noexcept {
    switch (reason) {
    case WorldAvatarPlaceAuditReason::none: return "none";
    case WorldAvatarPlaceAuditReason::room_info_incomplete: return "room_info_incomplete";
    case WorldAvatarPlaceAuditReason::actor_missing: return "actor_missing";
    case WorldAvatarPlaceAuditReason::actor_not_avatar: return "actor_not_avatar";
    case WorldAvatarPlaceAuditReason::actor_flags_unavailable: return "actor_flags_unavailable";
    case WorldAvatarPlaceAuditReason::blocker_id_invalid: return "blocker_id_invalid";
    case WorldAvatarPlaceAuditReason::blocker_missing: return "blocker_missing";
    case WorldAvatarPlaceAuditReason::blocker_flags_unavailable: return "blocker_flags_unavailable";
    case WorldAvatarPlaceAuditReason::multitile_identity_unavailable: return "multitile_identity_unavailable";
    case WorldAvatarPlaceAuditReason::entry_point_table_unavailable: return "entry_point_table_unavailable";
    case WorldAvatarPlaceAuditReason::entry_point5_bhav_unimplemented: return "entry_point5_bhav_unimplemented";
    case WorldAvatarPlaceAuditReason::entry_point5_result_unavailable: return "entry_point5_result_unavailable";
    case WorldAvatarPlaceAuditReason::place_flags_unavailable: return "place_flags_unavailable";
    case WorldAvatarPlaceAuditReason::rule_audit_unsupported: return "rule_audit_unsupported";
    }
    return "invalid_reason";
}

} // namespace odh
