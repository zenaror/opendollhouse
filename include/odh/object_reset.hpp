#pragma once
#include "odh/avatar_startup.hpp"
#include "odh/entry_points.hpp"

namespace odh {
struct ObjectResetResult : AvatarStartupResult {
    bool main_started = false;
    bool already_resetting = false;
};
struct ObjectResetHooks {
    std::function<bool()> after_stack_cleared;
    std::function<bool()> before_return_avatar_effects;
};
// Runtime object recovery, not object creation or saved-lot activation policy.
// Host must first detach/clear its queues, scheduler/service waits and external
// handles, then call at a safe point with two distinct VMs and exact code banks.
// Clears the entity VM execution while retaining TEMP/XL, runs EP3 Cleanup on
// a fresh disposable synchronous check with shared register effects, and starts
// EP1 Main without dispatching it. Never runs Init0 or dynamic-multitile8.
// Missing entries, IDs<=255 and absent routines skip like ExecuteEntryPoint.
// Cleanup true/false both complete normally. Failure retains preceding World
// and register effects: host must discard/fail the candidate, not publish it.
// The caller owns full scheduler-phase and cleanup preconditions; this helper
// only guards active VM dispatch. It does not reset host queues/services.
ObjectResetResult run_object_reset(Vm& disposable_check, Vm& entity_thread,
    std::int16_t entity_id, CodeOwner resource_owner, const EntryPointTable&,
    std::uint64_t instruction_budget=10000, const VmPrimitive& synchronous_primitive={});
// Actual source instruction/typed-check lifecycle. Hooks receive a target-only
// stage capability; ordinary run/start/snapshot guards remain unchanged.
ObjectResetResult run_object_reset(const VmInstructionLease&,Vm& disposable_check,Vm& entity_thread,
    std::int16_t entity_id,CodeOwner resource_owner,const EntryPointTable&,
    std::uint64_t instruction_budget=10000,const VmPrimitive& synchronous_primitive={},
    const ObjectResetHooks& hooks={});
} // namespace odh
