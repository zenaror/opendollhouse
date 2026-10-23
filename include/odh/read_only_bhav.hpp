#pragma once

#include "odh/world.hpp"

namespace odh {

enum class ReadOnlyBhavState { evaluated_true, evaluated_false, unsupported };
enum class ReadOnlyBhavReason {
    completed, invalid_snapshot, invalid_resources, start_failed, budget_exhausted, vm_fault,
    did_not_complete
};
struct ReadOnlyBhavResult {
    ReadOnlyBhavState state = ReadOnlyBhavState::unsupported;
    ReadOnlyBhavReason reason = ReadOnlyBhavReason::did_not_complete;
    std::uint64_t steps = 0;
    std::string diagnostic;
};
struct ReadOnlyBhavLimits {
    WorldLimits world{};
    VmLimits vm{};
};

// Restore only into a fresh disposable World. All World writes are denied;
// VM-local temps/arguments/locals/StackObjectID may change during execution.
// Only expressions, explicit BHAV calls and object_type_primitive are enabled.
// No callback, file lookup, owner inference, WAIT resumption or World publication.
// Every call starts fresh, runs at most instruction_budget instructions, and
// treats faults/budget exhaustion as unsupported. Allocation exceptions propagate.
ReadOnlyBhavResult evaluate_read_only_bhav(
    const WorldSnapshot&, const RoutineSet&, const RoutineOwners&,
    RoutineKey root, VmContext, const std::vector<std::int16_t>& arguments,
    std::uint64_t instruction_budget, ReadOnlyBhavLimits limits = {});

} // namespace odh
