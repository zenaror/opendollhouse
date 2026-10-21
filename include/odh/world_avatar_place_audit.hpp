#pragma once

#include "odh/room_info_obstacles.hpp"
#include "odh/saved_avatar_place_audit.hpp"
#include "odh/world.hpp"

#include <cstdint>
#include <functional>
#include <optional>

namespace odh {

enum class WorldAvatarPlaceAuditReason : std::uint8_t {
    none,
    room_info_incomplete,
    actor_missing,
    actor_not_avatar,
    actor_flags_unavailable,
    blocker_id_invalid,
    blocker_missing,
    blocker_flags_unavailable,
    multitile_identity_unavailable,
    entry_point_table_unavailable,
    entry_point5_bhav_unimplemented,
    entry_point5_result_unavailable,
    place_flags_unavailable,
    rule_audit_unsupported,
};

struct WorldAvatarPlaceAuditResult {
    SavedAvatarPlaceAuditResult rules;
    WorldAvatarPlaceAuditReason reason = WorldAvatarPlaceAuditReason::room_info_incomplete;
};

using WorldAvatarPlaceEntryPoint5Resolver =
    std::function<std::optional<bool>(std::int16_t, std::int16_t)>;

// Audits entity-overlap rules over a complete entity-backed RoomInfo query. Group identity
// and AllAvatarsSolid are caller-supplied runtime facts. EntryPoint 5 action
// functions that reference BHAVs remain unsupported because this adapter does
// not execute BHAV. This is a Simitone-reference diagnostic, not final placement
// acceptance or proof of original executable behavior.
WorldAvatarPlaceAuditResult audit_world_avatar_place_overlaps(
    const World&, std::int16_t actor_id, const RoomInfoObstacleQuery&,
    std::optional<bool> all_avatars_solid,
    const std::function<std::optional<bool>(std::int16_t, std::int16_t)>& same_multitile_group,
    const WorldAvatarPlaceEntryPoint5Resolver& entry_point5_result = {});

const char* world_avatar_place_audit_reason_name(WorldAvatarPlaceAuditReason) noexcept;

} // namespace odh
