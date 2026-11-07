// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include <cstddef>

namespace odh {
// VMAvatar.Tick, pinned FreeSO7e76c196: advance writable PersonData27 once
// after avatar motive/animation updates, including out-of-world avatars.
// Caller owns logical tick ordering. No time rebase or restore-time update.
// Preflight every avatar before writes; false leaves all counters unchanged.
// Ghost images are included, as in the reference avatar Tick implementation.
bool tick_ts1_avatar_counters(World&,std::size_t avatar_budget=4096);
}
