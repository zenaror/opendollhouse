#pragma once
#include "odh/avatar_startup.hpp"
#include "odh/entry_points.hpp"

namespace odh {
using SavedEntityLoadResult = AvatarStartupResult;
// Pinned TS1 blueprint-load callback EP2, OUTSIDE a simulation tick. The host
// first restores the entity's saved execution and exact resource/code bindings.
// The host owns the scheduler-phase precondition; dispatch_active only guards
// instruction reentry, not the phase between two scheduled entities.
// Supply a separate fresh check VM bound to the SAME PRIVATE candidate World,
// with the complete resolved Load code and synchronous-only primitive handler.
// Caller/callee=self, stack object0, four zero args. Check registers clone the
// restored thread's registers; their changes never overwrite its continuation.
// Explicit World effects are kept; Main stack/status/pending/registers are not
// changed and no Init0/dynamic8/Reset3/Main1 is executed here.
// Both true/false callback returns complete normally (the source caller ignores
// that boolean). Unsupported code, waits/faults/budget failures are incomplete;
// discard the private candidate including any effects already produced.
// This is a callback operation, NOT authority to activate a neutral property.
SavedEntityLoadResult run_saved_entity_load(Vm& disposable_check, Vm& restored_thread,
    std::int16_t entity_id, CodeOwner resource_owner, const EntryPointTable&,
    std::uint64_t instruction_budget = 10000, const VmPrimitive& synchronous_primitive = {});
} // namespace odh
