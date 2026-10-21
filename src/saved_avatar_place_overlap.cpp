#include "odh/saved_avatar_place_overlap.hpp"

namespace odh {
namespace {

constexpr std::uint8_t kDisallowPersonIntersection = 2;
constexpr std::uint8_t kAllowPersonIntersection = 16;

AvatarPlaceOverlapResult result(AvatarPlaceOverlapStatus status,
                                std::optional<bool> break_on_blocker = false) noexcept {
  return {status, break_on_blocker};
}

bool valid_entry_point_knowledge(AvatarPlaceKnowledge value) noexcept {
  return value == AvatarPlaceKnowledge::unknown ||
         value == AvatarPlaceKnowledge::false_value ||
         value == AvatarPlaceKnowledge::true_value;
}

}  // namespace

AvatarPlaceOverlapResult evaluate_saved_avatar_place_overlap(
    const AvatarPlaceOverlapInput& input) noexcept {
  if (input.same_multitile_group) {
    return result(AvatarPlaceOverlapStatus::ignored_same_multitile_group);
  }

  if (input.blocker_kind != AvatarPlaceBlockerKind::avatar &&
      input.blocker_kind != AvatarPlaceBlockerKind::object)
    return result(AvatarPlaceOverlapStatus::unsupported_rule_input);

  if (!valid_entry_point_knowledge(input.actor_entry_point5) ||
      !valid_entry_point_knowledge(input.blocker_entry_point5))
    return result(AvatarPlaceOverlapStatus::unsupported_entry_point5);

  if (input.actor_entry_point5 == AvatarPlaceKnowledge::true_value ||
      input.blocker_entry_point5 == AvatarPlaceKnowledge::true_value) {
    return result(AvatarPlaceOverlapStatus::exempt_entry_point5);
  }

  if (input.actor_entry_point5 == AvatarPlaceKnowledge::unknown ||
      input.blocker_entry_point5 == AvatarPlaceKnowledge::unknown) {
    return result(AvatarPlaceOverlapStatus::unsupported_entry_point5);
  }

  bool passes = false;
  if (input.blocker_kind == AvatarPlaceBlockerKind::avatar) {
    if (input.all_avatars_solid == true) {
      passes = false;
    } else if (input.all_avatars_solid == false) {
      if (!input.actor_allow_person_intersection)
        return result(AvatarPlaceOverlapStatus::unsupported_rule_input);
      passes = *input.actor_allow_person_intersection;
    } else if (input.actor_allow_person_intersection == false) {
      passes = false;
    } else {
      return result(AvatarPlaceOverlapStatus::unsupported_rule_input);
    }
  } else if (input.blocker_flags.has_value()) {
    const auto flags = *input.blocker_flags;
    passes = (flags & kAllowPersonIntersection) != 0 &&
             (flags & kDisallowPersonIntersection) == 0;
  } else {
    return result(AvatarPlaceOverlapStatus::unsupported_rule_input);
  }

  if (passes) return result(AvatarPlaceOverlapStatus::allowed);
  return result(AvatarPlaceOverlapStatus::blocking, input.blocker_has_entry_point26);
}

}  // namespace odh
