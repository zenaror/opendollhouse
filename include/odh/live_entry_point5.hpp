#pragma once

#include "odh/route_placement_proof.hpp"
#include "odh/route_execution_proof.hpp"
#include "odh/world.hpp"

namespace odh {

enum class LiveEntryPoint5State { evaluated_true, evaluated_false, unsupported };

// Host-owned ephemeral capability for ordinary placement immediately after
// real object construction. The callback must expire with that construction
// scope; it authenticates the actual suspended source without a fake opcode.
struct LiveObjectCreationPlacementSource {
    const Vm* source_vm=nullptr;
    std::uint64_t source_run_generation=0;
    std::int16_t created_entity=0;
    std::uint64_t created_generation=0;
    std::function<bool()> source_still_matches;
    // UnderneathMe retry ignores only the exact creating Caller multitile group.
    std::optional<WorldEntityReference> ignored_intersection_group;
};

struct LiveRetiredContainerPlacementSource {
    const World* world=nullptr;
    World::RetiredContainer container;
    WorldEntityReference child;
    std::function<bool()> source_still_matches;
};
// Original-import recovery owns the actual parked person Thread and original
// traversal. The scope callback must expire before either borrowed owner dies.
// It is checked before reading the token or Thread; this is no opcode dispatch.
struct LiveOriginalRecoveryPlacementSource {
    const World* world=nullptr;
    const OriginalLoadTraversal* traversal=nullptr;
    WorldEntityReference person{};
    const Vm* thread=nullptr;
    std::uint64_t run_generation=0;
    std::function<bool()> source_still_matches;
    std::optional<OriginalLoadPersonCursor> registration;
};
bool live_original_recovery_placement_source_matches(const LiveOriginalRecoveryPlacementSource&,const World&) noexcept;

// Explicit host-owned avatar construction/VerifyFamily scope, separate from a
// pending Create42 and from original saved-person recovery. The host owns the
// private activation, scheduler boundary, immutable resource and exact Thread.
// Main is prepared at pc0 but has never been dispatched. Check authority before
// dereferencing borrowed VM/World pointers; copied capabilities expire with it.
struct LiveAvatarConstructorPlacementSource {
    const World* world=nullptr;
    WorldEntityReference avatar{};
    const Vm* thread=nullptr;
    std::uint64_t run_generation=0;
    CodeOwner code_owner=0;
    std::function<bool()> source_still_matches;
};
bool live_avatar_constructor_placement_source_matches(const LiveAvatarConstructorPlacementSource&,const World&) noexcept;

struct LiveEntryPoint5Request {
    const World* world = nullptr;
    std::int16_t tested_entity = 0;
    std::int16_t other_entity = 0;
    std::uint64_t tested_generation = 0;
    std::uint64_t other_generation = 0;
    std::uint16_t action = 0;
    std::int16_t ghost = 0;

    // Proof that the owning Snap or route scope is still executing. The EP5 itself
    // runs in a separate disposable check VM, as source VMThread.EvaluateCheck
    // does, so this VM's Main stack and invocation ticket remain parked intact.
    const Vm* source_vm = nullptr;
    const VmFrame* source_frame = nullptr;
    const Instruction* source_instruction = nullptr;
    VmDispatch source_dispatch{};

    // Exact code-bank identity resolved by the host for tested_entity. The
    // default bank is the selected Global resource; owner_program combines the
    // tested entity's private and linked semiglobal routines.
    CodeOwner code_owner = 0;
    std::shared_ptr<const VmProgram> global_program;
    std::shared_ptr<const VmProgram> owner_program;
    VmMemory live_memory;

    // VMThread.EvaluateCheck shares the tested entity's registers while the
    // simulation tick is active. Null is accepted only when the host proves
    // that this entity has no Thread, in which case source initializes zeros.
    Vm* register_owner = nullptr;
    bool no_thread_proven = false;

    // Ghost-image ExecuteEntryPoint temporarily expands a VM sandbox. This
    // runner does not emulate that sandbox and refuses it explicitly.
    bool ghost_sandbox = false;
    std::function<bool()> identities_still_match;
    VmPrimitive check_primitive;
    std::uint64_t instruction_budget = 4096;
    std::optional<RoutePlacementProof> route_placement_proof;
    // Separate authority for source Route45/GotoRelative27 execution. This is
    // not interchangeable with the range-selection lease above. AttemptWalk
    // executes EP5 with ghost1 and keeps the caller's current heading.
    std::optional<RouteExecutionProof> route_execution_proof;
    RouteExecutionPhase route_execution_phase=RouteExecutionPhase::attempt_walk;
    std::optional<LiveObjectCreationPlacementSource> object_creation_source;
    std::optional<LiveRetiredContainerPlacementSource> retired_container_source;
    std::optional<LiveOriginalRecoveryPlacementSource> original_recovery_source;
    std::optional<LiveAvatarConstructorPlacementSource> avatar_constructor_source;
    Vm::OwnerResolver owner_resolver={}; // Explicit same-namespace resources, independent of register_owner.

};

struct LiveEntryPoint5Result {
    LiveEntryPoint5State state = LiveEntryPoint5State::unsupported;
    std::uint64_t steps = 0;
    std::string diagnostic;
};

// Runs one known EP5 with source's {other, ghost, 0, 0} arguments against the
// live World-backed VM memory. A missing action function (<=255) or a known
// absent BHAV is source false; incomplete resource or identity evidence is
// unsupported. World/register side effects before a non-return are preserved.
LiveEntryPoint5Result evaluate_live_entry_point5(const LiveEntryPoint5Request&);

} // namespace odh
