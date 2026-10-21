#include "odh/entry_point5_readonly.hpp"

#include "odh/object_type.hpp"
#include "odh/world.hpp"

namespace odh {
namespace {
std::uint16_t word(const Instruction& instruction, std::size_t offset) {
    return static_cast<std::uint16_t>(instruction.operands[offset] |
        (static_cast<std::uint16_t>(instruction.operands[offset + 1]) << 8));
}

bool supported_shape(std::uint16_t routine_id, const Bhav& bhav,
                     std::uint32_t& tested_guid) {
    if (routine_id < 4096 || bhav.version != 0x8002 || !bhav.metadata ||
        bhav.metadata->arguments != 0 || bhav.metadata->locals != 0 ||
        bhav.instructions.size() != 2)
        return false;
    const auto& type_test = bhav.instructions[0];
    const auto& attribute_test = bhav.instructions[1];
    if (type_test.opcode != 32 || type_test.on_true != 1 || type_test.on_false != 0xff ||
        word(type_test, 4) != 0 || type_test.operands[6] != 10 || type_test.operands[7] != 0)
        return false;
    tested_guid = static_cast<std::uint32_t>(type_test.operands[0]) |
        (static_cast<std::uint32_t>(type_test.operands[1]) << 8) |
        (static_cast<std::uint32_t>(type_test.operands[2]) << 16) |
        (static_cast<std::uint32_t>(type_test.operands[3]) << 24);
    if (attribute_test.opcode != 2 || attribute_test.on_true != 0xfe ||
        attribute_test.on_false != 0xff || word(attribute_test, 0) != 16 ||
        word(attribute_test, 2) != 1 || attribute_test.operands[4] != 0 ||
        attribute_test.operands[5] != 2 || attribute_test.operands[6] != 1 ||
        attribute_test.operands[7] != 7)
        return false;
    return true;
}
} // namespace

ReadOnlyEntryPoint5Result evaluate_read_only_entry_point5(
    std::uint16_t routine_id, const Bhav& bhav, std::uint32_t stack_object_guid,
    std::optional<std::uint32_t> stack_object_master_guid,
    std::optional<std::int16_t> stack_object_attribute16) {
    std::uint32_t tested_guid = 0;
    if (!supported_shape(routine_id, bhav, tested_guid))
        return {ReadOnlyEntryPoint5State::unsupported, "bhav_shape_not_in_read_only_allowlist", 0};
    const bool guid_matches = stack_object_guid == tested_guid ||
        (stack_object_master_guid && *stack_object_master_guid == tested_guid);
    if (guid_matches && !stack_object_attribute16)
        return {ReadOnlyEntryPoint5State::unsupported, "stack_object_attribute16_unavailable", 0};

    // A tiny disposable World is sufficient because the allowlisted routine
    // reads only the stack object's identity and attribute 16. It is never the
    // Main World, and static validation above rejects every mutating opcode.
    World scratch;
    if (!scratch.create(1, EntityKind::avatar) ||
        !scratch.create(2, EntityKind::object, EntityLayout{17, 0, 0, 0}) ||
        !scratch.define_type(2, stack_object_guid, stack_object_master_guid))
        return {ReadOnlyEntryPoint5State::unsupported, "scratch_world_setup_failed", 0};
    if (stack_object_attribute16) {
        const VmFrame frame{routine_key(4096), 0, {1, 1, 2, 0}, {}, {}};
        if (!scratch.write(frame, 1, 16, *stack_object_attribute16))
            return {ReadOnlyEntryPoint5State::unsupported, "scratch_attribute_setup_failed", 0};
    }

    RoutineSet routines;
    if (!routines.emplace(routine_key(routine_id), bhav).second)
        return {ReadOnlyEntryPoint5State::unsupported, "routine_binding_failed", 0};
    VmLimits limits;
    limits.stack_depth = 1;
    limits.frame_variables = 4;
    limits.code_owners = 1;
    Vm vm(std::move(routines), scratch.vm_memory(), limits, object_type_primitive(scratch));
    if (vm.start(routine_key(routine_id), {1, 1, 2, 0}) != VmStatus::running)
        return {ReadOnlyEntryPoint5State::unsupported, "vm_start_failed", vm.steps()};
    const auto status = vm.run(2);
    if (status == VmStatus::returned_true)
        return {ReadOnlyEntryPoint5State::evaluated_true, "read_only_bhav_completed", vm.steps()};
    if (status == VmStatus::returned_false)
        return {ReadOnlyEntryPoint5State::evaluated_false, "read_only_bhav_completed", vm.steps()};
    return {ReadOnlyEntryPoint5State::unsupported, "read_only_bhav_did_not_complete", vm.steps()};
}

const char* read_only_entry_point5_state_name(ReadOnlyEntryPoint5State state) noexcept {
    switch (state) {
    case ReadOnlyEntryPoint5State::evaluated_true: return "evaluated_true";
    case ReadOnlyEntryPoint5State::evaluated_false: return "evaluated_false";
    case ReadOnlyEntryPoint5State::unsupported: return "unsupported";
    }
    return "invalid_state";
}

} // namespace odh
