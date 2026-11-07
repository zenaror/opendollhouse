#pragma once

#include "odh/vm.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace odh {

struct AvatarStartupStage {
    std::string phase;
    std::uint16_t routine = 0;
    VmStatus status = VmStatus::ready;
    std::uint64_t steps = 0;
    std::string diagnostic;
};

struct AvatarStartupResult {
    std::vector<AvatarStartupStage> stages;
    std::uint64_t steps = 0;
    bool completed = false;
    std::string diagnostic;
    bool self_deleted = false; // Trusted lifecycle observed source deletion during the entry.
};

using AvatarStartupTrace = std::function<void(const std::string&, const VmTrace&)>;

// Runs the TS1 construction entries on an already-bound avatar VM. The VM
// supplies that avatar's private code owner and shared World/RNG callbacks.
// Init and entry 8 share one instruction budget; Main is never started here.
// VMEntity.ExecuteEntryPoint normalizes a null Init array to four zeros. Both
// phases therefore use exactly four words, without metadata-based padding.
AvatarStartupResult run_avatar_startup(
    Vm& vm, std::int16_t avatar_id, std::uint16_t init_entry,
    std::uint16_t dynamic_multitile_entry, std::uint64_t instruction_budget = 10000,
    const AvatarStartupTrace& trace = {});

} // namespace odh
