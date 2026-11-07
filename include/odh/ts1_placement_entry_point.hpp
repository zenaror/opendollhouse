// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_placement.hpp"
namespace odh {
struct Ts1AvatarPlacementEntryPointRequest {
    const World* world=nullptr;
    const Ts1PlacementInvocation* invocation=nullptr;
    Ts1PlacementIdentity avatar;
    CodeOwner code_owner=0;
    std::shared_ptr<const VmProgram> global_program,owner_program;
    VmMemory live_memory;
    Vm* register_owner=nullptr;
    VmPrimitive check_primitive;
    std::function<bool()> identities_still_match;
    std::uint64_t instruction_budget=4096;
};
enum class Ts1PlacementEntryPointState {evaluated_true,evaluated_false,unsupported};
struct Ts1PlacementEntryPointResult {
    Ts1PlacementEntryPointState state=Ts1PlacementEntryPointState::unsupported;
    std::uint64_t steps=0;std::string diagnostic;
};
// VMAvatar.PositionChange -> VMEntity.PositionChange -> ExecuteEntryPoint9(true).
// Uses a source EvaluateCheck thread, with the actual avatar's Temp/TempXL bank.
// Main stack, pending services, action ancestry and source ticket stay intact.
Ts1PlacementEntryPointResult evaluate_ts1_avatar_placement_entry_point(const Ts1AvatarPlacementEntryPointRequest&);
}
