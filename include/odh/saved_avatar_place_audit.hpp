#pragma once

#include "odh/saved_avatar_place_overlap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {

enum class SavedAvatarPlaceAuditStatus { allowed, blocked, unsupported };

struct SavedAvatarPlaceBlockerCandidate {
  std::int32_t blocker_id = 0;
  AvatarPlaceOverlapInput overlap;
};

struct SavedAvatarPlaceAuditResult {
  SavedAvatarPlaceAuditStatus status = SavedAvatarPlaceAuditStatus::unsupported;
  std::size_t pair_classifications = 0;
  std::int32_t final_blocker_id = 0;
  // True means evaluation stopped at EP26; false means iteration completed;
  // missing means a final known blocker had unknown EP26 presence.
  std::optional<bool> early_stop_known;
};

// Audits an already discovered, complete, strictly increasing blocker list. This
// models rule aggregation only; it does not discover geometry or run BHAVs.
SavedAvatarPlaceAuditResult evaluate_saved_avatar_place_blockers(
    const std::vector<SavedAvatarPlaceBlockerCandidate>& blockers,
    bool inventory_complete, std::size_t max_blockers = 4096) noexcept;

}  // namespace odh
