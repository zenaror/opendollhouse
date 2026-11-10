#pragma once
#include "odh/entity_reference.hpp"
#include "odh/advertisement_memory.hpp"

#include "odh/formats.hpp"
#include "odh/temporary_registers.hpp"
#include <map>
#include <memory>
#include <utility>

namespace odh {

enum class RoutineScope { global, private_object, semiglobal, named_private, named_semiglobal };
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
RoutineKey named_routine_key(std::uint16_t id,bool linked_semiglobal);
bool routine_key_valid(RoutineKey) noexcept;
bool named_routine_scope(RoutineScope) noexcept;

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
using SharedRoutineOwners = std::map<CodeOwner, std::shared_ptr<const VmProgram>>;
struct ShareVmProgramTag {};
struct ShareVmOwnerProgramsTag {};

enum class VmStatus { ready, dormant, running, budget_exhausted, waiting, returned_true, returned_false, fault, yielded, next_tick };
enum class VmSavedRoutinePolicy { require_present, defer_missing_fault, defer_missing_or_pc_fault };
enum class VmCallReturnMode { ordinary, interaction_next_tick, external_frame_next_tick, immediate };
const char* vm_status_name(VmStatus);
struct VmContext {
    std::int16_t caller = 0;
    std::int16_t callee = 0;
    std::int16_t stack_object = 0;
    CodeOwner code_owner = 0;
    // Absent only in legacy contexts. Bound generation0 is explicit nil,
    // including a positive raw diagnostic ID; it must never be resolved again.
    bool entity_references_bound=false;
    std::uint64_t caller_generation=0,callee_generation=0,stack_object_generation=0;
};
bool vm_context_equal(const VmContext&,const VmContext&) noexcept;
bool vm_context_references_valid(const VmContext&) noexcept;
WorldEntityReference vm_context_reference(const VmContext&,VmEntityRole) noexcept;
// Partial role updates require already-bound source/destination contexts.
// False leaves the destination unchanged; bind all legacy roles atomically first.
bool vm_context_set_reference(VmContext&,VmEntityRole,WorldEntityReference) noexcept;
bool vm_context_copy_reference(VmContext&,VmEntityRole,const VmContext&,VmEntityRole) noexcept;
// Exact pinned7e76 original-import return annotation. These values describe
// a converted original stack, not an ODHS pending-service reconstruction.
enum class VmOriginalReturn : std::uint8_t { normal, retry, interaction };
struct VmOriginalUse {
    WorldEntityReference target{};
    std::uint8_t flags=0;
    bool operator==(const VmOriginalUse& other) const noexcept {return target==other.target&&flags==other.flags;}
};
struct VmOriginalFrameState {
    std::int32_t primitive_state=0; // Preserved; ConvertThread7e76 ignores this field.
    VmOriginalReturn result=VmOriginalReturn::normal;
    std::vector<VmOriginalUse> uses; // Exact ordered ObjectUses at this source depth.
    bool operator==(const VmOriginalFrameState& other) const noexcept {
        return primitive_state==other.primitive_state&&result==other.result&&uses==other.uses;
    }
};
struct VmFrame {
    RoutineKey key;
    std::uint16_t pc = 0;
    VmContext context;
    std::vector<std::int16_t> arguments;
    std::vector<std::int16_t> locals;
    VmCallReturnMode return_mode = VmCallReturnMode::ordinary;
    std::uint64_t invocation_ticket = 0;
    bool exact_arguments = false; // Source service frame extent; ordinary BHAV calls retain padding.
    bool action_tree=false;
    std::optional<VmOriginalFrameState> original_import={};
};
bool vm_frame_equal(const VmFrame&,const VmFrame&) noexcept;
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
    // Numeric entity assignments resolve LIVE identities once. A nullopt is
    // known nil, not permission to retry against a reused ID later.
    std::function<std::optional<WorldEntityReference>(std::int16_t)> resolve_live_entity;
    // Scope33 consumes Temp0/Temp1 from the actual reading thread. The host
    // supplies only the authenticated career catalog, never a register donor.
    std::function<std::optional<std::int16_t>(const VmFrame&,std::uint16_t,
        std::int16_t,std::int16_t)> job_data;
};
struct VmMemoryAddress {std::uint8_t scope;std::int16_t index;};
// Scopes30/31 select My/StackObject PersonData using temps[index]. The temp
// selector is checked here; the entity memory backend checks the resulting
// PersonData index. Negative selected indices are rejected, as in the shared
// resolver below. Other addresses pass through unchanged.
std::optional<VmMemoryAddress> resolve_person_data_address(std::uint8_t scope,
    std::int16_t index,const Ts1Temps& temps);
