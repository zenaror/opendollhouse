#include "odh/object_startup.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace odh {

ObjectStartupResult run_object_startup(
    Vm& vm, std::int16_t object_id, CodeOwner resource_owner,
    const EntryPointTable& entry_points, std::uint64_t instruction_budget,
    const AvatarStartupTrace& trace) {
    if (object_id <= 0 || resource_owner == 0)
        throw std::invalid_argument("object startup needs a positive ID and resource owner");
    if (entry_points.entries.size() <= 8)
        throw std::invalid_argument("object startup needs entry points 0 and 8");

    const std::array<std::pair<const char*, std::uint16_t>, 2> stages{{
        {"init", entry_points.entries[0].action},
        {"dynamic_multitile", entry_points.entries[8].action}
    }};
    ObjectStartupResult result;
    result.stages.reserve(stages.size());
    for (const auto& [phase, routine] : stages) {
        if (routine <= 255) {
            result.stages.push_back({phase, routine, VmStatus::ready, 0,
                                     "entry ID<=255 skipped by reference"});
            continue;
        }
        const auto remaining = result.steps < instruction_budget ?
            instruction_budget - result.steps : 0;
        const VmContext context{object_id, object_id, 0, resource_owner};
        vm.start(routine_key(routine), context, {0, 0, 0, 0});
        vm.run(remaining, [&](const VmTrace& record) {
            if (trace) trace(phase, record);
        });
        const auto used = vm.steps();
        result.stages.push_back({phase, routine, vm.status(), used, vm.diagnostic()});
        if (used > std::numeric_limits<std::uint64_t>::max() - result.steps)
            throw std::overflow_error("object startup instruction count overflow");
        result.steps += used;
        if (vm.status() != VmStatus::returned_true && vm.status() != VmStatus::returned_false) {
            result.diagnostic = "object startup entry did not return synchronously";
            return result;
        }
    }
    result.completed = true;
    return result;
}

} // namespace odh
