#include "odh/slot_scoring.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
constexpr std::uint32_t random_scoring = 8192;
constexpr std::uint32_t equal_proximity = 1U << 29;
void validate_position(RoutePoint p) {
    if (p.x < -32768 || p.x > 32767 || p.y < -32768 || p.y > 32767)
        throw std::invalid_argument("reference SLOT score position exceeds LotTilePos short coordinates");
}
std::int32_t distance(RoutePoint a, RoutePoint b) {
    const auto dx = static_cast<std::int64_t>(a.x) - b.x;
    const auto dy = static_cast<std::int64_t>(a.y) - b.y;
    const auto square = dx * dx + dy * dy;
    // The reference multiplies signed ints. Do not emulate overflow inputs.
    if (square > std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument("reference SLOT distance exceeds safe reference integer arithmetic");
    return static_cast<std::int32_t>(std::sqrt(static_cast<double>(square)));
}
}
ReferenceSlotScores score_reference_slot_candidates(
    const SlotTable& table, std::size_t type3_index, RoutePoint object_position,
    RoutePoint caller_position, const std::vector<ReferenceSlotScoreInput>& inputs,
    RandomState& random) {
    const auto* slot = slot_of_type(table, 3, type3_index);
    const bool common=table.version == 10 && slot &&
        slot->offset_bits == std::array<std::uint32_t,3>{0,0,0} && slot->standing == 1 &&
        slot->sitting == 0 && slot->ground == 0 && slot->snap_target_slot == -1 &&
        slot->max_size == 0 && slot->unknown_i10 == 0 && slot->gradient_bits == 0x3E400000 &&
        slot->height == 0 && slot->facing == -2 && slot->resolution == 16 && slot->proximity &&
        (*slot->proximity)[0] > 0 && (*slot->proximity)[1] >= (*slot->proximity)[0] &&
        (*slot->proximity)[2] >= (*slot->proximity)[0] && (*slot->proximity)[0] <= 4096 &&
        (*slot->proximity)[1] <= 4096;
    const bool circle=common &&
        (slot->flags & ~(random_scoring | equal_proximity)) == 0x55 &&
        (*slot->proximity)[0] == (*slot->proximity)[1] &&
        (*slot->proximity)[0] == (*slot->proximity)[2];
    const bool door_range=common &&
        (slot->flags & 0xFFU) == 0x54U &&
        (slot->flags & ~(random_scoring | equal_proximity)) == 0x54U &&
        *slot->proximity == std::array<std::int32_t,3>{16,32,16};
    if (!circle && !door_range)
        throw std::invalid_argument("reference SLOT scoring requires the House07 circle or Door 16-to-32 range profile");
    if (inputs.size() > 4096) throw std::length_error("reference SLOT scoring exceeds 4096 candidates");
    validate_position(object_position);
    validate_position(caller_position);
    const auto min_proximity = (*slot->proximity)[0];
    const auto max_proximity = (*slot->proximity)[1] == 0 ? min_proximity : (*slot->proximity)[1];
    const auto desired_proximity = (*slot->proximity)[2] == 0 ? min_proximity : (*slot->proximity)[2];
    // Both divisions are intentionally different: maxScore uses integer /3,
    // while VerifyAndAddLocation subtracts integer distance divided by 3.0.
    const double max_score = std::max(std::abs(desired_proximity-min_proximity),
        std::abs(max_proximity-desired_proximity)) +
        (distance(object_position, caller_position) + max_proximity) / 3 + 2;
    std::vector<std::int32_t> caller_distances;
    caller_distances.reserve(inputs.size());
    for (const auto& input : inputs) {
        validate_position(input.goal.position);
        const bool in_range=std::isfinite(input.slot_distance) && (circle
            ? input.slot_distance == min_proximity
            : input.slot_distance >= min_proximity-0.5 && input.slot_distance <= max_proximity+0.5);
        if (!in_range || input.goal.entry_flags == 0 ||
            (input.goal.entry_flags & ~static_cast<std::uint8_t>(slot->flags)) != 0 ||
            input.goal.facing > 7 ||
            (input.verification != ReferenceSlotVerification::eligible &&
             input.verification != ReferenceSlotVerification::eligible_with_penalty &&
             input.verification != ReferenceSlotVerification::rejected_after_score))
            throw std::invalid_argument("reference SLOT scoring requires known verification and valid pre-score metadata");
        caller_distances.push_back(distance(input.goal.position, caller_position));
    }
    ReferenceSlotScores result{{},{},{},random.seed(),random.seed(),0};
    result.records.reserve(inputs.size());
    result.ranked_goals.reserve(inputs.size());
    result.ranked_input_indices.reserve(inputs.size());
    auto next_random = random;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        const auto before = next_random.seed();
        double base = max_score;
        if ((slot->flags & equal_proximity) == 0) {
            base = static_cast<float>(next_random.next(1024)) / 1024.0f;
            ++result.random_calls;
            if ((slot->flags & random_scoring) == 0)
                base += max_score - std::abs(desired_proximity - input.slot_distance);
        }
        std::optional<double> score;
        if (input.verification != ReferenceSlotVerification::rejected_after_score) {
            score = base;
            if ((slot->flags & random_scoring) == 0) *score -= caller_distances[i] / 3.0;
            *score *= slot->standing;
            if (input.verification == ReferenceSlotVerification::eligible_with_penalty) *score /= 100000;
            result.ranked_input_indices.push_back(i);
        }
        result.records.push_back({base,score,input.verification,before,next_random.seed()});
    }
    std::stable_sort(result.ranked_input_indices.begin(), result.ranked_input_indices.end(),
        [&](std::size_t a, std::size_t b) { return *result.records[a].final_score > *result.records[b].final_score; });
    for (const auto i : result.ranked_input_indices) result.ranked_goals.push_back(inputs[i].goal);
    result.seed_after = next_random.seed();
    random = next_random;
    return result;
}
} // namespace odh
