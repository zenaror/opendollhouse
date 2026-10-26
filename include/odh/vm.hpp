#pragma once

#include "odh/formats.hpp"
#include "odh/temporary_registers.hpp"
#include <map>
#include <memory>
#include <utility>

namespace odh {

enum class RoutineScope { global, private_object, semiglobal };
struct RoutineKey {
    RoutineScope scope;
    std::uint16_t id;
    bool operator<(const RoutineKey& other) const {
        return scope != other.scope ? scope < other.scope : id < other.id;
    }
};
using CodeOwner = std::uint32_t; // Resource identity; independent of World entity IDs.
// Explicit resource set for one code owner. No implicit file precedence.
using RoutineSet = std::map<RoutineKey, Bhav>;
// Additional owners contain only private/semiglobal routines. They may be
// appended at a host safe point, but an installed owner's routines never change.
// Owner zero is reserved for the default set; global routines are shared there.
using RoutineOwners = std::map<CodeOwner, RoutineSet>;
RoutineKey routine_key(std::uint16_t id);

// Immutable BHAV bank that multiple entity execution states may share.
// Runtime owners remain local to each Vm so a resource installed for one
// entity cannot accidentally become visible to another entity.
class VmProgram {
public:
    explicit VmProgram(RoutineSet routines):routines_(std::move(routines)){}
    const RoutineSet& routines() const {return routines_;}
private:
    friend class Vm;
    const RoutineSet routines_;
};
struct ShareVmProgramTag {};

enum class VmStatus { ready, dormant, running, budget_exhausted, waiting, returned_true, returned_false, fault, yielded };
enum class VmCallReturnMode { ordinary, interaction_next_tick, external_frame_next_tick };
const char* vm_status_name(VmStatus);
struct VmContext {
    std::int16_t caller = 0;
    std::int16_t callee = 0;
    std::int16_t stack_object = 0;
    CodeOwner code_owner = 0;
};
struct VmFrame {
    RoutineKey key;
    std::uint16_t pc = 0;
    VmContext context;
    std::vector<std::int16_t> arguments;
    std::vector<std::int16_t> locals;
    VmCallReturnMode return_mode = VmCallReturnMode::ordinary;
    std::uint64_t invocation_ticket = 0;
};
struct VmTrace {
    std::uint64_t step;
    std::size_t depth;
    RoutineKey key;
    std::uint16_t pc;
    Instruction instruction;
    CodeOwner code_owner = 0;
};
// World scopes are supplied explicitly by the eventual entity/world layer.
// Empty callbacks or nullopt mean unsupported, never zero or success.
struct VmMemory {
    std::function<std::optional<std::int16_t>(const VmFrame&, std::uint8_t, std::int16_t)> read;
    std::function<bool(const VmFrame&, std::uint8_t, std::int16_t, std::int16_t)> write;
    // Simantics scope 23 needs Temp0 as a room selector alongside its data index.
    std::function<std::optional<std::int16_t>(const VmFrame&, std::int16_t, std::int16_t)> room_by_temp0;
    // Scope 27 selects a dynamic sprite flag with the Temp register named by data.
    std::function<std::optional<bool>(const VmFrame&, std::uint16_t)> dynamic_sprite_flag;
    std::function<bool(const VmFrame&, std::uint16_t, bool)> set_dynamic_sprite_flag;
    // Expression-only setter result: nullopt is unsupported; false is a known
    // rejection and selects false for operators that observe SetBigVariable.
    // Existing write callbacks retain their unsupported-on-false contract.
    std::function<std::optional<bool>(const VmFrame&, std::uint8_t, std::int16_t, std::int16_t)> expression_write;
    // Caller level, callee level, callee OBJD LevelOffset. Used lazily only by
    // the reference's exact MyObject.Room < / >= 1024 Expression special case.
    std::function<std::optional<std::array<std::int32_t, 3>>(const VmFrame&)> expression_levels;
};
struct VmMemoryAddress {std::uint8_t scope;std::int16_t index;};
// Scopes30/31 select My/StackObject PersonData using temps[index]. The temp
// selector is checked here; the entity memory backend checks the resulting
// PersonData index. Other addresses pass through unchanged.
std::optional<VmMemoryAddress> resolve_person_data_address(std::uint8_t scope,
    std::int16_t index,const Ts1Temps& temps);
struct VmLimits {
    std::size_t stack_depth = 64;
    std::size_t frame_variables = 4096;
    std::size_t code_owners = 64; // Includes default owner zero.
};
enum class PrimitiveDisposition { goto_true, goto_false, wait, unsupported, call };
struct VmCall {
    RoutineKey key;
    VmContext context;
    std::vector<std::int16_t> arguments;
    VmCallReturnMode return_mode = VmCallReturnMode::ordinary;
};
enum class VmFrameBank { arguments, locals };
struct VmFrameWrite {
    VmFrameBank bank;
    std::size_t index;
    std::int16_t value;
    // Some game animation commands declare an event LocalNumber beyond the
    // BHAV's declared local count. Permit bounded growth only for that output.
    bool allow_local_growth = false;
};
struct VmTempWrite {
    std::size_t index;
    std::int16_t value;
};
struct VmStackObjectWrite {
    std::int16_t value;
};
struct PrimitiveOutcome {
    PrimitiveDisposition disposition = PrimitiveDisposition::unsupported;
    std::string diagnostic;
    std::optional<VmFrameWrite> write = {}; // Branches, WAIT and calls may publish validated frame output.
    std::optional<VmCall> call = {}; // Explicit child frame; parent remains on this instruction.
    // Synchronous branches only. Validated together with frame output before either is published.
    std::optional<VmTempWrite> temp_write = {};
    // Synchronous branches only; may hold any signed identity, independent of World entities.
    // Validated with frame/temp output before publication; other context identities are preserved.
    std::optional<VmStackObjectWrite> stack_object_write = {};
    // Synchronous branches only; validated with every output, applied after temp_write.
    std::optional<VmTempWrite> second_temp_write = {};
};
class Vm;
struct VmDispatch { const Vm* origin; std::uint64_t ticket; };
using VmPrimitive = std::function<PrimitiveOutcome(const VmFrame&, const Instruction&,
                                                 const Ts1Temps&, const VmDispatch&)>;
struct VmPending {
    std::uint64_t ticket;
    VmTrace instruction;
    VmContext context;
};
// An interaction result is diagnostic only; it never selects the parent's branch.
struct VmInteractionReturn {
    std::uint64_t ticket;
    bool result;
    CodeOwner code_owner;
    std::uint64_t run_generation;
};
struct VmExternalFrameReturn {
    std::uint64_t ticket;
    bool result;
    CodeOwner code_owner;
    std::uint64_t run_generation;
};

// Owned runtime values only. Resource/callback identity is a host precondition:
// restore into a fresh Vm built with the same immutable routines/owners and
// equivalent memory/primitive bindings. This is not a session-file envelope.
struct VmSnapshot {
    std::vector<VmFrame> stack;
    Ts1Temps temps{};
    // Simitone's VMThread owns two signed 32-bit TempXL values per entity.
    Ts1TempXL temp_xl{};
    VmStatus status = VmStatus::ready;
    std::string diagnostic;
    std::uint64_t steps = 0;
    std::optional<VmPending> pending;
    std::vector<VmPending> suspended_pending;
    std::optional<VmInteractionReturn> interaction_return;
    std::optional<VmExternalFrameReturn> external_frame_return;
    std::uint64_t ticket_sequence = 0;
    std::uint64_t run_generation = 0;
};

// Result of running a short BHAV on an existing entity thread's temporary
// stack. The entity's Main frames/status are restored; the thread's Temps and
// TempXL remain shared with the called tree, matching Simitone's same-thread
// subroutine model. A non-return status must be treated as an unsupported call
// by the host; callers must pre-audit the tree for synchronous behavior. This
// does not roll back World or service writes made before a non-return status.
struct VmSubroutineResult {
    bool started = false;
    VmStatus status = VmStatus::ready;
    std::uint64_t steps = 0;
    std::string diagnostic;
};

// Host-independent, resumable instruction kernel. Owns immutable routine sets.
// No scheduler, entities, queue, routing, animation, or clock is implied.
class Vm {
public:
    explicit Vm(RoutineSet routines, VmMemory memory = {}, VmLimits limits = {}, VmPrimitive primitive = {},
                RoutineOwners owners = {});
    explicit Vm(std::shared_ptr<const VmProgram> program, ShareVmProgramTag, VmMemory memory = {}, VmLimits limits = {},
                VmPrimitive primitive = {}, RoutineOwners owners = {});
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;
    Vm(Vm&&) = delete;
    Vm& operator=(Vm&&) = delete;
    VmSnapshot snapshot() const;
    // Invalid values return false; allocation failures throw. Both leave the
    // fresh destination unchanged. Calls from callbacks are forbidden.
    bool restore_snapshot(const VmSnapshot&);
    VmStatus start(RoutineKey, VmContext = {}, const std::vector<std::int16_t>& args = {});
    // Load a complete, already-resolved stack without starting Main or running
    // an instruction. Invalid snapshots throw and leave this VM unchanged.
    void restore_dormant_state(std::vector<VmFrame>, const Ts1Temps& temps);
    // A host must explicitly opt in after restoring the surrounding world state.
    bool activate_dormant_state();
    VmStatus run(std::uint64_t instruction_budget,
                 const std::function<void(const VmTrace&)>& trace = {});
    // Run a synchronous child tree using this thread's code owners, memory,
    // primitive handlers, and Temp registers, while saving/restoring its Main
    // stack and status. Requires an idle safe point with a live dormant or
    // runnable Main stack and no pending asynchronous service.
    VmSubroutineResult run_subroutine_in_temporary_stack(
        RoutineKey, VmContext, const std::vector<std::int16_t>& arguments,
        std::uint64_t instruction_budget,
        const std::function<void(const VmTrace&)>& trace = {});
    // Append one private/semiglobal resource owner at a host safe point. Existing
    // owners and active VM state are preserved. Rejects owner zero, duplicates,
    // invalid keys, capacity overflow, or calls made from inside run/callbacks.
    bool install_owner(CodeOwner, RoutineSet routines);
    // Observe availability in this VM without installing or changing code/state.
    // A missing nonzero owner returns false, including for global routines.
    bool has_routine(RoutineKey key, CodeOwner owner) const { return resolve(key,owner)!=nullptr; }
    VmStatus status() const { return status_; }
    const std::string& diagnostic() const { return diagnostic_; }
    const std::vector<VmFrame>& stack() const { return stack_; }
    Ts1Temps& temps() { return temps_; }
    const Ts1Temps& temps() const { return temps_; }
    Ts1TempXL& temp_xl() { return temp_xl_; }
    const Ts1TempXL& temp_xl() const { return temp_xl_; }
    std::uint64_t steps() const { return steps_; }
    std::uint64_t run_generation() const { return run_generation_; } // Every start attempt, including faults.
    const std::optional<VmPending>& pending() const { return pending_; }
    const std::optional<VmInteractionReturn>& interaction_return() const { return interaction_return_; }
    const std::optional<VmExternalFrameReturn>& external_frame_return() const { return external_frame_return_; }
    // Host must enforce the next tick boundary. This only releases a matching
    // yielded interaction and leaves the parent on its original instruction.
    bool resume_interaction(std::uint64_t ticket);
    // Push a BHAV child on behalf of a waiting service frame. The parent wait is
    // retained; child waits use their own tickets and child return yields back
    // to the service without re-dispatching the parent's BHAV instruction.
    bool call_pending(std::uint64_t ticket, const VmCall& call);
    bool resume_external_frame(std::uint64_t ticket);
    // Accept one matching service completion. Stale/duplicate tickets do nothing.
    // An invalid frame output rejects the entire completion without consuming its ticket.
    // Completion follows the saved instruction's branch; it does not dispatch again.
    bool complete(std::uint64_t ticket, bool result, std::optional<VmFrameWrite> write = {},
                  std::optional<VmStackObjectWrite> stack_object_write = {});
    // Publish a matching wait's frame output without branching, dispatching, or
    // consuming its ticket. Invalid/stale updates leave the entire VM unchanged.
    bool update_pending(std::uint64_t ticket, VmFrameWrite write);
    // Release a matching service wait for reexecution of the same instruction.
    // Unlike complete(), retry never takes an instruction branch.
    bool retry_pending(std::uint64_t ticket, std::optional<VmFrameWrite> write = {});
private:
    const Bhav* resolve(RoutineKey, CodeOwner) const;
    const Bhav& current_routine(const VmFrame&) const;
    void push(RoutineKey, VmContext, const std::vector<std::int16_t>&,
              VmCallReturnMode = VmCallReturnMode::ordinary, std::uint64_t ticket = 0);
    void branch(bool);
    bool expression(const Instruction&);
    std::int32_t read(std::uint8_t, std::int16_t);
    bool write(std::uint8_t, std::int16_t, std::int32_t);
    std::shared_ptr<const VmProgram> program_;
    RoutineOwners owners_;
    VmMemory memory_;
    VmLimits limits_;
    std::vector<VmFrame> stack_;
    Ts1Temps temps_{};
    Ts1TempXL temp_xl_{};
    VmStatus status_ = VmStatus::ready;
    std::string diagnostic_;
    std::uint64_t steps_ = 0;
    bool executing_ = false;
    VmPrimitive primitive_;
    std::optional<VmPending> pending_;
    std::vector<VmPending> suspended_pending_;
    std::optional<VmInteractionReturn> interaction_return_;
    std::optional<VmExternalFrameReturn> external_frame_return_;
    std::uint64_t ticket_sequence_ = 0; // Never reset by start().
    std::uint64_t run_generation_ = 0;
};
} // namespace odh
