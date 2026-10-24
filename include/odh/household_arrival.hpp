#pragma once

#include "odh/avatar_candidate_preflight.hpp"
#include "odh/household_spawn.hpp"
#include <string>

namespace odh {

struct HouseholdArrivalAssessment {
    AvatarCandidatePreflightInput cell;
    // Separate from cell clearance: the host must establish that applying the
    // position has supported PositionChange/entry9 semantics for this profile.
    CandidateCellGateStatus position_change = CandidateCellGateStatus::unsupported;
};
using HouseholdArrivalAssessor = std::function<HouseholdArrivalAssessment(
    const World&, std::int16_t, const EntityPosition&)>;
struct HouseholdArrivalPreflight {
    AvatarCandidatePreflightStatus status = AvatarCandidatePreflightStatus::unsupported;
    std::optional<EntityPosition> position;
    std::optional<std::int16_t> mailbox_id;
    std::size_t attempts = 0;
    std::string diagnostic;
};

// Read-only planning for an unpositioned staged avatar. Requires exactly one
// positioned, non-ghost mailbox of a supported exact GUID. Unknown assessment
// stops the search: skipping it would not establish the first valid arrival.
// The assessor must not mutate the World, resources, RNG, or controllers.
// A clear result records caller-supplied evidence only; it does not execute
// PositionChange, place an avatar, start Main, publish a World or permit saving.
HouseholdArrivalPreflight preflight_staged_household_arrival(
    const World&, std::int16_t avatar_id, std::uint16_t lot_width,
    std::uint16_t lot_height, const HouseholdArrivalAssessor&);

} // namespace odh
