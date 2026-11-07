// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "odh/vm.hpp"
#include "odh/world.hpp"

namespace odh {
namespace variable_detail {
inline std::optional<std::int16_t> indexed_value(
    const std::vector<std::int16_t>& values, std::int16_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= values.size()) return {};
    return values[static_cast<std::size_t>(index)];
}

inline std::optional<std::int16_t> temp_value(
    const Ts1Temps& temps, std::int16_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= temps.size()) return {};
    return temps[static_cast<std::size_t>(index)];
}

inline std::optional<std::int16_t> read_memory_scope(
    const World& world, const VmMemory& memory, const VmFrame& frame,
    std::uint8_t scope, std::int16_t index) {
    if (memory.read) return memory.read(frame, scope, index);
    return world.read(frame, scope, index);
}

} // namespace variable_detail

// Pinned FreeSO VMMemory.GetVariable (the short-word path, not GetVariableBig).
// This mirrors the VM's variable read for scopes whose selector itself uses a
// BHAV local, argument, or Temp. Entity-backed and host-owned scopes continue
// through the exact memory backend supplied by the caller.
inline std::optional<std::int16_t> read_ts1_variable(
    const World& world, const VmMemory& memory, const VmFrame& frame,
    const Ts1Temps& temps, std::uint8_t scope, std::int16_t index) {
    const auto resolved=resolve_ts1_variable_address(scope,index,frame,temps);
    if(!resolved)return {};
    scope=resolved->scope;index=resolved->index;
    switch (scope) {
    case 7: // Literal
        return index;
    case 8: // Temps
        return variable_detail::temp_value(temps, index);
    case 9: // Parameters
        return variable_detail::indexed_value(frame.arguments, index);
    case 10: // StackObjectID
        return frame.context.stack_object;
    case 11: { // TempByTemp
        const auto selector = variable_detail::temp_value(temps, index);
        return selector ? variable_detail::temp_value(temps, *selector) : std::nullopt;
    }
    case 25: // Local
        return variable_detail::indexed_value(frame.locals, index);
    case 27: { // DynSpriteFlagForTempOfStackObject
        const auto selector = variable_detail::temp_value(temps, index);
        if (!selector) return {};
        if (memory.dynamic_sprite_flag) {
            const auto value = memory.dynamic_sprite_flag(
                frame, static_cast<std::uint16_t>(*selector));
            if (value) return static_cast<std::int16_t>(*value ? 1 : 0);
        }
        return variable_detail::read_memory_scope(world, memory, frame, scope, index);
    }
    case 34: // NeighborhoodData returns zero in the pinned implementation.
    case 48: // MoneyOverHead32Bit returns zero in the pinned implementation.
        return std::int16_t{0};
    case 33: // JobData uses this reading thread's Temp0/Temp1, not entity data.
        return memory.job_data?memory.job_data(frame,static_cast<std::uint16_t>(temps[0]),temps[1],index):
            std::nullopt;
    case 54: // FeatureEnableLevel is always enabled in the pinned implementation.
        return std::int16_t{1};
    default:
        break;
    }

    // Pinned VMMemory.GetVariable explicitly throws for TempXL. Only the
    // expression-specific GetVariableBig path handles that bank; this opcode
    // uses GetVariable and therefore retains the source fault boundary.
    if (scope == 42) return {};

    if (scope == 23 && memory.room_by_temp0) {
        const auto value = memory.room_by_temp0(frame, temps[0], index);
        if (value) return value;
    }
    const auto address = resolve_person_data_address(scope, index, temps);
    return address ? variable_detail::read_memory_scope(world, memory, frame, address->scope, address->index)
                   : std::nullopt;
}

} // namespace odh
