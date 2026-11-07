#include "odh/distance_to.hpp"
#include "odh/ts1_variable.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace odh {
namespace {


std::int16_t signed_word(std::int32_t value) {
    const auto low = static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xffffU);
    const auto signed_value = low < 32768U ? static_cast<std::int32_t>(low)
                                           : static_cast<std::int32_t>(low) - 65536;
    return static_cast<std::int16_t>(signed_value);
}

struct SourcePosition {
    std::int16_t x;
    std::int16_t y;
    std::int16_t level;
};

std::optional<SourcePosition> source_position(const Entity& entity) {
    // VMEntity initializes Position to LotTilePos.OUT_OF_WORLD (-32768,-32768,1).
    // World represents that state with an absent position.
    if (!entity.position) return SourcePosition{-32768, -32768, 1};
    const auto& position = *entity.position;
    if (position.x < -32768 || position.x > 32767 ||
        position.y < -32768 || position.y > 32767 ||
        position.level < -128 || position.level > 127) return {};
    return SourcePosition{static_cast<std::int16_t>(position.x),
                          static_cast<std::int16_t>(position.y),
                          position.level};
}

std::int16_t distance_to(const SourcePosition& first, const SourcePosition& second) {
    const auto dx = static_cast<double>(first.x) - static_cast<double>(second.x);
    const auto dy = static_cast<double>(first.y) - static_cast<double>(second.y);
    // The source performs Math.Sqrt(Math.Pow(dx, 2) + Math.Pow(dy, 2)) / 16.0,
    // then floors and narrows to Int16.
    const auto planar = static_cast<std::int32_t>(std::floor(std::sqrt(dx * dx + dy * dy) / 16.0));
    const auto level_difference = std::abs(static_cast<std::int32_t>(second.level) -
                                           static_cast<std::int32_t>(first.level));
    const auto result = std::max(20 * level_difference,
                                 planar + 5 * level_difference);
    return signed_word(result);
}

std::int16_t read_i16_le(const std::array<std::uint8_t, 8>& bytes, std::size_t offset) {
    const auto raw = static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    const auto signed_value = raw < 32768U ? static_cast<std::int32_t>(raw)
                                           : static_cast<std::int32_t>(raw) - 65536;
    return static_cast<std::int16_t>(signed_value);
}

} // namespace

VmPrimitive get_distance_to_primitive(const World& world, VmMemory memory) {
    return [&world, memory = std::move(memory)](
        const VmFrame& frame, const Instruction& instruction,
        const Ts1Temps& temps, const VmDispatch&) -> PrimitiveOutcome {
        if (instruction.opcode != 11) return {};
        const auto& operand = instruction.operands;
        const auto temp_number = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(operand[0]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(operand[1]) << 8));
        if (temp_number >= temps.size())
            return {PrimitiveDisposition::unsupported,
                    "GetDistanceTo TempNum is outside the VM's twenty-register bank"};

        auto object_scope = operand[3];
        auto object_data = read_i16_le(operand, 4);
        // VMGetDistanceToOperand.Read rewrites every even Flags value to
        // MyObject[11], and then sets bit0. Odd Flags values use Scope/Data.
        if ((operand[2] & 1U) == 0) {
            object_scope = 3;
            object_data = 11;
        }
        const auto target_id = read_ts1_variable(world, memory, frame, temps, object_scope, object_data);
        if (!target_id)
            return {PrimitiveDisposition::unsupported,
                    "GetDistanceTo target variable is unavailable in the supplied VM memory"};
        const auto* stack_object = world.frame_entity(frame,VmEntityRole::stack_object);
        const auto* target = world.find(*target_id);
        if (!stack_object || !target)
            return {PrimitiveDisposition::unsupported,
                    "GetDistanceTo StackObject or selected target is unavailable"};
        const auto stack_position = source_position(*stack_object);
        const auto target_position = source_position(*target);
        if (!stack_position || !target_position)
            return {PrimitiveDisposition::unsupported,
                    "GetDistanceTo position is outside the source LotTilePos coordinate range"};

        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true, {}};
        outcome.temp_write = VmTempWrite{
            static_cast<std::size_t>(temp_number), distance_to(*stack_position, *target_position)};
        return outcome;
    };
}

} // namespace odh
