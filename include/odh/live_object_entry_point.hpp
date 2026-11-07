// SPDX-License-Identifier: MPL-2.0
// Source: FreeSO VMEntity.ExecuteEntryPoint/ExecuteGenericEntryPoint,
// revision 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once

#include "odh/live_entry_point5.hpp"

namespace odh {

enum class LiveObjectEntryPointState { evaluated_true, evaluated_false, unsupported };

struct LiveObjectEntryPointRequest {
    const World* world=nullptr;
    std::int16_t object=0;
    std::uint64_t object_generation=0;
    std::uint16_t entry_point=0,action=0;
    // Source null StackObject is zero; an explicit live entity needs its epoch.
    std::int16_t stack_object=0;
    std::uint64_t stack_object_generation=0;
    // Null creates exactly four zeros. Present arrays retain their extent.
    std::optional<std::vector<std::int16_t>> source_arguments;
    // This capability authorizes only the newly-created object itself. Calls
    // on neighboring objects need a separate, specifically scoped capability.
    LiveObjectCreationPlacementSource creation_source;
    CodeOwner code_owner=0;
    std::shared_ptr<const VmProgram> global_program,owner_program;
    VmMemory live_memory;
    Vm* register_owner=nullptr;
    // Host certifies exact entity/resource provenance and real Thread binding.
    // Programs must also be the actual immutable banks installed in that VM.
    std::function<bool()> identities_still_match;
    VmPrimitive check_primitive;
    std::uint64_t instruction_budget=4096;
    Vm::OwnerResolver owner_resolver={}; // Explicit same-namespace resources, independent of register_owner.

};

struct LiveObjectEntryPointResult {
    LiveObjectEntryPointState state=LiveObjectEntryPointState::unsupported;
    std::uint64_t steps=0;
    std::string diagnostic;
};

// Synchronous ordinary-object entry points, including creation EP6/8/9. EP11
// needs its architecture wrapper and is explicitly refused. Ghost sandboxes,
// asynchronous calls and invented primitive dispatch authority are unsupported.
// World and real Thread TEMP/XL effects survive false and non-return outcomes.
LiveObjectEntryPointResult evaluate_live_object_entry_point(const LiveObjectEntryPointRequest&);

} // namespace odh
