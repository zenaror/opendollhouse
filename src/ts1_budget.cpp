#include "odh/ts1_budget.hpp"

#include <cstdint>
#include <limits>

namespace odh {
namespace {
std::optional<std::int16_t> read_amount_word(const VmFrame& frame,
        const std::array<std::int16_t, 20>& temps, const VmMemory& memory,
        std::uint8_t scope, std::int16_t index) {
    switch (scope) {
    case 7: return index; // Literal
    case 8:
        if (index < 0 || static_cast<std::size_t>(index) >= temps.size()) return {};
        return temps[static_cast<std::size_t>(index)];
    case 9:
        if (index < 0 || static_cast<std::size_t>(index) >= frame.arguments.size()) return {};
        return frame.arguments[static_cast<std::size_t>(index)];
    case 10: return frame.context.stack_object; // StackObjectID
    case 25:
        if (index < 0 || static_cast<std::size_t>(index) >= frame.locals.size()) return {};
        return frame.locals[static_cast<std::size_t>(index)];
    case 42: return {}; // TempXL is 32-bit and is not yet represented by Vm.
    default:
        if (!memory.read) return {};
        return memory.read(frame, scope, index);
    }
}

std::int32_t wrap_subtract(std::int32_t left, std::int32_t right) {
    const auto bits = static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right);
    if (bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return static_cast<std::int32_t>(bits);
    return static_cast<std::int32_t>(static_cast<std::int64_t>(bits) - 0x100000000LL);
}
} // namespace

VmPrimitive ts1_budget_primitive(const VmMemory& memory,
        Ts1FamilyBudgetReader read_budget, Ts1FamilyBudgetWriter write_budget) {
    return [&memory, read_budget = std::move(read_budget),
            write_budget = std::move(write_budget)](const VmFrame& frame,
            const Instruction& instruction, const std::array<std::int16_t, 20>& temps,
            const VmDispatch&) -> PrimitiveOutcome {
        if (instruction.operands.size() != 8)
            return {PrimitiveDisposition::unsupported, "TS1 Budget requires its 8-byte operand"};

        const auto& operand = instruction.operands;
        const auto old_owner = operand[0];
        std::uint8_t amount_scope = operand[1];
        if (old_owner == 0) amount_scope = 7; // Literal
        else if (old_owner == 1) amount_scope = 9; // Parameters
        else if (old_owner == 2) amount_scope = 25; // Local
        else if (old_owner != 3)
            return {PrimitiveDisposition::unsupported, "TS1 Budget amount-owner value is unsupported"};

        const auto raw_index = static_cast<std::uint16_t>(operand[2]) |
            (static_cast<std::uint16_t>(operand[3]) << 8);
        const auto amount_word = read_amount_word(frame, temps, memory, amount_scope,
            static_cast<std::int16_t>(raw_index));
        if (!amount_word)
            return {PrimitiveDisposition::unsupported, "TS1 Budget amount source is unavailable"};

        const auto flags = operand[4];
        std::int32_t amount = *amount_word;
        if ((flags & 0x02U) != 0) amount = -amount; // TS1 names this flag Subtract; it adds.

        const auto current = read_budget ? read_budget(frame) : std::nullopt;
        const std::int32_t old_budget = current.value_or(0);
        const std::int32_t new_budget = wrap_subtract(old_budget, amount);
        if (old_budget < 0) return {PrimitiveDisposition::goto_false, {}};
        if ((flags & 0x01U) == 0 && current) {
            if (!write_budget || !write_budget(frame, new_budget))
                return {PrimitiveDisposition::unsupported,
                    "TS1 Budget family change has no successful persistent writer"};
        }
        return {PrimitiveDisposition::goto_true, {}};
    };
}

} // namespace odh
