#pragma once

#include "odh/formats.hpp"
#include <map>

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
// Additional immutable owners contain only private/semiglobal routines.
// Owner zero is reserved for the default set; global routines are shared there.
using RoutineOwners = std::map<CodeOwner, RoutineSet>;
RoutineKey routine_key(std::uint16_t id);

enum class VmStatus { ready, running, budget_exhausted, waiting, returned_true, returned_false, fault, yielded };
enum class VmCallReturnMode { ordinary, interaction_next_tick };
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
};
struct VmMemoryAddress {std::uint8_t scope;std::int16_t index;};
// Scopes30/31 select My/StackObject PersonData using temps[index]. The temp
// selector is checked here; the entity memory backend checks the resulting
// PersonData index. Other addresses pass through unchanged.
std::optional<VmMemoryAddress> resolve_person_data_address(std::uint8_t scope,
    std::int16_t index,const std::array<std::int16_t,20>& temps);
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
                                                 const std::array<std::int16_t,20>&, const VmDispatch&)>;
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

// Host-independent, resumable instruction kernel. Owns an immutable routine set.
// No scheduler, entities, queue, routing, animation, or clock is implied.
class Vm {
public:
    explicit Vm(RoutineSet routines, VmMemory memory = {}, VmLimits limits = {}, VmPrimitive primitive = {},
                RoutineOwners owners = {});
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;
    Vm(Vm&&) = delete;
    Vm& operator=(Vm&&) = delete;
    VmStatus start(RoutineKey, VmContext = {}, const std::vector<std::int16_t>& args = {});
    VmStatus run(std::uint64_t instruction_budget,
                 const std::function<void(const VmTrace&)>& trace = {});
    VmStatus status() const { return status_; }
    const std::string& diagnostic() const { return diagnostic_; }
    const std::vector<VmFrame>& stack() const { return stack_; }
    std::array<std::int16_t, 20>& temps() { return temps_; }
    const std::array<std::int16_t, 20>& temps() const { return temps_; }
    std::uint64_t steps() const { return steps_; }
    std::uint64_t run_generation() const { return run_generation_; } // Every start attempt, including faults.
    const std::optional<VmPending>& pending() const { return pending_; }
    const std::optional<VmInteractionReturn>& interaction_return() const { return interaction_return_; }
    // Host must enforce the next tick boundary. This only releases a matching
    // yielded interaction and leaves the parent on its original instruction.
    bool resume_interaction(std::uint64_t ticket);
    // Accept one matching service completion. Stale/duplicate tickets do nothing.
    // An invalid frame output rejects the entire completion without consuming its ticket.
    // Completion follows the saved instruction's branch; it does not dispatch again.
    bool complete(std::uint64_t ticket, bool result, std::optional<VmFrameWrite> write = {});
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
    std::int16_t read(std::uint8_t, std::int16_t);
    void write(std::uint8_t, std::int16_t, std::int32_t);
    RoutineSet routines_;
    RoutineOwners owners_;
    VmMemory memory_;
    VmLimits limits_;
    std::vector<VmFrame> stack_;
    std::array<std::int16_t, 20> temps_{};
    VmStatus status_ = VmStatus::ready;
    std::string diagnostic_;
    std::uint64_t steps_ = 0;
    bool executing_ = false;
    VmPrimitive primitive_;
    std::optional<VmPending> pending_;
    std::optional<VmInteractionReturn> interaction_return_;
    std::uint64_t ticket_sequence_ = 0; // Never reset by start().
    std::uint64_t run_generation_ = 0;
};
} // namespace odh
