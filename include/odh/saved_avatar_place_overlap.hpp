#pragma once

#include <cstdint>
#include <optional>

namespace odh {

enum class AvatarPlaceKnowledge { unknown, false_value, true_value };
enum class AvatarPlaceBlockerKind { avatar, object };
enum class AvatarPlaceOverlapStatus {
  ignored_same_multitile_group,
  exempt_entry_point5,
  unsupported_entry_point5,
  unsupported_rule_input,
  allowed,
  blocking,
};

struct AvatarPlaceOverlapInput {
  bool same_multitile_group = false;
  AvatarPlaceBlockerKind blocker_kind = AvatarPlaceBlockerKind::object;
  std::optional<bool> actor_allow_person_intersection;
  std::optional<std::uint8_t> blocker_flags;
  std::optional<bool> all_avatars_solid;
  AvatarPlaceKnowledge actor_entry_point5 = AvatarPlaceKnowledge::unknown;
  AvatarPlaceKnowledge blocker_entry_point5 = AvatarPlaceKnowledge::unknown;
  std::optional<bool> blocker_has_entry_point26;
};

struct AvatarPlaceOverlapResult {
  AvatarPlaceOverlapStatus status = AvatarPlaceOverlapStatus::blocking;
  // Missing only when the blocker is known to block but EntryPoint 26 presence
  // was not supplied; this affects iteration order, not the overlap decision.
  std::optional<bool> break_on_blocker = false;
};

// Models only the overlapping blocker rules in Simitone VMContext.GetAvatarPlace.
AvatarPlaceOverlapResult evaluate_saved_avatar_place_overlap(
    const AvatarPlaceOverlapInput& input) noexcept;

}  // namespace odh
