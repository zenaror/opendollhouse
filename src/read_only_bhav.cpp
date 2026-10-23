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

PrimitiveOutcome set_to_next_person(const World& scratch, const VmFrame& frame,
                                   const Instruction& instruction) {
    // Simitone's opcode 31 has many search modes and some write object state.
    // This one exact Global.iff#355 form searches the live avatar list using
    // StackObjectID and changes only the disposable VM frame's StackObjectID.
    static constexpr std::array<std::uint8_t, 8> operands{0, 0, 0, 0, 129, 10, 0, 0};
    if (instruction.opcode != 31 || frame.key.scope != RoutineScope::global ||
        frame.key.id != 355 || frame.pc != 3 || instruction.on_true != 4 ||
        instruction.on_false != 0xff || instruction.operands != operands)
        return {PrimitiveDisposition::unsupported, "unsupported read-only primitive"};

    const auto next = scratch.next_entity(frame.context.stack_object, EntityKind::avatar);
    if (!next) return {PrimitiveDisposition::goto_false, {}};
    PrimitiveOutcome outcome;
    outcome.disposition = PrimitiveDisposition::goto_true;
    outcome.stack_object_write = VmStackObjectWrite{*next};
    return outcome;
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
        const auto object_type = object_type_primitive(scratch);
        VmPrimitive primitive = [&scratch, &object_type](const VmFrame& frame,
            const Instruction& instruction, const std::array<std::int16_t, 20>& temps,
            const VmDispatch& dispatch) {
            if (instruction.opcode == 31)
                return set_to_next_person(scratch, frame, instruction);
            return object_type(frame, instruction, temps, dispatch);
        };
        Vm vm(routines, std::move(memory), limits.vm, std::move(primitive), owners);
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
