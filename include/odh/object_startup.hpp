#pragma once

#include "odh/avatar_startup.hpp"
#include "odh/entry_points.hpp"

namespace odh {

using ObjectStartupResult = AvatarStartupResult;

// Runs the object's synchronous Init (EP0) and dynamic-multitile (EP8)
// actions on disposable checks seeded from that object's real Thread.
// Register changes persist only during a scheduler tick. Pinned FreeSO7e76
// VMEntity.Init passes null for EP0, but ExecuteEntryPoint replaces null with
// new short[4] before building its frame. Thus EP0 and EP8 both receive exactly
// four zeros, independent of the BHAV metadata's declared argument count.
// Main (EP1) is intentionally
// left for the entity scheduler after startup succeeds.
ObjectStartupResult run_object_startup(
    Vm&, std::int16_t object_id, CodeOwner resource_owner,
    const EntryPointTable&, std::uint64_t instruction_budget = 10000,
    const AvatarStartupTrace& trace = {}, bool scheduler_running_now = false,
    const VmPrimitive& synchronous_primitive = {},
    const std::function<bool()>& self_deleted = {});

} // namespace odh
