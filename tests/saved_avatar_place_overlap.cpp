#include "odh/saved_avatar_place_overlap.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using odh::AvatarPlaceBlockerKind;
using odh::AvatarPlaceKnowledge;
using odh::AvatarPlaceOverlapInput;
using odh::AvatarPlaceOverlapStatus;

struct Case {
  const char* name;
  AvatarPlaceOverlapInput input;
  AvatarPlaceOverlapStatus expected_status;
  bool expected_break;
};

AvatarPlaceOverlapInput base() {
  AvatarPlaceOverlapInput input;
  input.actor_entry_point5 = AvatarPlaceKnowledge::false_value;
  input.blocker_entry_point5 = AvatarPlaceKnowledge::false_value;
  input.actor_allow_person_intersection = false;
  input.all_avatars_solid = false;
  input.blocker_has_entry_point26 = false;
  input.blocker_flags = std::uint8_t{0};
  return input;
}

}  // namespace

int main() {
  auto same_group = base();
  same_group.same_multitile_group = true;
  same_group.actor_entry_point5 = AvatarPlaceKnowledge::unknown;

  auto actor_ep5_true = base();
  actor_ep5_true.actor_entry_point5 = AvatarPlaceKnowledge::true_value;
  actor_ep5_true.blocker_entry_point5 = AvatarPlaceKnowledge::unknown;

  auto blocker_ep5_true = base();
  blocker_ep5_true.blocker_entry_point5 = AvatarPlaceKnowledge::true_value;
  blocker_ep5_true.actor_entry_point5 = AvatarPlaceKnowledge::unknown;

  auto unknown_ep5 = base();
  unknown_ep5.actor_entry_point5 = AvatarPlaceKnowledge::unknown;

  auto actor_allowed = base();
  actor_allowed.blocker_kind = AvatarPlaceBlockerKind::avatar;
  actor_allowed.actor_allow_person_intersection = true;

  auto all_solid = actor_allowed;
  all_solid.all_avatars_solid = true;
  all_solid.blocker_has_entry_point26 = true;

  auto object_allowed = base();
  object_allowed.blocker_flags = std::uint8_t{16};

  auto object_disallowed = base();
  object_disallowed.blocker_flags = std::uint8_t{18};
  object_disallowed.blocker_has_entry_point26 = true;

  auto object_missing_flags = base();
  object_missing_flags.blocker_flags.reset();

  auto avatar_missing_allow = base();
  avatar_missing_allow.blocker_kind = AvatarPlaceBlockerKind::avatar;
  avatar_missing_allow.actor_allow_person_intersection.reset();

  auto avatar_unknown_all_solid = base();
  avatar_unknown_all_solid.blocker_kind = AvatarPlaceBlockerKind::avatar;
  avatar_unknown_all_solid.actor_allow_person_intersection = true;
  avatar_unknown_all_solid.all_avatars_solid.reset();

  auto avatar_known_blocked_despite_unknown_all_solid = base();
  avatar_known_blocked_despite_unknown_all_solid.blocker_kind = AvatarPlaceBlockerKind::avatar;
  avatar_known_blocked_despite_unknown_all_solid.actor_allow_person_intersection = false;
  avatar_known_blocked_despite_unknown_all_solid.all_avatars_solid.reset();

  auto missing_ep26 = base();
  missing_ep26.blocker_flags = std::uint8_t{0};
  missing_ep26.blocker_has_entry_point26.reset();

  auto invalid_ep5 = base();
  invalid_ep5.actor_entry_point5 = static_cast<AvatarPlaceKnowledge>(99);

  auto invalid_blocker_kind = base();
  invalid_blocker_kind.blocker_kind = static_cast<AvatarPlaceBlockerKind>(99);

  auto avatar_no_permission = base();
  avatar_no_permission.blocker_kind = AvatarPlaceBlockerKind::avatar;

  const std::array<Case, 16> cases{{
      {"same group precedes entry point 5", same_group,
       AvatarPlaceOverlapStatus::ignored_same_multitile_group, false},
      {"actor EP5 true exempts", actor_ep5_true,
       AvatarPlaceOverlapStatus::exempt_entry_point5, false},
      {"blocker EP5 true exempts", blocker_ep5_true,
       AvatarPlaceOverlapStatus::exempt_entry_point5, false},
      {"unknown EP5 unsupported", unknown_ep5,
       AvatarPlaceOverlapStatus::unsupported_entry_point5, false},
      {"actor permission allows avatar", actor_allowed,
       AvatarPlaceOverlapStatus::allowed, false},
      {"AllAvatarsSolid blocks with EP26 break", all_solid,
       AvatarPlaceOverlapStatus::blocking, true},
      {"object allow bit allows", object_allowed,
       AvatarPlaceOverlapStatus::allowed, false},
      {"object disallow bit wins", object_disallowed,
       AvatarPlaceOverlapStatus::blocking, true},
      {"missing object flags remain unsupported", object_missing_flags,
       AvatarPlaceOverlapStatus::unsupported_rule_input, false},
      {"avatar without permission blocks", avatar_no_permission,
       AvatarPlaceOverlapStatus::blocking, false},
      {"missing actor permission is unsupported when needed", avatar_missing_allow,
       AvatarPlaceOverlapStatus::unsupported_rule_input, false},
      {"unknown AllAvatarsSolid is unsupported when outcome varies", avatar_unknown_all_solid,
       AvatarPlaceOverlapStatus::unsupported_rule_input, false},
      {"known disallow blocks despite unknown AllAvatarsSolid", avatar_known_blocked_despite_unknown_all_solid,
       AvatarPlaceOverlapStatus::blocking, false},
      {"unknown EP26 leaves break status unknown", missing_ep26,
       AvatarPlaceOverlapStatus::blocking, false},
      {"invalid EP5 enum remains unsupported", invalid_ep5,
       AvatarPlaceOverlapStatus::unsupported_entry_point5, false},
      {"invalid blocker kind remains unsupported", invalid_blocker_kind,
       AvatarPlaceOverlapStatus::unsupported_rule_input, false},
  }};

  for (const auto& test : cases) {
    const auto actual = odh::evaluate_saved_avatar_place_overlap(test.input);
    if (actual.status != test.expected_status ||
        actual.break_on_blocker.value_or(false) != test.expected_break) {
      std::cerr << "failed: " << test.name << '\n';
      return EXIT_FAILURE;
    }
  }

  if (odh::evaluate_saved_avatar_place_overlap(missing_ep26).break_on_blocker.has_value()) {
    std::cerr << "failed: missing EP26 was reported as known\n";
    return EXIT_FAILURE;
  }

  std::cout << cases.size() << " synthetic overlap cases passed\n";
  return EXIT_SUCCESS;
}
