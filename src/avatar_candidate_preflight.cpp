#include "odh/avatar_candidate_preflight.hpp"

namespace odh {

AvatarCandidatePreflightResult evaluate_avatar_candidate_preflight(
    const AvatarCandidatePreflightInput& input) noexcept {
    AvatarCandidatePreflightResult result;
    const CandidateCellGateStatus gates[] = {
        input.floor,
        input.entity_overlap,
        input.attempt_walk_snapshot,
        input.candidate_cell,
    };
    for (const auto gate : gates) {
        if (gate == CandidateCellGateStatus::blocked) ++result.blocked_gates;
        else if (gate != CandidateCellGateStatus::allowed) ++result.unsupported_gates;
    }
    if (result.blocked_gates != 0) result.status = AvatarCandidatePreflightStatus::blocked;
    else if (result.unsupported_gates != 0) result.status = AvatarCandidatePreflightStatus::unsupported;
    else result.status = AvatarCandidatePreflightStatus::clear;
    return result;
}

const char* candidate_cell_gate_status_name(CandidateCellGateStatus status) noexcept {
    switch (status) {
    case CandidateCellGateStatus::allowed: return "allowed";
    case CandidateCellGateStatus::blocked: return "blocked";
    case CandidateCellGateStatus::unsupported: return "unsupported";
    }
    return "unsupported";
}

const char* avatar_candidate_preflight_status_name(AvatarCandidatePreflightStatus status) noexcept {
    switch (status) {
    case AvatarCandidatePreflightStatus::clear: return "clear";
    case AvatarCandidatePreflightStatus::blocked: return "blocked";
    case AvatarCandidatePreflightStatus::unsupported: return "unsupported";
    }
    return "unsupported";
}

} // namespace odh
