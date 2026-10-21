#include "odh/saved_avatar_place_audit.hpp"

#include <limits>

namespace odh {

SavedAvatarPlaceAuditResult evaluate_saved_avatar_place_blockers(
    const std::vector<SavedAvatarPlaceBlockerCandidate>& blockers,
    bool inventory_complete, std::size_t max_blockers) noexcept {
  SavedAvatarPlaceAuditResult result;

  if (!inventory_complete || blockers.size() > max_blockers) return result;

  std::int32_t previous_id = 0;
  for (const auto& blocker : blockers) {
    if (blocker.blocker_id <= 0 ||
        blocker.blocker_id > std::numeric_limits<std::int16_t>::max() ||
        blocker.blocker_id <= previous_id)
      return result;
    previous_id = blocker.blocker_id;
  }

  result.status = SavedAvatarPlaceAuditStatus::allowed;
  result.early_stop_known = false;

  for (std::size_t i = 0; i < blockers.size(); ++i) {
    const auto pair = evaluate_saved_avatar_place_overlap(blockers[i].overlap);
    ++result.pair_classifications;

    switch (pair.status) {
      case AvatarPlaceOverlapStatus::ignored_same_multitile_group:
      case AvatarPlaceOverlapStatus::exempt_entry_point5:
      case AvatarPlaceOverlapStatus::allowed:
        continue;
      case AvatarPlaceOverlapStatus::blocking:
        result.status = SavedAvatarPlaceAuditStatus::blocked;
        result.final_blocker_id = blockers[i].blocker_id;
        if (pair.break_on_blocker == true) {
          result.early_stop_known = true;
          return result;
        }
        if (!pair.break_on_blocker.has_value()) {
          if (i + 1 < blockers.size()) {
            result.status = SavedAvatarPlaceAuditStatus::unsupported;
            result.final_blocker_id = 0;
            result.early_stop_known.reset();
            return result;
          }
          result.early_stop_known.reset();
        }
        continue;
      case AvatarPlaceOverlapStatus::unsupported_entry_point5:
      case AvatarPlaceOverlapStatus::unsupported_rule_input:
        result.status = SavedAvatarPlaceAuditStatus::unsupported;
        result.final_blocker_id = 0;
        result.early_stop_known.reset();
        return result;
    }

    result.status = SavedAvatarPlaceAuditStatus::unsupported;
    result.final_blocker_id = 0;
    result.early_stop_known.reset();
    return result;
  }

  return result;
}

}  // namespace odh
