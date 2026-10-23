#include "odh/entry_point5_readonly.hpp"

#include "odh/object_type.hpp"
#include "odh/world.hpp"

#include <algorithm>

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

bool is_urnstone_shape(std::uint16_t routine_id, const Bhav& bhav) {
    if (routine_id != 4105 || bhav.version != 0x8002 || !bhav.metadata ||
        bhav.metadata->type != 0 || bhav.metadata->arguments != 1 ||
        bhav.metadata->locals != 0 || bhav.metadata->tree_version != 3 ||
        bhav.instructions.size() != 2) return false;
    const auto& is_person = bhav.instructions[0];
    const auto& same_neighbor = bhav.instructions[1];
    return is_person.opcode == 2 && is_person.on_true == 1 && is_person.on_false == 0xff &&
        word(is_person, 0) == 9 && word(is_person, 2) == 2 &&
        is_person.operands[4] == 0 && is_person.operands[5] == 2 &&
        is_person.operands[6] == 21 && is_person.operands[7] == 7 &&
        same_neighbor.opcode == 2 && same_neighbor.on_true == 0xfe &&
        same_neighbor.on_false == 0xff && word(same_neighbor, 0) == 31 &&
        word(same_neighbor, 2) == 1 && same_neighbor.operands[4] == 0 &&
        same_neighbor.operands[5] == 2 && same_neighbor.operands[6] == 19 &&
        same_neighbor.operands[7] == 0;
}

bool is_dining_table_shape(std::uint16_t routine_id, const Bhav& bhav) {
    if (routine_id != 4122 || bhav.version != 0x8002 || !bhav.metadata ||
        bhav.metadata->type != 0 || bhav.metadata->arguments != 0 ||
        bhav.metadata->locals != 0 || bhav.metadata->tree_version != 1 ||
        bhav.instructions.size() != 3) return false;
    const auto& verify_person = bhav.instructions[0];
    const auto& bit_two = bhav.instructions[1];
    const auto& bit_three = bhav.instructions[2];
    const auto exact_flag_test = [](const Instruction& instruction, std::uint16_t bit,
                                    std::uint8_t on_false) {
        return instruction.opcode == 2 && instruction.on_true == 0xfe &&
            instruction.on_false == on_false && word(instruction, 0) == 65 &&
            word(instruction, 2) == bit && instruction.operands[4] == 0 &&
            instruction.operands[5] == 8 && instruction.operands[6] == 19 &&
            instruction.operands[7] == 7;
    };
    // The call is Global 355, the audited "verify person in stack obj" routine.
    // Its exact behavior is to search the live avatar set for StackObjectID.
    return verify_person.opcode == 355 && verify_person.on_true == 1 &&
        verify_person.on_false == 0xff && verify_person.operands ==
            std::array<std::uint8_t, 8>{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff} &&
        exact_flag_test(bit_two, 2, 2) && exact_flag_test(bit_three, 3, 0xff);
}
} // namespace

