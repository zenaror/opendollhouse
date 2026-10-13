#include "odh/random.hpp"
#include <utility>

namespace odh {
std::uint64_t RandomState::next(std::uint64_t range) {
    if (range == 0) return 0;
    seed_ ^= seed_ >> 12;
    seed_ ^= seed_ << 25;
    seed_ ^= seed_ >> 27;
    return (seed_ * UINT64_C(2685821657736338717)) % range;
}
namespace {
std::uint16_t word(const Instruction& instruction, std::size_t offset) {
    return static_cast<std::uint16_t>(instruction.operands[offset] |
        (static_cast<std::uint16_t>(instruction.operands[offset + 1]) << 8));
}
std::int16_t signed_word(std::uint16_t bits) {
    return static_cast<std::int16_t>(bits < 32768 ? static_cast<std::int32_t>(bits) :
                                                 static_cast<std::int32_t>(bits) - 65536);
}
bool in_bank(std::int16_t index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}
std::optional<std::int16_t> read(const VmFrame& frame, const std::array<std::int16_t,20>& temps,
                               const VmMemory& memory, std::uint8_t scope, std::int16_t index) {
    switch (scope) {
    case 7: return index;
    case 8: return in_bank(index, temps.size()) ? std::optional<std::int16_t>{temps[static_cast<std::size_t>(index)]} : std::nullopt;
    case 9: return in_bank(index, frame.arguments.size()) ? std::optional<std::int16_t>{frame.arguments[static_cast<std::size_t>(index)]} : std::nullopt;
    case 10: return frame.context.stack_object;
    case 25: return in_bank(index, frame.locals.size()) ? std::optional<std::int16_t>{frame.locals[static_cast<std::size_t>(index)]} : std::nullopt;
    default: return memory.read ? memory.read(frame, scope, index) : std::nullopt;
    }
}
PrimitiveOutcome reject(const char* reason) { return {PrimitiveDisposition::unsupported, reason}; }
}
VmPrimitive random_primitive(RandomState& state, VmMemory memory, RandomObserver observer) {
    return [&state, memory = std::move(memory), observer = std::move(observer)](
        const VmFrame& frame, const Instruction& instruction,
        const std::array<std::int16_t,20>& temps, const VmDispatch&) -> PrimitiveOutcome {
        if (instruction.opcode != 8) return {};
        const auto destination_scope_word = word(instruction, 2);
        const auto range_scope_word = word(instruction, 6);
        if (destination_scope_word > 255 || range_scope_word > 255)
            return reject("Random Number scope exceeds byte-sized VM scope");
        const auto destination_scope = static_cast<std::uint8_t>(destination_scope_word);
        const auto range_scope = static_cast<std::uint8_t>(range_scope_word);
        const auto destination_data = signed_word(word(instruction, 0));
        const auto range_data = signed_word(word(instruction, 4));
        const auto range_value = read(frame, temps, memory, range_scope, range_data);
        if (!range_value) return reject("Random Number range is unavailable or outside its bank");
        switch (destination_scope) {
        case 7: case 10: return reject("Random Number destination has no supported output");
        case 8:
            if (!in_bank(destination_data, temps.size())) return reject("Random Number temp destination outside bank");
            break;
        case 9:
            if (!in_bank(destination_data, frame.arguments.size())) return reject("Random Number argument destination outside bank");
            break;
        case 25:
            if (!in_bank(destination_data, frame.locals.size())) return reject("Random Number local destination outside bank");
            break;
        default:
            if (!memory.write) return reject("Random Number external destination is unbound");
        }
        const auto seed_before = state.seed();
        auto candidate = state;
        const auto range = static_cast<std::uint16_t>(*range_value);
        const auto result = signed_word(static_cast<std::uint16_t>(candidate.next(range)));
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true, {}};
        switch (destination_scope) {
        case 8: outcome.temp_write = VmTempWrite{static_cast<std::size_t>(destination_data), result}; break;
        case 9: outcome.write = VmFrameWrite{VmFrameBank::arguments, static_cast<std::size_t>(destination_data), result}; break;
        case 25: outcome.write = VmFrameWrite{VmFrameBank::locals, static_cast<std::size_t>(destination_data), result}; break;
        default:
            if (!memory.write(frame, destination_scope, destination_data, result))
                return reject("Random Number external destination rejected write");
        }
        state = candidate;
        if (observer) observer(RandomRecord{frame.context, range, result, destination_scope,
                                            destination_data, seed_before, state.seed()});
        return outcome;
    };
}
} // namespace odh
