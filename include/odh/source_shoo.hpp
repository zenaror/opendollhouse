// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/vm.hpp"
#include <optional>
#include <string>
#include <variant>

namespace odh {
// Pure source VMRoutingFrame/GetAction planning. The host authenticates live
// participants, construction provenance and installed owner identity separately.
enum class SourceShooMode { direct_route, queued_interaction };
enum class SourceShooPlanStatus { ready, no_action, unsupported };
struct SourceShooPlanInput {
    SourceShooMode mode=SourceShooMode::direct_route;
    std::int16_t actor=0,destination=0;
    CodeOwner owner=0;
    // Complete exact resource banks; null means unknown, never known absent.
    const VmProgram* owner_program=nullptr;
    const VmProgram* global_program=nullptr;
    const Ttab* interactions=nullptr;
};
struct SourceShooDirectPlan { VmCall call; };
struct SourceShooQueuedPlan {
    RoutineKey action=routine_key(0);
    VmContext context;
    std::optional<RoutineKey> check;
    std::optional<std::vector<std::int16_t>> source_arguments; // null: exactly four zeros at dispatch
    std::int16_t priority=50;
    std::uint32_t flags=0;
    std::int32_t interaction_number=3;
};
using SourceShooPlan=std::variant<SourceShooDirectPlan,SourceShooQueuedPlan>;
struct SourceShooPlanResult {
    SourceShooPlanStatus status=SourceShooPlanStatus::unsupported;
    std::optional<SourceShooPlan> plan;
    std::string diagnostic;
};
// Queue.Any predicate shared with VMSnap. Null callee is a known absence;
// unresolved retained/live callee or matching-GOTO interaction number is unknown.
struct SourceShooQueueEntry {
    bool callee_known=false;
    std::optional<std::uint32_t> callee_guid;
    std::optional<std::int32_t> interaction_number;
};
std::optional<bool> source_queue_has_shoo(const std::vector<SourceShooQueueEntry>&);
// ready contains exactly one typed plan. Known GetAction absence is no_action:
// source PushUserInteraction is void and the caller still WalkInterrupts(60).
SourceShooPlanResult plan_source_shoo(const SourceShooPlanInput&);
} // namespace odh