// Resolve frame/Temp selected addresses without changing either bank. Negative
// indirect results are unavailable; direct literal/signed data pass unchanged.
// The backing memory still enforces attribute/motive/person bank bounds and
// generation identity. Resolve afresh for each read and write.
std::optional<VmMemoryAddress> resolve_ts1_variable_address(std::uint8_t scope,
    std::int16_t index,const VmFrame& frame,const Ts1Temps& temps);
struct VmLimits {
    std::size_t stack_depth = 64;
    std::size_t frame_variables = 4096;
    std::size_t code_owners = 64; // Includes default owner zero.
};
enum class PrimitiveDisposition { goto_true, goto_false, wait, unsupported, call, goto_true_next_tick, continue_execution, continue_next_tick, lifecycle_deleted, source_error, suspend_and_call };
struct VmCall {
    RoutineKey key;
    VmContext context;
    std::vector<std::int16_t> arguments;
    VmCallReturnMode return_mode = VmCallReturnMode::ordinary;
    bool exact_arguments = false;
    std::optional<bool> action_tree_override={};
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
// A single synchronous output through the interpreter's ordinary TS1 setter.
// Keep this separate from multi-output frame/temporary publication: backend
// setters need not provide rollback and must never run before validation.
struct VmVariableWrite {
    std::uint16_t scope;
    std::int16_t index,value;
};
class Vm;
class Routing;
struct VmDispatch;
struct LiveEntryPoint5Request;
struct Ts1AvatarPlacementEntryPointRequest;
namespace detail {struct VmInstructionHandle;struct VmInstructionState;struct VmInstructionLink;struct VmDetachedCallState;}
class VmDetachedCallContinuation {
public:
    VmDetachedCallContinuation()=default;
private:
    friend class Vm;friend class VmInstructionLease;
    explicit VmDetachedCallContinuation(std::shared_ptr<detail::VmDetachedCallState> value):state_(std::move(value)){}
    std::shared_ptr<detail::VmDetachedCallState> state_;
};
// Ephemeral actual-instruction identity. Value copies grant no authority after
// the originating callback/setter expires, and are never snapshot data.
class VmInstructionLease {
public:
    VmInstructionLease()=default;
    bool valid() const noexcept;
    bool matches(const VmFrame&,const Instruction&,const VmDispatch&) const noexcept;
    bool detached() const noexcept;
    bool call_commit_active() const noexcept;
    const Vm* origin() const noexcept;
    std::optional<VmDetachedCallContinuation> prepare_detached_call(const VmCall&) const;
    // Commit-only proof: does not reopen handler or lifecycle authority.
    bool matches_call_commit(const VmFrame&,const Instruction&,const VmDispatch&,std::size_t parent_depth) const noexcept;
private:
    friend class Vm;friend class VmSourceResetAccess;
    explicit VmInstructionLease(std::weak_ptr<detail::VmInstructionHandle> value):handle_(std::move(value)){}
    std::weak_ptr<detail::VmInstructionHandle> handle_;
};
enum class VmInstructionChildKind {construction,source_check};
class VmInstructionChildScope {
public:
    ~VmInstructionChildScope();
    bool valid() const noexcept;
    bool authenticated_reset_of(const Vm&) const noexcept;
    VmInstructionChildScope(const VmInstructionChildScope&)=delete;
    VmInstructionChildScope& operator=(const VmInstructionChildScope&)=delete;
private:
    friend class Vm;friend class VmSourceResetAccess;
    explicit VmInstructionChildScope(std::shared_ptr<detail::VmInstructionLink> value):link_(std::move(value)){}
    std::shared_ptr<detail::VmInstructionLink> link_;
};
std::unique_ptr<VmInstructionChildScope> authorize_source_check(Vm& fresh_check,const LiveEntryPoint5Request&);
std::unique_ptr<VmInstructionChildScope> authorize_source_placement_entry_point_check(Vm& fresh_check,const Ts1AvatarPlacementEntryPointRequest&);

struct PrimitiveOutcome {
    PrimitiveDisposition disposition = PrimitiveDisposition::unsupported;
    std::string diagnostic;
    std::optional<VmFrameWrite> write = {}; // Branches, WAIT and calls may publish validated frame output.
    // call: ordinary/immediate child; suspend_and_call: ordinary descriptor
    // admitted as a real external_frame_next_tick child in this same run().
    // The latter creates the parent service pending record atomically, retains
    // its exact dispatch ticket, and accepts no other primitive outputs.
    std::optional<VmCall> call = {};
    // Synchronous branches or ordinary child calls. Call Temps publish after child
    // admission and successful call_commit, before the child executes; a rejected
    // call preserves them. Immediate/service children do not accept Temp outputs.
    std::optional<VmTempWrite> temp_write = {};
    // Synchronous branches only; may hold any signed identity, independent of World entities.
    // Validated with frame/temp output before publication; other context identities are preserved.
    std::optional<VmStackObjectWrite> stack_object_write = {};
    // Same branch/ordinary-call contract; validated with every output, applied after temp_write.
    std::optional<VmTempWrite> second_temp_write = {};
    // A primitive may commit external controller state only after the child
    // frame was successfully created. This callback must be atomic and must not
    // throw; false or an exception rolls back the child frame and any newly
    // admitted parent service pending record before that child runs.
    std::function<bool()> call_commit = {};
    // Exclusive of all other outputs/calls, and accepted only on a synchronous
    // branch. Vm resolves indirection and invokes its effective memory setter.
    std::optional<VmVariableWrite> variable_write = {};
    // Required only for a source-authorized ordinary call after synchronous
    // Reset detached the executing frame. Single-use, never persisted.
    std::optional<VmDetachedCallContinuation> detached_call = {};
};
class Vm;
struct VmDispatch {
    const Vm* origin;
    std::uint64_t ticket;
    // Check trees use the same interpreter state but a deliberately separate,
    // synchronous-only primitive handler. Handlers can refuse asynchronous or
    // queue-mutating services before they perform any external effects.
    bool check_mode = false;
};
using VmPrimitive = std::function<PrimitiveOutcome(const VmFrame&, const Instruction&,
                                                 const Ts1Temps&, const VmDispatch&)>;
using VmPrimitiveFactory=std::function<VmPrimitive(const VmMemory&)>;
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
struct VmImmediateContinuation {
    std::uint64_t ticket=0;
    std::size_t parent_depth=0;
    VmStatus parent_status=VmStatus::running;
    std::optional<VmPending> parent_pending;
};
struct VmImmediateReturn {
    std::uint64_t ticket=0;
    bool result=false;
    CodeOwner code_owner=0;
    std::uint64_t run_generation=0;
};
using ImmediatePendingAudit=std::function<bool(const VmPending&,const VmFrame&)>;

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
    std::vector<VmImmediateContinuation> immediate_continuations;
    std::optional<VmImmediateReturn> immediate_return;
    bool action_tree_known=false; // Legacy wire/import has no ActionTree metadata.
    AdvertisementChanges advertisement_changes;
};

