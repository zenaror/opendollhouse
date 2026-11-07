#pragma once

#include "odh/vm.hpp"
#include "odh/world.hpp"

namespace odh {

enum class FunctionalTreeCodeStatus : std::uint8_t {
    resolved,
    target_resource_absent,
    unsupported
};

struct FunctionalTreeResolvedCode {
    FunctionalTreeCodeStatus status = FunctionalTreeCodeStatus::unsupported;
    CodeOwner owner = 0;
    RoutineKey key{};
    std::shared_ptr<const VmProgram> global_program;
    std::shared_ptr<const VmProgram> owner_program;
    VmMemory memory;
    Vm::OwnerResolver owner_resolver={}; // Same resource namespace as these immutable banks; not a Temp seed.
};

struct FunctionalTreeIconUpdate {
    // can_commit is read-only. Both callbacks must not throw; commit is called
    // only after the child frame is accepted, or directly for an empty tree.
    std::function<bool()> can_commit;
    std::function<bool()> commit;
};

struct RunFunctionalTreeRequest {
    World* world = nullptr;
    Vm* source_vm = nullptr;
    const VmFrame* source_frame = nullptr;
    const Instruction* source_instruction = nullptr;
    VmDispatch source_dispatch{};

    // Host-owned liveness and resource provenance. A resolved owner with no
    // requested routine in its exact banks is distinct from missing evidence.
    std::function<bool(std::int16_t, std::uint64_t)> target_is_live;
    std::function<bool()> identities_still_match;
    std::function<FunctionalTreeResolvedCode(
        std::int16_t, std::uint64_t, std::uint16_t)> resolve_routine;

    // RunTree checks copy the Caller entity's native registers into a fresh
    // stack-depth-five check VM. A null register owner is accepted only when
    // the host has proven that Caller has no VM thread.
    Vm* caller_register_owner = nullptr;
    bool caller_thread_absent_proven = false;
    VmPrimitive check_primitive;
    std::uint64_t check_instruction_budget = 4096;

    // These callbacks bind the inherited ActionTree state and queue-owned icon
    // mutation without making the VM primitive depend on the interaction queue.
    std::function<std::optional<bool>(const VmFrame&, const Instruction&,
                                      const VmDispatch&)> action_tree_for;
    std::function<std::optional<FunctionalTreeIconUpdate>(
        const VmFrame&, const Instruction&, const VmDispatch&,
        std::int16_t icon_owner, std::uint64_t icon_generation,
        const std::optional<VmCall>& expected_child)> prepare_icon_update;
};

// Execute TS1 RunFunctionalTree operand/function semantics at an authenticated
// active opcode-20 dispatch. Source faults and unsupported host evidence remain
// explicit; missing target action BHAVs and failed conditions branch false.
PrimitiveOutcome run_functional_tree(const RunFunctionalTreeRequest&);

} // namespace odh
