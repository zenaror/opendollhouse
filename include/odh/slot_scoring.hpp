#pragma once
#include "odh/random.hpp"
#include "odh/routing.hpp"

namespace odh {
// Caller-supplied result of post-score verification. Unknown is deliberately
// invalid input, never an eligible goal. No geometry/PositionValid is run here.
enum class ReferenceSlotVerification : std::uint8_t {
    unknown, eligible, eligible_with_penalty, rejected_after_score
};
struct ReferenceSlotScoreInput {
    PreRankedSlotGoal goal;
    double slot_distance; // measured by the upstream enumeration, in 1/16 tiles
    ReferenceSlotVerification verification = ReferenceSlotVerification::unknown;
};
struct ReferenceSlotScoreRecord {
    double base_score;
    // Rejected records may have failed before distance/weight adjustment.
    std::optional<double> final_score;
    ReferenceSlotVerification verification;
    std::uint64_t seed_before, seed_after;
};
struct ReferenceSlotScores {
    std::vector<ReferenceSlotScoreRecord> records; // input order, including rejects
    std::vector<PreRankedSlotGoal> ranked_goals; // feeds an explicit provider
    std::vector<std::size_t> ranked_input_indices;
    std::uint64_t seed_before, seed_after;
    std::size_t random_calls;
};
// Bounded Simitone-reference arithmetic for the House07 v10 type-3 circle and
// Door 16-to-32 ranged profiles, optionally with RandomScoring /
// FSOEqualProximityScore. Inputs must already have passed proximity, room and
// direction gates, in reference enumeration order.
// Does not enumerate, resolve facing, verify eligibility or establish TS1
// executable equivalence. Rejects still consume RNG; malformed input leaves
// the explicit RNG unchanged. No runtime/default seed or Main wiring implied.
ReferenceSlotScores score_reference_slot_candidates(
    const SlotTable&, std::size_t type3_index, RoutePoint object_position,
    RoutePoint caller_position, const std::vector<ReferenceSlotScoreInput>&,
    RandomState&);
// Generates, filters by ranged proximity/direction, scores and stably ranks the
// pinned Simitone Door 16-to-32 profile. Room and clearance eligibility remain
// the Routing host's responsibility; the supplied RNG is explicit and mutable.
std::vector<PreRankedSlotGoal> rank_reference_door_range_slot(
    const SlotTable&,std::size_t type3_index,const EntityPosition& caller,
    const EntityPosition& target,RandomState&);
} // namespace odh
