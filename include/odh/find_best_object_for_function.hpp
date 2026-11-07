#pragma once

#include "odh/run_functional_tree.hpp"

namespace odh {

struct FindBestObjectIdentity {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
};

struct FindBestObjectForFunctionRequest {
    World* world = nullptr;
    Vm* source_vm = nullptr;
    const VmFrame* source_frame = nullptr;
    const Instruction* source_instruction = nullptr;
    VmDispatch source_dispatch{};

    // A complete snapshot of VM.Entities in source order. UI-only ghosts that
    // were never added to VM.Entities must remain excluded.
    std::function<std::optional<std::vector<FindBestObjectIdentity>>()> ordered_live_entities;
    std::function<bool()> identities_still_match;
    std::function<std::optional<std::int16_t>(std::int16_t,std::uint64_t)> effective_lockout;
    std::function<std::optional<std::uint32_t>(std::int16_t,std::uint64_t)> disabled;
    std::function<std::optional<std::int16_t>(std::int16_t,std::uint64_t,std::uint8_t)> score_value;
    std::function<std::optional<bool>(std::int16_t,std::uint64_t)> slot_zero_empty;
    std::function<std::optional<bool>(std::int16_t,std::uint64_t)> is_in_use;

    // Condition lookup follows GetRoutineWithOwner: an exact known missing
    // target resource means no condition routine; unresolved provenance is not
    // treated as a missing routine.
    std::function<FunctionalTreeResolvedCode(std::int16_t,std::uint64_t,std::uint16_t)> resolve_condition;
    Vm* caller_register_owner = nullptr;
    bool caller_thread_absent_proven = false;
    VmPrimitive check_primitive;
    std::uint64_t check_instruction_budget = 4096;
    VmPrimitiveFactory check_primitive_factory;
    bool scheduler_running_now=false; // Source EvaluateCheck aliases only during the scheduler tick.
};

// TS1 FindBestObjectForFunction (opcode 14), with host-proven complete entity
// order, timed lockout, disabled state, and cross-avatar use checks.
PrimitiveOutcome find_best_object_for_function(const FindBestObjectForFunctionRequest&);

} // namespace odh
