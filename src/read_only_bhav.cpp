#include "odh/read_only_bhav.hpp"

#include "odh/object_type.hpp"

namespace odh {
namespace {
bool valid_keys(const RoutineSet& routines, bool additional_owner) {
    for (const auto& entry : routines)
        if (entry.first.id < 256 || entry.first.scope != routine_key(entry.first.id).scope ||
            (additional_owner && entry.first.scope == RoutineScope::global)) return false;
    return true;
}
} // namespace

ReadOnlyBhavResult evaluate_read_only_bhav(
    const WorldSnapshot& snapshot, const RoutineSet& routines, const RoutineOwners& owners,
    RoutineKey root, VmContext context, const std::vector<std::int16_t>& arguments,
    std::uint64_t instruction_budget, ReadOnlyBhavLimits limits) {
    if (root.id < 256 || root.scope != routine_key(root.id).scope || !valid_keys(routines, false))
        return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::invalid_resources, 0,
                "invalid root or default routine keys"};
    for (const auto& owner : owners)
        if (owner.first == 0 || !valid_keys(owner.second, true))
            return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::invalid_resources, 0,
                    "invalid additional owner or routine keys"};

    World scratch(limits.world);
    if (!scratch.restore_snapshot(snapshot))
        return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::invalid_snapshot, 0,
                "snapshot rejected by scratch World"};
    VmMemory memory;
    memory.read = [&scratch](const VmFrame& frame, std::uint8_t scope, std::int16_t index) {
        return scratch.read(frame, scope, index);
    };
    memory.write = [](const VmFrame&, std::uint8_t, std::int16_t, std::int16_t) { return false; };
    try {
        Vm vm(routines, std::move(memory), limits.vm, object_type_primitive(scratch), owners);
        if (vm.start(root, context, arguments) != VmStatus::running)
            return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::start_failed,
                    vm.steps(), vm.diagnostic()};
        const auto status = vm.run(instruction_budget);
        if (status == VmStatus::returned_true || status == VmStatus::returned_false)
            return {status == VmStatus::returned_true ? ReadOnlyBhavState::evaluated_true
                                                     : ReadOnlyBhavState::evaluated_false,
                    ReadOnlyBhavReason::completed, vm.steps(), {}};
        const auto reason = status == VmStatus::budget_exhausted ? ReadOnlyBhavReason::budget_exhausted
                          : status == VmStatus::fault ? ReadOnlyBhavReason::vm_fault
                                                     : ReadOnlyBhavReason::did_not_complete;
        return {ReadOnlyBhavState::unsupported, reason, vm.steps(), vm.diagnostic()};
    } catch (const std::invalid_argument& error) {
        return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::invalid_resources, 0, error.what()};
    }
}
} // namespace odh