// Single-use source-authorized admission into this actual undispatched Thread.
// Tickets are reserved from its native counter, never alleged saved TS1 tickets.
class VmOriginalImportPlan {
public:
    const VmSnapshot& snapshot() const noexcept {return prepared_;}
private:
    friend class Vm;
    const Vm* origin_=nullptr;
    VmSnapshot before_,prepared_;
    std::function<bool()> source_current_;
    bool consumed_=false;
};

// Result of running a short BHAV on an existing entity thread's temporary
// stack. The entity's Main frames/status are restored; the thread's Temps and
// TempXL remain shared with the called tree. A non-return status is not success
// or a failed eligibility result. World and register writes before that status
// remain visible; no action should be published from that evaluation.
struct VmSubroutineResult {
    bool started = false;
    VmStatus status = VmStatus::ready;
    std::uint64_t steps = 0;
    std::string diagnostic;
    // Explicit terminal result of a trusted disposable lifecycle, not a BHAV return.
    bool lifecycle_deleted = false;
    // Explicit authored ERROR observed in this synchronous evaluation only.
    // Fault remains a fault; the caller decides whether its source contract
    // maps ERROR to false. Unsupported operations and host failures never set it.
    bool source_error = false;
};

// Host-independent, resumable instruction kernel. Owns immutable routine sets.
// No scheduler, entities, queue, routing, animation, or clock is implied.
class Vm {
public:
    using OwnerResolver=std::function<std::shared_ptr<const VmProgram>(CodeOwner)>;
    // Exact immutable resource lookup for a missing nonzero owner at frame
    // push. Successful banks are validated/cached under the ordinary owner cap.
    // No owner0 fallback, implicit loading during queries/snapshot restore, or
    // serialization of this callback. Resolver must not reenter this VM.
    // False during dispatch/check/resolver execution leaves binding unchanged.
    bool set_owner_resolver(OwnerResolver);
    bool set_advertisement_fallback(AdvertisementFallback);
    const AdvertisementChanges& advertisement_changes() const noexcept {return advertisement_changes_;}
    const VmMemory& effective_memory() const noexcept {return memory_;}
    explicit Vm(RoutineSet routines, VmMemory memory = {}, VmLimits limits = {}, VmPrimitive primitive = {},
                RoutineOwners owners = {});
    explicit Vm(std::shared_ptr<const VmProgram> program, ShareVmProgramTag, VmMemory memory = {}, VmLimits limits = {},
                VmPrimitive primitive = {}, RoutineOwners owners = {});
    explicit Vm(std::shared_ptr<const VmProgram> program, ShareVmProgramTag, VmMemory memory, VmLimits limits,
                VmPrimitive primitive, SharedRoutineOwners owners, ShareVmOwnerProgramsTag);
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;
    Vm(Vm&&) = delete;
    Vm& operator=(Vm&&) = delete;
    VmSnapshot snapshot() const;
    // Invalid values return false; allocation failures throw. Both leave the
    // fresh destination unchanged. Calls from callbacks are forbidden.
    bool restore_snapshot(const VmSnapshot&,
        VmSavedRoutinePolicy = VmSavedRoutinePolicy::require_present);
    VmStatus start(RoutineKey, VmContext = {}, const std::vector<std::int16_t>& args = {},
                   bool exact_arguments = false);
    // Original Person use-unwind only, outside dispatch/check evaluation.
    // Discard obsolete frames without executing them or changing registers,
    // run identity, code ownership or surviving frames. Pending async/frame
    // ownership must be detached by its real service before this operation.
    bool source_recovery_pop_to_depth(std::size_t);
    // Load a complete saved stack without starting Main or running
    // an instruction. Invalid snapshots throw and leave this VM unchanged.
    // Pinned FreeSO VMStackFrame.Load retains absent saved routines. Opt in
    // only after authenticating the exact resource manifests: ordinary missing
    // frames retain their original data and fault when execution reaches them.
    // Existing routines still require valid metadata/banks/branches. Only
    // defer_missing_or_pc_fault also preserves an out-of-range saved PC, as
    // pinned FreeSO VMStackFrame.Load does; it faults when that frame is reached.
    // These policies are host admission only, never serialized flags or code.
    void restore_dormant_state(std::vector<VmFrame>, const Ts1Temps& temps,
        VmSavedRoutinePolicy = VmSavedRoutinePolicy::require_present);
    // Read-only version of the SAME dormant-frame admission/zero-padding
    // rules, used to preflight a queue against its prospective native stack.
    std::vector<VmFrame> prepare_dormant_frames(std::vector<VmFrame>,
        VmSavedRoutinePolicy = VmSavedRoutinePolicy::require_present) const;
    // Original OBJM/7e76 ConvertThread only. Frames carry their authenticated
    // return mapping and raw primitive_state. Requires current complete banks,
    // bound references and an undispatched actual Main/controller preimage.
    // No scripts run, no synthetic steps, no native pending service is invented.
    std::optional<VmOriginalImportPlan> prepare_original_import(
        std::vector<VmFrame>,const Ts1Temps&,std::function<bool()> source_current,
        std::string& diagnostic) const;
    bool original_import_current(const VmOriginalImportPlan&) const;
    bool commit_original_import(VmOriginalImportPlan&);
    // A host must explicitly opt in after restoring the surrounding world state.
    bool activate_dormant_state();
    VmStatus run(std::uint64_t instruction_budget,
                 const std::function<void(const VmTrace&)>& trace = {});
    // Execute a freshly started, disposable check VM at an external safe point.
    // The host supplies cloned or shared register values and exact code/resource
    // ownership. Only the explicit synchronous handler is used; waits and service
    // child calls fault instead of creating an untracked asynchronous operation.
    // World/register effects before a failure remain visible. Do not use on Main.
    // Lifecycle deletion is a separate terminal fact, enabled only by a trusted
    // Init/Load host that proves the exact owner was removed. Such a host must
    // keep the real Thread alive until this call and any register loan unwind.
    VmSubroutineResult run_synchronous_check(std::uint64_t instruction_budget,
                                            const VmPrimitive& check_primitive = {},
                                            const std::function<void(const VmTrace&)>& trace = {},
                                            bool allow_lifecycle_deletion = false);
    // Borrow the actual callee Thread's register banks during a scheduler tick.
    // Nested checks and Caller.Thread readers see the same arrays immediately,
    // including effects before false/fault/limit. Outside the tick use a clone.
    // On return the disposable VM retains a final value copy, never an alias.
    // The owner must outlive this synchronous call and cannot be this VM.
    VmSubroutineResult run_synchronous_check_with_registers(Vm& register_owner,
        bool share_effects, std::uint64_t instruction_budget,
        const VmPrimitive& check_primitive = {},
        const std::function<void(const VmTrace&)>& trace = {},
        bool allow_lifecycle_deletion = false);
    // Run a synchronous child tree using this thread's code owners, memory,
    // primitive handlers, and Temp registers, while saving/restoring its Main
    // stack and status. Requires an idle safe point with a live dormant or
    // runnable Main stack and no pending asynchronous service.
    VmSubroutineResult run_subroutine_in_temporary_stack(
        RoutineKey, VmContext, const std::vector<std::int16_t>& arguments,
        std::uint64_t instruction_budget,
        const std::function<void(const VmTrace&)>& trace = {});
    // Source EvaluateCheck uses a fresh disposable VM with shared immutable
    // banks/memory and real thread TEMP/XL. The entity execution layer remains
    // installed; a linked source check can Reset that layer without restoring
    // its displaced old Main. Only the supplied synchronous primitive/factory
    // handles check opcodes, with its own budget and no asynchronous waits or
    // tick yields. Authenticated ordinary child calls remain synchronous.
    // Named RunInMyStack is a separate same-VM path below. Exact source check
    // frames retain the supplied array extent; legacy calls pad metadata.
    VmSubroutineResult evaluate_check(
        RoutineKey, VmContext, const std::vector<std::int16_t>& arguments,
        std::uint64_t instruction_budget, const VmPrimitive& check_primitive = {},
        bool exact_arguments = false,const VmPrimitiveFactory& primitive_factory = {});
    // Source EvaluateCheck at an external queue boundary: disposable execution
    // shares immutable code/memory, the exact lazy owner resolver and thread registers, preserving every
    // parent continuation and its work/ticket/run counters. A real thread may
    // have an empty, dormant, stopped or yielded Main; that does not suppress
    // synchronous evaluation. Nested/reentrant execution remains forbidden.
    VmSubroutineResult evaluate_check_at_safe_point(
        RoutineKey,VmContext,const std::vector<std::int16_t>& arguments,
        std::uint64_t instruction_budget,const VmPrimitive& check_primitive = {},
        bool exact_arguments = false,const VmPrimitiveFactory& primitive_factory = {});
    // Source RunInMyStack: real executor registers/advertisements remain owned
    // by this Thread; parent frames and queue execution are temporarily parked.
    VmSubroutineResult run_named_in_stack(RoutineKey,VmContext,const std::vector<std::int16_t>&,
        std::uint64_t instruction_budget,const VmPrimitive& = {},const VmPrimitiveFactory& = {});
    bool synchronous_queue_empty() const noexcept {return named_stack_depth_!=0;}
    // False for a named tree descended from a fresh EvaluateCheck logical Thread.
    bool named_stack_inherits_parent_queue_index() const noexcept {return named_stack_depth_&&named_queue_inherits_parent_index_;}
    // Source ExecuteEntryPoint(runImmediately): disposable execution under this Thread's configured native budget,
    // registers owned by this entity's real Thread, shared only during scheduler ticks.
    VmSubroutineResult evaluate_entry_point_check(RoutineKey,VmContext,
        const std::vector<std::int16_t>& arguments,std::uint64_t instruction_budget,
        bool share_registers,const VmPrimitive& synchronous_primitive = {},
        const std::function<void(const VmTrace&)>& trace = {},
        bool allow_lifecycle_deletion = false);
    // Append one private/semiglobal resource owner at a host safe point. Existing
    // owners and active VM state are preserved. Rejects owner zero, duplicates,
    // invalid keys, capacity overflow, or calls made from inside run/callbacks.
    bool install_owner(CodeOwner, RoutineSet routines);
    // Share the immutable bank while keeping owner visibility local to this VM.
    bool install_shared_owner(CodeOwner, std::shared_ptr<const VmProgram> program);
    // Host owns the immutable registry and must exclude its controller, queue,
    // playback, primary and any other external owner references before release.
    // VM rejects a different registered bank or an owner referenced by execution.
    bool release_idle_owner(CodeOwner,const std::shared_ptr<const VmProgram>& registered_program);
    std::size_t remaining_owner_capacity() const noexcept {
        return limits_.code_owners-1-owners_.size();
    }
    // Discard execution at an external safe point before object Reset. Keeps
    // Temp/TempXL, memory and code; invalidates the old run and pending tickets.
    // Leaves an idle returned_false state, not a fresh-constructor state. The
    // host must separately cancel queues/controllers before using this API.
    bool reset_execution_state();
    VmStatus start_after_reset(RoutineKey,VmContext={},const std::vector<std::int16_t>& args={},bool exact_arguments=false);
    bool bind_context_live(VmContext&) const;
    bool bind_legacy_entity_references();
    std::vector<WorldEntityReference> strong_entity_references() const;
    bool references_entity(WorldEntityReference) const;
    // Observe availability in this VM without installing or changing code/state.
    // A missing nonzero owner returns false, including for global routines.
    bool has_routine(RoutineKey key, CodeOwner owner) const { return resolve(key,owner)!=nullptr; }
    // Read-only access to an already authenticated immutable bank. This never
    // resolves or installs an unknown owner and grants no entity authority.
    std::shared_ptr<const VmProgram> shared_program_for_owner(CodeOwner owner) const noexcept {
        if(owner==0)return program_;
        const auto found=owners_.find(owner);
        return found==owners_.end()?std::shared_ptr<const VmProgram>{}:found->second;
    }
    // Preflight an ordinary, interaction or immediate child at a primitive safe
    // point; external service frames belong to their scheduler. The subsequent
    // call with the same VM state will pass push's deterministic checks.
    bool can_accept_call(const VmCall&) const;
    // Authenticated primitive safe point: resolve a bounded host owner before
    // ordinary call preflight. Plain queries never trigger owner loading.
    bool prepare_primitive_call(const VmFrame&,const Instruction&,const VmDispatch&,const VmCall&);
    bool prepare_instruction_call(const VmInstructionLease&,const VmFrame&,const Instruction&,const VmDispatch&,const VmCall&);
    // Resolve only an immutable resource through THIS receiver's registered
    // resolver at an exact actual source instruction. The source may be a
    // different VM (including a linked check), or a retained detached frame.
    // Does not change execution/queue/register state or grant call/Reset rights.
    bool prepare_instruction_owner(const VmInstructionLease&,const VmFrame&,
        const Instruction&,const VmDispatch&,CodeOwner);
    // Authenticate this active primitive before resolving only an immutable owner.
    // Does not preflight/push a child, change registers, or grant queue authority.
    bool prepare_primitive_owner(const VmFrame&,const Instruction&,const VmDispatch&,CodeOwner);
    // Reconstruct a parked Thread's complete value state with immutable code
    // and explicit memory authority. No scheduler/controller callbacks migrate.
    std::unique_ptr<Vm> clone_for_check(VmMemory) const;
    VmStatus status() const { return status_; }
    // External load/check operations must not observe a register owner in the
    // middle of its instruction dispatch. This does not imply a VM status.
    bool dispatch_active() const noexcept { return executing_; }
    static std::optional<VmInstructionLease> active_instruction_lease(const VmFrame&) noexcept;
    std::optional<VmInstructionLease> instruction_lease(const VmFrame&) const noexcept;
    std::unique_ptr<VmInstructionChildScope> authorize_instruction_child(const VmInstructionLease&,
        VmInstructionChildKind=VmInstructionChildKind::construction);
    bool source_reset_lifecycle_active() const noexcept {return source_reset_hook_depth_!=0;}
    bool source_reset_queue_mutation_active() const noexcept;
    // Actual work in the last outer run(), independent of source Reset's
    // per-run steps counter. Runtime accounting only, never wire authority.
    std::uint64_t last_run_instruction_count() const noexcept {return last_run_instruction_count_;}

