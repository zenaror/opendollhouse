#include "odh/avatar_candidate_preflight.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using odh::AvatarCandidatePreflightInput;
    using odh::AvatarCandidatePreflightStatus;
    using odh::CandidateCellGateStatus;
    const std::array<CandidateCellGateStatus, 3> values = {
        CandidateCellGateStatus::allowed,
        CandidateCellGateStatus::blocked,
        CandidateCellGateStatus::unsupported,
    };

    std::size_t combinations = 0;
    for (const auto floor : values) {
        for (const auto overlap : values) {
            for (const auto snapshot : values) {
                for (const auto cell : values) {
                    const AvatarCandidatePreflightInput input{floor, overlap, snapshot, cell};
                    const auto actual = odh::evaluate_avatar_candidate_preflight(input);
                    const auto expected =
                        floor == CandidateCellGateStatus::blocked || overlap == CandidateCellGateStatus::blocked ||
                        snapshot == CandidateCellGateStatus::blocked || cell == CandidateCellGateStatus::blocked
                            ? AvatarCandidatePreflightStatus::blocked
                            : floor == CandidateCellGateStatus::unsupported || overlap == CandidateCellGateStatus::unsupported ||
                              snapshot == CandidateCellGateStatus::unsupported || cell == CandidateCellGateStatus::unsupported
                                ? AvatarCandidatePreflightStatus::unsupported
                                : AvatarCandidatePreflightStatus::clear;
                    require(actual.status == expected, "combined result disagrees with four-gate truth table");
                    const auto expected_blocked = static_cast<std::uint8_t>(
                        (floor == CandidateCellGateStatus::blocked) +
                        (overlap == CandidateCellGateStatus::blocked) +
                        (snapshot == CandidateCellGateStatus::blocked) +
                        (cell == CandidateCellGateStatus::blocked));
                    const auto expected_unsupported = static_cast<std::uint8_t>(
                        (floor == CandidateCellGateStatus::unsupported) +
                        (overlap == CandidateCellGateStatus::unsupported) +
                        (snapshot == CandidateCellGateStatus::unsupported) +
                        (cell == CandidateCellGateStatus::unsupported));
                    require(actual.blocked_gates == expected_blocked,
                            "blocked gate count disagrees with truth-table input");
                    require(actual.unsupported_gates == expected_unsupported,
                            "unsupported gate count disagrees with truth-table input");
                    ++combinations;
                }
            }
        }
    }
    require(combinations == 81, "truth table did not cover all four-gate combinations");

    const auto clear = odh::evaluate_avatar_candidate_preflight({
        CandidateCellGateStatus::allowed, CandidateCellGateStatus::allowed,
        CandidateCellGateStatus::allowed, CandidateCellGateStatus::allowed});
    require(clear.status == AvatarCandidatePreflightStatus::clear &&
            clear.blocked_gates == 0 && clear.unsupported_gates == 0,
            "all-known allowed gates should produce clear");

    const auto known_blocker = odh::evaluate_avatar_candidate_preflight({
        CandidateCellGateStatus::unsupported, CandidateCellGateStatus::blocked,
        CandidateCellGateStatus::unsupported, CandidateCellGateStatus::allowed});
    require(known_blocker.status == AvatarCandidatePreflightStatus::blocked &&
            known_blocker.blocked_gates == 1 && known_blocker.unsupported_gates == 2,
            "known blocker should decide blocked while preserving unknown counts");

    require(std::string(odh::avatar_candidate_preflight_status_name(AvatarCandidatePreflightStatus::clear)) == "clear",
            "status name should be stable for JSON output");
    std::cout << "avatar candidate preflight: " << combinations << " combinations passed\n";
}
