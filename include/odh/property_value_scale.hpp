#pragma once
#include <cstdint>
#include <optional>

namespace odh {
enum class PropertyValueScaleReason { none, nonfinite_multiplier, out_of_range };
struct PropertyValueScaleResult {
    std::optional<std::int32_t> value;
    PropertyValueScaleReason reason=PropertyValueScaleReason::out_of_range;
};
// Explicit comparison profile: exact FILD int32 input, binary32 multiplier,
// multiplication rounded to24 significand bits nearest/ties-even, then signed
// truncation toward zero. Pure integer implementation; no host fenv dependency.
// The comparison application establishes this intended PC24/nearest context,
// but its dominance over EVERY original valuation call path is not proved.
// Calling this API explicitly selects that precondition. It does not choose
// the original policy implicitly, add assets, calculate refunds or mutate state.
// Rejects nonfinite multipliers and results outside supported signed32 range.
PropertyValueScaleResult comparison_pc24_truncate_scale(std::int32_t input,
    std::uint32_t multiplier_bits) noexcept;
const char* property_value_scale_reason_name(PropertyValueScaleReason) noexcept;
} // namespace odh