    // Certifies the current synchronous primitive callback, not a trace or a
    // stale copy of a former dispatch. Copies of its value token are allowed
    // only while that same callback and protected frame remain active.
    bool matches_active_primitive(const VmFrame&,const Instruction&,const VmDispatch&) const noexcept;
    // Read-only identity proof also authenticates real synchronous check dispatches.
    // It grants no mutation, owner admission, or child-call authority.
    bool matches_active_query(const VmFrame&,const Instruction&,const VmDispatch&) const noexcept;
    // Source GosubFoundAction publishes ActiveAction.Callee before AttemptPush.
    // Only the authenticated current primitive may alter its own StackObject;
    // no Caller/Callee/code-owner/PC or parent-frame authority is granted.
    bool set_active_primitive_stack_object(const VmFrame&,const Instruction&,const VmDispatch&,std::int16_t);
    // Kernel-authorized commit phase, after its exact child has been pushed
    // but before that child executes. Trace callbacks cannot borrow this proof.
    bool matches_active_call_commit(const VmFrame&,const Instruction&,const VmDispatch&,
                                   std::size_t parent_depth) const noexcept;
    const std::string& diagnostic() const { return diagnostic_; }
    const std::vector<VmFrame>& stack() const { return stack_; }
    // Entity.Thread.Stack excludes an ordinary temporary EvaluateCheck stack.
    // Actual RunInMyStack replaces the Thread stack, so remains visible here.
    const std::vector<VmFrame>& entity_thread_frames() const noexcept {return entity_thread_stack_?*entity_thread_stack_:stack_;}
    // A removal service must retain object data used by another execution
    // frame, including service waits, until that reference has been released.
    bool references_entity(std::int16_t entity) const;
    // Storage liveness only: preserve numeric IDs and all execution state.
    // The host must audit the current pending primitive as not reading the old
    // StackObject; future primitives/calls and unknown footprints fail closed.
    // The predicate may inspect this VM, but must not mutate it or the World.
    // Node budgets are bounded to 4096; unsupported/over-budget proofs pin storage.
    bool can_release_entity_storage(std::int16_t entity,
        const std::function<bool(const VmFrame&,const Instruction&)>& transparent_pending_predicate = {},
        std::size_t node_budget = 128) const;
    Ts1Temps& temps() { reject_storage_proof_mutation(); return register_temps(); }
    const Ts1Temps& temps() const { return register_temps(); }
    Ts1TempXL& temp_xl() { reject_storage_proof_mutation(); return register_xl(); }
    const Ts1TempXL& temp_xl() const { return register_xl(); }
    std::uint64_t steps() const { return steps_; }
    std::uint64_t run_generation() const { return run_generation_; } // Every start attempt, including faults.
    const std::optional<VmPending>& pending() const { return pending_; }
    const std::vector<VmPending>& suspended_pending() const {return suspended_pending_;}
    const std::optional<VmInteractionReturn>& interaction_return() const { return interaction_return_; }
    const std::optional<VmExternalFrameReturn>& external_frame_return() const { return external_frame_return_; }
    const std::vector<VmImmediateContinuation>& immediate_continuations() const {return immediate_continuations_;}
    const std::optional<VmImmediateReturn>& immediate_return() const {return immediate_return_;}
    // Queue/scheduler policy remains external: these methods never wake a
    // sleeping thread. The audit certifies only this already-pending service.
    bool can_inject_immediate(const VmCall&,const ImmediatePendingAudit& audit = {}) const;
    std::optional<std::uint64_t> inject_immediate(const VmCall&,const ImmediatePendingAudit& audit = {});
    // Queue must observe/retire its matching protected action before ACK.
    // Restores the exact parent wait/status and leaves its PC unchanged.
    bool resume_immediate(std::uint64_t ticket);
    // Host must enforce the next tick boundary. This only releases a matching
    // yielded interaction and leaves the parent on its original instruction.
    bool resume_interaction(std::uint64_t ticket);
    // Pinned FreeSO7e76 GOTO_TRUE_NEXT_TICK branches first, then stops the
    // current slice. Host must enforce the next tick boundary before release;
    // this does not redispatch the yielded primitive or consume a service ticket.
    bool resume_next_tick();
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
    // Complete the same authenticated wait, then yield a tick only if its branch
    // leaves a running continuation. Terminal/fault outcomes remain terminal.
    bool complete_next_tick(std::uint64_t ticket, bool result, std::optional<VmFrameWrite> write = {},
                            std::optional<VmStackObjectWrite> stack_object_write = {});
    // Consume an authenticated wait by pushing its ordinary callback child.
    // The parent stays on the original instruction; the child's return selects
    // that instruction's branch without an imposed tick boundary. Host resolves
    // the routine/owner first. Four source arguments are expanded to metadata
    // extent with -1; Caller/Callee/StackObject must match the protected parent.
    // Temp0 and ticket consumption commit only after successful child creation.
    // Stale/invalid calls, missing owners or failed pushes leave state unchanged.
    bool complete_call(std::uint64_t ticket, const VmCall& callback,
                       std::optional<std::int16_t> temp0 = {});
    // Publish a matching wait's frame output without branching, dispatching, or
    // consuming its ticket. Invalid/stale updates leave the entire VM unchanged.
    bool update_pending(std::uint64_t ticket, VmFrameWrite write);
    // Release a matching service wait for reexecution of the same instruction.
    // Unlike complete(), retry never takes an instruction branch.
    bool retry_pending(std::uint64_t ticket, std::optional<VmFrameWrite> write = {});
private:
    bool admit_shared_owner(CodeOwner,std::shared_ptr<const VmProgram>);
    // Only the actual instruction bridge and Routing's real Shoo callback
    // scope may reach this bounded cache operation. Public idle installation
    // retains its previous dispatch guard.
    bool prepare_source_owner(CodeOwner,const std::function<bool()>& authenticated_source);
    friend class Routing;
    void resolve_missing_owner(CodeOwner);
    // Both disposable-check constructors share immutable banks and inherit the
    // exact lazy owner provider before the first frame is pushed. New cache
    // admissions belong to that disposable VM, never its parked real Thread.
    void inherit_disposable_resource_context(Vm&) const;
    bool can_start_synchronous_check(std::uint64_t budget) const;
    const Bhav* resolve(RoutineKey, CodeOwner) const;
    const Bhav& current_routine(const VmFrame&) const;
    void push(RoutineKey, VmContext, const std::vector<std::int16_t>&,
              VmCallReturnMode = VmCallReturnMode::ordinary, std::uint64_t ticket = 0, bool exact_arguments = false,std::optional<bool> action_tree_override={});
    void branch(bool);
    bool pop_current_frame(bool);
    void branch_from_detached(VmFrame&,const Instruction&,bool);

