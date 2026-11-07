// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"
#include "odh/world.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace odh {
struct HouseLayoutPair {std::int32_t samples=0,routing=0;};
struct HouseLayoutHistory {
    std::array<HouseLayoutPair,5> past{}; // Most recent first, original world+38.
    HouseLayoutPair current;
    // Native lifecycle metadata, not part of the original twelve HOUS words.
    std::optional<std::uint64_t> last_sample_tick;
};
// Pass the payload of the authenticated HOUS id1, never HOUS id0 or SIMI.
// Version>=26(hex) owns a real persisted history; old/absent formats are absent.
// Rejects malformed/truncated data instead of synthesizing history.
std::optional<HouseLayoutHistory> read_house_layout_history(const View&);
// Explicit original NEW-history policy (4165e9), not a missing-save fallback.
HouseLayoutHistory new_house_layout_history(std::int32_t authenticated_fill);
// Caller certifies complete live-entity membership and returns actual active
// routing-stack state, not queued/planned walking or a failed route count.
// Source45cdf8: every10 ticks, PersonData32==0, Hidden34==0, SleepState11>=0.
// False on unknown required input/overflow/duplicate or backward sample tick;
// candidate publication is atomic. Non-sample ticks do nothing and return true.
bool sample_house_layout_history(HouseLayoutHistory&,const World&,std::uint64_t tick,
    const std::function<std::optional<bool>(WorldEntityReference)>& routing_active);
// Preserves original forward overlapping copy (old past[0] propagates to1..4).
// Dispatch at the daily midnight boundary (original event0xdc), including
// explicit source day-change events. Paused wall-clock frames never rotate.
void rotate_house_layout_history(HouseLayoutHistory&);

bool house_layout_history_valid(const HouseLayoutHistory&) noexcept;
} // namespace odh
