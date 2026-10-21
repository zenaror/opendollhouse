#include "odh/saved_avatar_place_audit.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using odh::AvatarPlaceKnowledge;
using odh::AvatarPlaceOverlapInput;
using odh::SavedAvatarPlaceAuditResult;
using odh::SavedAvatarPlaceAuditStatus;
using odh::SavedAvatarPlaceBlockerCandidate;

AvatarPlaceOverlapInput blocking(std::optional<bool> ep26 = false) {
  AvatarPlaceOverlapInput input;
  input.actor_entry_point5 = AvatarPlaceKnowledge::false_value;
  input.blocker_entry_point5 = AvatarPlaceKnowledge::false_value;
  input.blocker_flags = std::uint8_t{0};
  input.blocker_has_entry_point26 = ep26;
  return input;
}

AvatarPlaceOverlapInput allowed() {
  auto input = blocking(false);
  input.blocker_flags = std::uint8_t{16};
  return input;
}

AvatarPlaceOverlapInput exempt() {
  auto input = blocking(false);
  input.blocker_entry_point5 = AvatarPlaceKnowledge::true_value;
  return input;
}

SavedAvatarPlaceBlockerCandidate candidate(std::int32_t id,
                                            AvatarPlaceOverlapInput overlap) {
  return {id, overlap};
}

struct Case {
  const char* name;
  std::vector<SavedAvatarPlaceBlockerCandidate> blockers;
  bool complete = true;
  std::size_t budget = 4096;
  SavedAvatarPlaceAuditStatus status = SavedAvatarPlaceAuditStatus::unsupported;
  std::size_t classifications = 0;
  std::int32_t final_id = 0;
  std::optional<bool> early_stop;
};

bool matches(const SavedAvatarPlaceAuditResult& actual, const Case& test) {
  return actual.status == test.status &&
         actual.pair_classifications == test.classifications &&
         actual.final_blocker_id == test.final_id &&
         actual.early_stop_known == test.early_stop;
}

}  // namespace

int main() {
  const std::vector<Case> cases{
      {"ascending IDs preserve last blocking blocker", {candidate(2, blocking()), candidate(7, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::blocked, 2, 7, false},
      {"allowed and EP5-exempt candidates continue", {candidate(1, allowed()), candidate(3, exempt()), candidate(8, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::blocked, 3, 8, false},
      {"EP26 true stops at current blocker", {candidate(1, blocking(true)), candidate(4, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::blocked, 1, 1, true},
      {"unknown EP26 before later candidate is ambiguous", {candidate(1, blocking(std::nullopt)), candidate(4, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 1, 0, std::nullopt},
      {"unknown EP26 on final blocker keeps definite block", {candidate(5, blocking(std::nullopt))}, true, 4096, SavedAvatarPlaceAuditStatus::blocked, 1, 5, std::nullopt},
      {"unsupported pair fails closed", {candidate(1, AvatarPlaceOverlapInput{})}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 1, 0, std::nullopt},
      {"zero ID rejected before pair evaluation", {candidate(0, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"negative ID rejected", {candidate(-1, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"ID outside VM short range rejected", {candidate(32768, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"duplicate ID rejected", {candidate(2, blocking(false)), candidate(2, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"descending IDs rejected", {candidate(3, blocking(false)), candidate(2, blocking(false))}, true, 4096, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"incomplete inventory rejected", {candidate(2, blocking(false))}, false, 4096, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"over budget rejected", {candidate(1, blocking(false)), candidate(2, blocking(false))}, true, 1, SavedAvatarPlaceAuditStatus::unsupported, 0, 0, std::nullopt},
      {"empty complete list is allowed", {}, true, 4096, SavedAvatarPlaceAuditStatus::allowed, 0, 0, false},
  };

  for (const auto& test : cases) {
    const auto actual = odh::evaluate_saved_avatar_place_blockers(
        test.blockers, test.complete, test.budget);
    if (!matches(actual, test)) {
      std::cerr << "failed: " << test.name << '\n';
      return EXIT_FAILURE;
    }
  }

  std::cout << cases.size() << " synthetic blocker aggregation cases passed\n";
  return EXIT_SUCCESS;
}