    bool expression(const Instruction&);
    bool assign_stack_object(VmContext&,std::int16_t);
    std::int32_t read(std::uint8_t, std::int16_t);
    bool write(std::uint8_t, std::int16_t, std::int32_t);
    std::shared_ptr<const VmProgram> program_;
    SharedRoutineOwners owners_;
    OwnerResolver owner_resolver_;
    VmMemory memory_;
    AdvertisementChanges advertisement_changes_;
    AdvertisementFallback advertisement_fallback_;
    VmLimits limits_;
    std::vector<VmFrame> stack_;
    const std::vector<VmFrame>* entity_thread_stack_=nullptr;
    Ts1Temps temps_{};
    Ts1TempXL temp_xl_{};
    Ts1Temps* shared_temps_=nullptr;
    Ts1TempXL* shared_xl_=nullptr;
    Ts1Temps& register_temps() noexcept {return shared_temps_?*shared_temps_:temps_;}
    const Ts1Temps& register_temps() const noexcept {return shared_temps_?*shared_temps_:temps_;}
    Ts1TempXL& register_xl() noexcept {return shared_xl_?*shared_xl_:temp_xl_;}
    const Ts1TempXL& register_xl() const noexcept {return shared_xl_?*shared_xl_:temp_xl_;}
    VmStatus status_ = VmStatus::ready;
    bool action_tree_known_=true;
    std::string diagnostic_;
    std::uint64_t steps_ = 0;
    bool executing_ = false;
    friend class VmInstructionLease;
    friend class VmInstructionChildScope;
    friend class VmSourceResetAccess;
    friend class InteractionQueue;
    VmSubroutineResult evaluate_queue_check_at_safe_point(RoutineKey,VmContext,
        const std::vector<std::int16_t>&,std::uint64_t,const VmPrimitive&,bool,
        const VmPrimitiveFactory&,const std::function<bool()>& source_current);
    friend std::unique_ptr<VmInstructionChildScope> authorize_source_check(Vm&,const LiveEntryPoint5Request&);
    friend std::unique_ptr<VmInstructionChildScope> authorize_source_placement_entry_point_check(Vm&,const Ts1AvatarPlacementEntryPointRequest&);
    detail::VmInstructionState* instruction_scope_=nullptr;
    std::shared_ptr<detail::VmInstructionLink> instruction_child_;
    unsigned source_reset_hook_depth_=0;
    std::uint64_t last_run_instruction_count_=0;
    std::unique_ptr<VmInstructionChildScope> authorize_source_check_service(const Vm*,std::function<bool()>);
    bool source_reset_allowed(const VmInstructionLease&) const noexcept;
    std::unique_ptr<VmInstructionChildScope> authorize_reset_cleanup(const VmInstructionLease*,Vm&,std::function<bool()>);

