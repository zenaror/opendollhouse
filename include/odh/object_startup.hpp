#pragma once

#include "odh/avatar_startup.hpp"
#include "odh/entry_points.hpp"

namespace odh {

using ObjectStartupResult = AvatarStartupResult;

// Runs the object's synchronous Init (EP0) and dynamic-multitile (EP8)
// actions using that object's resource owner. Main (EP1) is intentionally
// left for the entity scheduler after startup succeeds.
ObjectStartupResult run_object_startup(
    Vm&, std::int16_t object_id, CodeOwner resource_owner,
    const EntryPointTable&, std::uint64_t instruction_budget = 10000,
    const AvatarStartupTrace& trace = {});

} // namespace odh
