#pragma once
#include "odh/world.hpp"
#include <array>
#include <functional>
#include <string>

namespace odh {
struct SpecialEffectOperands {
    std::uint16_t timeout = 0;
    std::int8_t size = 0, zoom = 0, flags = 0, string_index = 0;
    std::array<std::uint8_t, 2> unused{};
};
SpecialEffectOperands parse_special_effect_operands(const std::array<std::uint8_t, 8>&);
struct SpecialEffectFocusRequest {
    WorldEntityReference target;
    SpecialEffectOperands operands;
    // Valid only synchronously inside center_to. The backend must use the exact
    // target's actual visual position, never a replacement with the same ID.
    std::function<bool()> source_still_matches;
};
struct SpecialEffectFocusResult { bool completed = false; std::string diagnostic; };
struct SpecialEffectServices {
    bool use_world = false;
    std::function<SpecialEffectFocusResult(const SpecialEffectFocusRequest&)> center_to;
};
// Pinned TS1 reference semantics: only flag bit3 requests World.CenterTo.
// The other fields/flags are parsed but have no simulated effects in that source.
// Original PC effects beyond this reference remain an investigation gap.
VmPrimitive special_effect_primitive(const World&, SpecialEffectServices = {});
}
