#pragma once

#include <cstdint>

namespace odh {

// One gate's answer for a proposed avatar position. These gates describe only
// whether the candidate cell itself is usable; they do not establish a route.
enum class CandidateCellGateStatus : std::uint8_t {
    allowed,
    blocked,
    unsupported,
};

struct AvatarCandidatePreflightInput {
    CandidateCellGateStatus floor = CandidateCellGateStatus::unsupported;
    CandidateCellGateStatus entity_overlap = CandidateCellGateStatus::unsupported;
    CandidateCellGateStatus attempt_walk_snapshot = CandidateCellGateStatus::unsupported;
    CandidateCellGateStatus candidate_cell = CandidateCellGateStatus::unsupported;
};

enum class AvatarCandidatePreflightStatus : std::uint8_t {
    clear,
    blocked,
    unsupported,
};

struct AvatarCandidatePreflightResult {
    AvatarCandidatePreflightStatus status = AvatarCandidatePreflightStatus::unsupported;
    std::uint8_t blocked_gates = 0;
    std::uint8_t unsupported_gates = 0;
};

// A candidate is clear only when every independent gate is known and allows
// it. A known blocker wins over unknown gates because the position is already
// unusable; otherwise any unknown leaves the combined answer unsupported.
AvatarCandidatePreflightResult evaluate_avatar_candidate_preflight(
    const AvatarCandidatePreflightInput&) noexcept;

const char* candidate_cell_gate_status_name(CandidateCellGateStatus) noexcept;
const char* avatar_candidate_preflight_status_name(AvatarCandidatePreflightStatus) noexcept;

} // namespace odh
