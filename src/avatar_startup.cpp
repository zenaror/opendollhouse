#include "odh/avatar_startup.hpp"

#include <array>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
struct Entry { const char* phase; std::uint16_t routine; };
}

AvatarStartupResult run_avatar_startup(
    Vm& vm, std::int16_t avatar_id, std::uint16_t init_entry,
    std::uint16_t dynamic_multitile_entry, std::uint64_t instruction_budget,
    const AvatarStartupTrace& trace) {
    if (avatar_id <= 0) throw std::invalid_argument("avatar startup needs a positive runtime ID");

    const std::array<Entry, 2> entries{{
        {"init", init_entry}, {"dynamic_multitile", dynamic_multitile_entry}}};
    AvatarStartupResult result;
    result.stages.reserve(entries.size());
    for (const auto& entry : entries) {
        if (entry.routine <= 255) {
            result.stages.push_back({entry.phase, entry.routine, VmStatus::ready, 0,
                                     "entry ID<=255 skipped by reference"});
            continue;
        }
        const auto remaining = result.steps < instruction_budget ?
            instruction_budget - result.steps : 0;
        const VmContext context{avatar_id, avatar_id, 0, static_cast<CodeOwner>(avatar_id)};
        vm.start(routine_key(entry.routine), context, {0, 0, 0, 0},true);
        vm.run(remaining, [&](const VmTrace& record) {
            if (trace) trace(entry.phase, record);
        });
        const auto used = vm.steps();
        result.stages.push_back({entry.phase, entry.routine, vm.status(), used, vm.diagnostic()});
        if (used > std::numeric_limits<std::uint64_t>::max() - result.steps)
            throw std::overflow_error("avatar startup instruction count overflow");
        result.steps += used;
        if (vm.status() != VmStatus::returned_true && vm.status() != VmStatus::returned_false) {
            result.diagnostic = "startup entry did not return synchronously";
            return result;
        }
    }
    result.completed = true;
    return result;
}
} // namespace odh
