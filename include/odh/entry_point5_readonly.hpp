#pragma once

#include "odh/formats.hpp"

#include <cstdint>
#include <optional>

namespace odh {

enum class ReadOnlyEntryPoint5State : std::uint8_t { evaluated_true, evaluated_false, unsupported };

struct ReadOnlyEntryPoint5Result {
    ReadOnlyEntryPoint5State state = ReadOnlyEntryPoint5State::unsupported;
    const char* reason = "invalid_input";
    std::uint64_t steps = 0;
};

// Executes only the audited two-instruction TS1 BHAV shape used by House07's
// flamingo EP5 (TestObjectType on StackObject, then read-only equality of
// StackObjectAttributes[16] with literal 1). All other code fails closed.
ReadOnlyEntryPoint5Result evaluate_read_only_entry_point5(
    std::uint16_t routine_id, const Bhav&,
    std::uint32_t stack_object_guid,
    std::optional<std::uint32_t> stack_object_master_guid,
    std::optional<std::int16_t> stack_object_attribute16,
    std::optional<std::uint16_t> stack_object_definition_word9 = {},
    std::optional<std::int16_t> stack_object_person_data31 = {},
    std::optional<std::int16_t> caller_attribute1 = {},
    std::optional<bool> stack_object_is_live_avatar = {},
    std::optional<std::int16_t> stack_object_person_data65 = {});

const char* read_only_entry_point5_state_name(ReadOnlyEntryPoint5State) noexcept;

} // namespace odh
