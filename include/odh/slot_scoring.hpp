#pragma once
#include "odh/random.hpp"
#include "odh/routing.hpp"
#include <functional>

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
// Explicit result of the source room lookup. Unknown is not treated as a
// room mismatch or as permission to continue.
enum class SourceSlotRoomResult : std::uint8_t { same_room, different_room, unknown };
// Semantic eligibility for the generic VMSlotParser range branch. This is
// deliberately independent of resource GUID and type-3 index.
bool is_source_standing_range_slot(const SlotRecord&);
// Generic standing range enumeration/scoring for zero-offset FaceTowards,
// FaceAnywhere or authored fixed-facing0..7 slots. Room lookup occurs before direction/scoring unless IgnoreRooms is
// set; candidate verification occurs after that candidate's score draw.
// The room callback compares against the target's room captured before the
// scan. Required callbacks preserve unknown room/placement evidence explicitly.
// The caller/target positions and target angle are frozen inputs for this
// bounded helper: adapters must report unknown (after preserving effects) if a
// placement check changes those transforms during the scan. The pinned source
// re-reads some transforms per candidate; live-transform mutation is not
// represented by this API.
std::vector<PreRankedSlotGoal> rank_source_standing_range_slot(
    const SlotTable&,std::size_t type3_index,const EntityPosition& caller,
    const EntityPosition& target,float target_radians,RandomState&,
    const std::function<SourceSlotRoomResult(RoutePoint)>& candidate_room,
    const std::function<ReferenceSlotVerification(RoutePoint)>& candidate_verification);
// Exact Legacy Collection Global SLOT100 v10/type-3/index-10 profile used by
// BHAV 8534. Its Sitting=10 preference is resolved per candidate.
bool is_child_shy_sitting_fallback_slot(const SlotTable&, std::size_t type3_index);
// Conservative inventory gate: every positioned object on the target floor
// must have a complete EntryPoint26 table before per-candidate footprint checks.
// Entities without a position are the game's OUT_OF_WORLD objects and cannot
// be found by Simitone's FindBestObjectForFunction scan.
std::optional<std::string> child_shy_sitting_candidate_inventory_preflight(
    const World&,const SlotTable&,std::size_t type3_index,const EntityPosition& target);
// Bounded Simitone-reference arithmetic for the House07 v10 type-3 circle,
// Door 16-to-32 ranged profiles and Global 16-to-64 randomized profile, with
// optional RandomScoring /
// FSOEqualProximityScore. Inputs must already have passed proximity, room and
// direction gates, in reference enumeration order.
// Does not enumerate, resolve facing, verify eligibility or establish TS1
// executable equivalence. Rejects still consume RNG; malformed input leaves
// the explicit RNG unchanged. No runtime/default seed or Main wiring implied.
ReferenceSlotScores score_reference_slot_candidates(
    const SlotTable&, std::size_t type3_index, RoutePoint object_position,
    RoutePoint caller_position, const std::vector<ReferenceSlotScoreInput>&,
    RandomState&, bool no_sitting_target_proven = false,
    bool sitting_candidates_resolved = false);
// Generates, filters by range/direction and room before scoring, then consumes
// score RNG before applying position-validity checks, matching the observed
// Simitone phase order. Routing revalidates selected goals.
std::vector<PreRankedSlotGoal> rank_reference_range_slot(
    const SlotTable&,std::size_t type3_index,const EntityPosition& caller,
    const EntityPosition& target,RandomState&,bool no_sitting_target_proven = false,
    const std::function<bool(RoutePoint)>& candidate_in_room = {},
    const std::function<bool(RoutePoint)>& candidate_position_valid = {},
    const std::function<std::optional<std::int16_t>(RoutePoint,std::uint8_t)>& candidate_sitting_object = {},
    const std::function<std::optional<RoutePoint>(RoutePoint)>& candidate_sitting_approach = {},
    std::optional<float> target_radians = std::nullopt,
    const std::function<ReferenceSlotVerification(RoutePoint)>& candidate_verification = {});
} // namespace odh