ReadOnlyEntryPoint5Result evaluate_read_only_entry_point5(
    std::uint16_t routine_id, const Bhav& bhav, std::uint32_t stack_object_guid,
    std::optional<std::uint32_t> stack_object_master_guid,
    std::optional<std::int16_t> stack_object_attribute16,
    std::optional<std::uint16_t> stack_object_definition_word9,
    std::optional<std::int16_t> stack_object_person_data31,
    std::optional<std::int16_t> caller_attribute1,
    std::optional<bool> stack_object_is_live_avatar,
    std::optional<std::int16_t> stack_object_person_data65) {
    if (is_urnstone_shape(routine_id, bhav)) {
        if (!stack_object_definition_word9)
            return {ReadOnlyEntryPoint5State::unsupported, "stack_object_definition_word9_unavailable", 0};
        if (*stack_object_definition_word9 != 2)
            return {ReadOnlyEntryPoint5State::evaluated_false, "read_only_bhav_completed", 1};
        if (!stack_object_person_data31)
            return {ReadOnlyEntryPoint5State::unsupported, "stack_object_person_data31_unavailable", 1};
        if (!caller_attribute1)
            return {ReadOnlyEntryPoint5State::unsupported, "caller_attribute1_unavailable", 1};
        return {*stack_object_person_data31 == *caller_attribute1
                    ? ReadOnlyEntryPoint5State::evaluated_true
                    : ReadOnlyEntryPoint5State::evaluated_false,
                "read_only_bhav_completed", 2};
    }
    if (is_dining_table_shape(routine_id, bhav)) {
        if (!stack_object_is_live_avatar)
            return {ReadOnlyEntryPoint5State::unsupported, "stack_object_avatar_presence_unavailable", 1};
        if (!*stack_object_is_live_avatar)
            return {ReadOnlyEntryPoint5State::evaluated_false, "read_only_bhav_completed", 1};
        if (!stack_object_person_data65)
            return {ReadOnlyEntryPoint5State::unsupported, "stack_object_person_data65_unavailable", 1};
        const auto flags = static_cast<std::uint16_t>(*stack_object_person_data65);
        return {(flags & ((1U << 1) | (1U << 2))) != 0
                    ? ReadOnlyEntryPoint5State::evaluated_true
                    : ReadOnlyEntryPoint5State::evaluated_false,
                "read_only_bhav_completed", 3};
    }
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

ReadOnlyBhavResult evaluate_read_only_entry_point5_bhav(
    const WorldSnapshot& snapshot, const RoutineSet& default_routines,
    const RoutineSet& owner_routines, std::uint16_t action,
    std::int16_t caller, std::int16_t stack_object,
    std::optional<std::int16_t> ghost, std::uint64_t instruction_budget,
    ReadOnlyBhavLimits limits) {
    if (action < 4096 || action >= 8192 || caller <= 0 || stack_object <= 0 ||
        (ghost && *ghost != 0 && *ghost != 1))
        return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::invalid_resources, 0,
                "invalid private EP5 action or execution context"};
    const auto has_entity = [&](std::int16_t id) {
        return std::any_of(snapshot.entities.begin(), snapshot.entities.end(),
            [&](const Entity& entity) { return entity.id == id; });
    };
    if (!has_entity(caller) || !has_entity(stack_object))
        return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::invalid_snapshot, 0,
                "EP5 caller or stack object is absent from the World snapshot"};

    // This disposable owner label is not an entity ID. Private/semiglobal
    // lookup stays separate from the World identities in VmContext.
    constexpr CodeOwner resource_owner = 1;
    const RoutineOwners owners{{resource_owner, owner_routines}};
    const auto run = [&](std::int16_t ghost_flag) {
        const VmContext context{caller, caller, stack_object, resource_owner};
        const std::vector<std::int16_t> arguments{stack_object, ghost_flag, 0, 0};
        return evaluate_read_only_bhav(snapshot, default_routines, owners,
            routine_key(action), context, arguments, instruction_budget, limits);
    };
    if (ghost) return run(*ghost);

    const auto normal = run(0);
    const auto ghost_image = run(1);
    const auto total_steps = normal.steps > UINT64_MAX - ghost_image.steps
        ? UINT64_MAX : normal.steps + ghost_image.steps;
    if (normal.state == ReadOnlyBhavState::unsupported ||
        ghost_image.state == ReadOnlyBhavState::unsupported) {
        const auto& failed = normal.state == ReadOnlyBhavState::unsupported ? normal : ghost_image;
        return {ReadOnlyBhavState::unsupported, failed.reason, total_steps,
                "EP5 ghost argument is unknown; " + failed.diagnostic};
    }
    if (normal.state != ghost_image.state)
        return {ReadOnlyBhavState::unsupported, ReadOnlyBhavReason::did_not_complete, total_steps,
                "EP5 result changes with the unknown ghost argument"};
    return {normal.state, ReadOnlyBhavReason::completed, total_steps, {}};
}

} // namespace odh
