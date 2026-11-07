#include "odh/object_startup.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace odh {

ObjectStartupResult run_object_startup(
    Vm& vm, std::int16_t object_id, CodeOwner resource_owner,
    const EntryPointTable& entry_points, std::uint64_t instruction_budget,
    const AvatarStartupTrace& trace, bool scheduler_running_now,
    const VmPrimitive& synchronous_primitive, const std::function<bool()>& self_deleted) {
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
        const auto checked=vm.evaluate_entry_point_check(routine_key(routine),context,
            std::vector<std::int16_t>(4,0),
            remaining,scheduler_running_now,synchronous_primitive,[&](const VmTrace& record){
                if(trace)trace(phase,record);
            },static_cast<bool>(self_deleted));
        const auto used=checked.steps;
        result.stages.push_back({phase,routine,checked.status,used,checked.diagnostic});
        if (used > std::numeric_limits<std::uint64_t>::max() - result.steps)
            throw std::overflow_error("object startup instruction count overflow");
        result.steps += used;
        // Only the VM's opt-in typed deletion stop plus the host's exact
        // removed identity proof can complete a self-deleting constructor.
        if (checked.started && checked.lifecycle_deleted && self_deleted && self_deleted()) {
            result.completed = true; result.self_deleted = true; return result;
        }
        if (checked.lifecycle_deleted || (self_deleted && self_deleted())) {
            result.diagnostic = "object lifecycle deletion lacks its matching typed stop/identity proof";
            return result;
        }
        if (!checked.started || (checked.status != VmStatus::returned_true && checked.status != VmStatus::returned_false)) {
            result.diagnostic = "object startup " + std::string(phase) + " entry " +
                std::to_string(routine) + " did not return synchronously (" +
                vm_status_name(checked.status) + ")";
            if(!checked.diagnostic.empty())result.diagnostic += ": " + checked.diagnostic;
            return result;
        }
    }
    result.completed = true;
    return result;
}

} // namespace odh