    bool source_reset_clear(const VmInstructionLease*);
    VmStatus source_reset_main(RoutineKey,VmContext,const std::vector<std::int16_t>&);

    bool preparing_primitive_call_ = false;
    const VmDispatch* active_call_commit_dispatch_ = nullptr;
    const Instruction* active_call_commit_instruction_ = nullptr;
    std::size_t active_call_commit_parent_depth_ = 0;
    const VmDispatch* active_primitive_dispatch_=nullptr;
    const Instruction* active_primitive_instruction_=nullptr;
    VmPrimitive primitive_;
    std::optional<VmPending> pending_;
    std::vector<VmPending> suspended_pending_;
    std::optional<VmInteractionReturn> interaction_return_;
    std::optional<VmExternalFrameReturn> external_frame_return_;
    std::vector<VmImmediateContinuation> immediate_continuations_;
    std::optional<VmImmediateReturn> immediate_return_;
    std::uint64_t ticket_sequence_ = 0; // Never reset by start().
    std::uint64_t run_generation_ = 0;
    std::size_t named_stack_depth_=0;
    bool named_queue_inherits_parent_index_=false;
    bool check_active_ = false;
    mutable bool storage_proof_active_ = false,storage_proof_reentered_ = false;
    bool external_check_active_ = false;
    void reject_storage_proof_mutation() const;
    VmSubroutineResult evaluate_temporary_stack(RoutineKey,VmContext,const std::vector<std::int16_t>&,
        std::uint64_t,const VmPrimitive&,bool,const VmPrimitiveFactory&,bool named);
    VmStatus run_internal(std::uint64_t instruction_budget,
                          const std::function<void(const VmTrace&)>&,
                          const VmPrimitive*, bool check_mode,
                          std::uint64_t* executed_steps = nullptr,
                          bool* lifecycle_deleted = nullptr,
                          bool* source_error = nullptr);
};
} // namespace odh
