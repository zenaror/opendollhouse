#pragma once
#include "odh/world.hpp"
#include "odh/interaction_label.hpp"
#include <algorithm>
#include <deque>
#include <stdexcept>

namespace odh {
// Exact TS1 TreeTable.InteractionByIndex fallback; nullptr is a known missing
// table. Unknown encodings and duplicate source keys remain unsupported.
std::optional<std::int16_t> interaction_advertisement(const Ttab*,std::int32_t interaction_number,
    std::uint8_t type,std::uint16_t motive_index);
struct InteractionCompletion {std::uint64_t uid,ticket,tick;bool result;};
enum class InteractionPermission { allowed, denied, unsupported };
// TS1 CheckTS1Action permission block only. Missing avatar data is unknown;
// FSOSkipPermissions bypasses permissions, never the separate check BHAV.
InteractionPermission ts1_interaction_permission(const Entity*,std::uint32_t flags);
enum class InteractionQueueMode : std::uint8_t { normal, parent_idle, parent_exit, idle };
enum class InteractionQueueProfile : std::uint8_t { legacy_fifo, ts1_priority };
enum class InteractionCancelResult : std::uint8_t { missing, removed, notified, unsupported };
// Presentation-only weak identity: source IconOwner can outlive removal of its
// VMEntity. Never use this as a callee, simulation reference or ID-only lookup.
struct InteractionIconOwnerSnapshot {std::int16_t id=0;std::uint64_t generation=0;};
enum class InteractionArgumentMode : std::uint8_t { legacy_metadata, source_null, source_explicit };
// Explicit producer intent for fresh eligibility arguments. This preserves the
// autonomous-only test contract present in original TS1 data across queued
// rechecks. It is a data-supported correction to the pinned reference's later
// zero-argument recheck, not a recovered original-executable ABI claim.
// Default retains legacy/manual zero checks; never infer this from priority.
enum class InteractionCheckContext : std::uint8_t { default_zero, autonomous };
struct InteractionActionSnapshot {
    std::uint64_t uid,target_generation;
    VmCall call;
    std::uint16_t queue_uid=0;
    std::int16_t priority=1;
    InteractionQueueMode mode=InteractionQueueMode::normal;
    std::uint32_t flags=0;
    bool notify_idle=false;
    // Check uses the action code owner and actor/source context. Missing means
    // a legacy or direct host request; never invent a check for old saves.
    std::optional<RoutineKey> check={};
    bool check_permissions=false;
    std::optional<InteractionIconOwnerSnapshot> icon_owner={};
    // Exact source VMQueuedAction.InteractionNumber, including global high bit.
    // Unknown/direct requests use nullopt; explicitly supplied signed bits are preserved.
    std::optional<std::int32_t> interaction_number={};
    InteractionArgumentMode argument_mode=InteractionArgumentMode::legacy_metadata;
    InteractionCheckContext check_context=InteractionCheckContext::default_zero;
    std::optional<InteractionLabel> label={};
};
// Actual AttemptPush may retain an old action across a synchronous Reset.
// Execution is distinct from the CURRENT header at its ActiveQueueBlock index;
// that header may be different or absent. No lifecycle lease is persisted.
struct InteractionDetachedExecutionSnapshot {
    VmCall call;
    std::uint64_t source_run_generation=0;
};
struct InteractionActiveSnapshot {
    std::uint64_t uid,ticket;
    // Header lookup follows the source's CURRENT numeric queue index.
    // uid is its actual current header UID, or0 only for a proved absence.
    std::optional<InteractionDetachedExecutionSnapshot> detached_execution={};
};

struct InteractionWaitSnapshot {std::uint64_t ticket,deadline;};
struct InteractionQueueSnapshot {
    std::int16_t actor_id;
    std::size_t capacity;
    std::uint64_t actor_generation,run_generation,sequence,clock,idle_start;
    std::optional<std::uint64_t> last_tick,resume_tick;
    std::deque<InteractionActionSnapshot> actions;
    std::optional<InteractionActiveSnapshot> active;
    std::optional<InteractionWaitSnapshot> wait;
    std::optional<InteractionCompletion> completion;
    bool interrupted;
    std::string diagnostic;
    InteractionQueueProfile profile=InteractionQueueProfile::ts1_priority;
    std::uint16_t next_queue_uid=0;
    // Protected action ancestors, ordered outermost to innermost parent. The
    // active action is stored separately and is always the innermost frame.
    std::vector<InteractionActiveSnapshot> suspended_active;
};
// Value-only cross-codec helpers. A detached binding follows the CURRENT
// numeric source index; its executing call is proven separately by VM state.
inline const VmCall* interaction_execution_call(const InteractionQueueSnapshot& state,std::size_t index){
    const InteractionActiveSnapshot* active=index<state.suspended_active.size()?&state.suspended_active[index]:
        index==state.suspended_active.size()&&state.active?&*state.active:nullptr;
    if(!active)return nullptr;
    if(active->detached_execution)return &active->detached_execution->call;
    return index<state.actions.size()?&state.actions[index].call:nullptr;
}
inline bool interaction_header_binding_valid(const InteractionQueueSnapshot& state,std::size_t index,
        const InteractionActiveSnapshot& active){
    if(!active.detached_execution)return active.uid&&index<state.actions.size()&&state.actions[index].uid==active.uid;
    const auto& detached=*active.detached_execution;const auto& call=detached.call;
    if(!detached.source_run_generation||detached.source_run_generation>=state.run_generation||
       call.return_mode!=VmCallReturnMode::interaction_next_tick||!routine_key_valid(call.key)||
       call.arguments.size()>4096||(call.action_tree_override&&!*call.action_tree_override)||
       !call.context.entity_references_bound||call.context.caller!=state.actor_id||
       call.context.caller_generation!=state.actor_generation||call.context.callee<=0||
       !call.context.callee_generation||call.context.stack_object!=call.context.callee||
       call.context.stack_object_generation!=call.context.callee_generation)return false;
    // Only the actual reset-first action can lack its current header; a
    // nonexistent arbitrary nested prefix is not a source continuation.
    return index<state.actions.size()?active.uid==state.actions[index].uid:
        (!active.uid&&index==0&&state.suspended_active.empty()&&state.actions.empty());
}
// Native queue for one avatar/VM. The TS1 profile currently implements priority
// insertion and its persisted action metadata. World and VM must outlive it.
// Host observes before each run/budget
// slice and after completion, then ticks once per consecutive simulation tick.
class InteractionQueue {
public:
    InteractionQueue(World& world,std::int16_t actor,std::size_t capacity=32)
        :world_(world),actor_id_(actor),capacity_(capacity) {}
    bool bind(Vm&);
    // Upgrade only legacy snapshots, proving accepted queued roots by UID,
    // ticket and call identity before propagating to their source children.
    static bool upgrade_legacy_action_tree(VmSnapshot&,InteractionQueueSnapshot&);
    static bool bind_legacy_references(InteractionQueueSnapshot&,const World&);
    std::vector<WorldEntityReference> strong_entity_references() const;
    std::optional<bool> source_reset_required() const;
    std::optional<bool> action_tree_for(const VmFrame&,const Instruction&,const VmDispatch&) const;
    // Real GenericTS1Call opcode1 mode2. Updates only the actual active action's
    // presentation icon, including a source-null clear; no target/queue rewrite.
    PrimitiveOutcome set_action_icon_to_stack_object(const VmFrame&,const Instruction&,const VmDispatch&);
    // Read-only source query against this queue's actual persistent VM. Works
    // during a synchronous CheckAction; unknown ancestry stays unsupported.
    std::optional<bool> interacting_with(const World& expected_world,const Vm& real_thread,
        std::int16_t caller,std::int16_t target) const;
    std::optional<bool> interacting_with(const World& expected_world,const Vm& real_thread,
        std::int16_t caller,WorldEntityReference target) const;
    class PreparedIconUpdate {
    public:
        bool can_commit() const noexcept;
        bool commit() noexcept;
    private:
        friend class InteractionQueue;
        InteractionQueue* queue_=nullptr;
        const Vm* vm_=nullptr;
        VmFrame frame_{};Instruction instruction_{};VmDispatch dispatch_{};
        std::optional<VmCall> child_;
        std::size_t depth_=0;
        std::uint64_t actor_generation_=0,run_=0,steps_=0,uid_=0,ticket_=0;
        std::uint64_t queue_sequence_=0,clock_=0;
        std::size_t protected_count_=0;
        std::optional<InteractionIconOwnerSnapshot> icon_;
        std::optional<InteractionIconOwnerSnapshot> previous_;
        bool consumed_=false;
    };
    // Authenticated opcode20 SetActionIcon or opcode1 mode2, retaining each
    // instruction's own proof. A source-null mode2 uses icon0/generation0.
    std::optional<PreparedIconUpdate> prepare_icon_owner_update(const VmFrame&,const Instruction&,
        const VmDispatch&,std::int16_t icon,std::uint64_t generation,
        const std::optional<VmCall>& expected_child={});
    InteractionQueueSnapshot snapshot() const;
    // No publication: prospective VM must have been admitted by the actual
    // bound VM's native frame/resource validator, never a fabricated new VM.
    bool can_restore_snapshot(const InteractionQueueSnapshot&,const VmSnapshot&) const;
    // Fresh controller, already bound to an equivalently restored World/VM,
    // with equivalent callbacks. Capacity is capped by the constructor value.
    // Invalid values return false; allocation failures throw. Neither changes
    // the destination. Capture/restore during controller or VM callbacks throws.
    bool restore_snapshot(const InteractionQueueSnapshot&);
    void set_thread_interrupt_source(std::function<bool()> consume) {thread_interrupt_=std::move(consume);}
    // Simple host queue request using inherited priority 1. A v1 FIFO-restored
    // queue retains FIFO semantics through this entry point. In the TS1 profile,
    // ordinary pivot insertion cancels its original suffix once in source order.
    // Retained items are signalled rather than retried forever. If a cancellation
    // fails, false removes the new request but preserves earlier World/queue
    // effects and consumed identities; diagnostics distinguish that partial failure.
    bool enqueue(RoutineKey,VmContext,const std::vector<std::int16_t>& args={},bool push_after_active=false);
    // Native PushInteraction path. A legacy v1 FIFO queue adopts priority order
    // on this first source action while preserving its existing action order.
    bool enqueue_ts1(RoutineKey,VmContext,const std::vector<std::int16_t>& args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,
        bool push_head,bool push_tail,std::optional<RoutineKey> check={},
        std::optional<std::int32_t> interaction_number={});
    // Player-selected TTAB action: retain original flags, recheck permissions
    // and BHAV at dispatch, with UserDriven priority 50.
    bool enqueue_user(RoutineKey,VmContext,const std::vector<std::int16_t>& args,
        std::uint32_t flags,std::optional<RoutineKey> check={},
        std::optional<std::int32_t> interaction_number={});
    // Source VMQueuedAction.ToStackFrame: null creates exactly four zeros;
    // a present array retains its exact extent, including an empty array.
    bool enqueue_source_ts1(RoutineKey,VmContext,const std::optional<std::vector<std::int16_t>>& source_args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,
        bool push_head,bool push_tail,std::optional<RoutineKey> check={},
        std::optional<std::int32_t> interaction_number={},
        std::optional<InteractionIconOwnerSnapshot> icon_owner={},std::optional<InteractionLabel> label={});
    bool enqueue_source_user(RoutineKey,VmContext,const std::optional<std::vector<std::int16_t>>& source_args,
        std::uint32_t flags,std::optional<RoutineKey> check={},
        std::optional<std::int32_t> interaction_number={},std::optional<InteractionLabel> label={});
    // Source autonomous action uses original TTAB flags/permissions, priority2
    // and exactly four selected-variant arguments. Eligibility rechecks retain
    // explicit autonomous context separately; action arguments are untouched.
    // No skip-permissions injection.
    bool enqueue_source_autonomous(RoutineKey,VmContext,const std::vector<std::int16_t>&,
        std::uint32_t flags,std::optional<RoutineKey> check={},std::optional<std::int32_t> interaction_number={},std::optional<InteractionLabel> label={});
    bool first_higher_priority(std::int16_t,std::optional<InteractionActionSnapshot>&);
    bool active_actions(std::vector<InteractionActionSnapshot>&);
    // Source GetTreeAd fallback belongs to this real Thread's ActiveAction,
    // including an exact retained Callee; candidate checks never call this.
    using AdvertisementLookup=std::function<std::optional<std::int16_t>(
        WorldEntityReference,std::int32_t,std::uint8_t,std::uint16_t)>;
    std::optional<std::int16_t> active_advertisement(const Vm&,std::uint8_t type,
        std::uint16_t index,const AdvertisementLookup&) const;
    // Cancel by stable native identity. The source-compatible wrapping 16-bit
    // queue UID remains metadata and is never used as an internal pointer.
    InteractionCancelResult cancel(std::uint64_t native_uid);
    // Internal original-load recovery safe point. These mutate native queue
    // ownership directly; ordinary user cancellation retains its existing rules.
    // The caller performs ordered use/frame unwinding and EP4 separately.
    bool source_recovery_remove(std::uint64_t uid,bool allow_active);
    bool source_recovery_set_active_icon(WorldEntityReference);
    bool source_recovery_set_action_icon(std::uint64_t uid,WorldEntityReference);
    // Main restart changes the VM epoch before native Person clears its active
    // header. Preserve that header/pending actions for EP3; the whole recovery
    // must finish before ticking, publishing or saving this transient state.
    bool source_recovery_rebase();
    // TS1 invokes the skipped object's EntryPoint 4 after removing the item.
    // The host supplies that synchronous operation; it is not persisted. The
    // callback must not reenter this queue; false/throw reports failure after
    // removal and does not restore the action.
    void set_queue_skipped_handler(std::function<bool(const InteractionActionSnapshot&)> handler) {
        if(executing_)throw std::logic_error("queue-skipped handler cannot reenter controller");
        queue_skipped_handler_=std::move(handler);
    }
    // Last consumed native identity (not the wrapping public queue UID). Only
    // use for request tracking after successful enqueue: a rejected insertion
    // with irreversible followup effects also consumes its published identity.
    std::uint64_t last_enqueued_uid() const {return sequence_;}
    bool contains_uid(std::uint64_t uid) const {
        return std::any_of(queue_.begin(),queue_.end(),[uid](const auto& action){return action.uid==uid;});
    }
    bool references_target(std::int16_t entity) const {
        return std::any_of(queue_.begin(),queue_.end(),[&](const InteractionActionSnapshot& action){
            return action.call.context.callee==entity;
        });
    }
    // Explicit synchronous check handlers, separate from asynchronous services.
    void set_check_primitive(VmPrimitive primitive) {check_primitive_=std::move(primitive);}
    void set_check_primitive_factory(std::function<VmPrimitive(const VmMemory&)> factory) {
        check_primitive_factory_=std::move(factory);
    }
    VmPrimitive primitive();
    bool observe(); // Validates identities and removes completion in the same host tick.
    // Stage a consecutive clock tick. Deferred staging leaves priority and wait
    // servicing to the host's actual scheduled-thread boundary.
    bool tick(std::uint64_t,bool defer_wait=false);
    bool service_wait(); // Same clock; normal return/deadline/interrupt service.
    bool has_immediate() const;
    // True includes no eligible action (or a protected first immediate). False
    // reports an unsupported transition. Check side effects remain source-visible.
    // The park callback runs after VM publication; failure is a fatal boundary,
    // not a rollback of the injected VM frame.
    bool try_run_immediate(const ImmediatePendingAudit& audit={},
        const std::function<bool(std::uint64_t)>& park_after_injection={});
    bool park_immediate_parent_wait(std::uint64_t ticket);
    bool retry_immediate_parent(std::uint64_t ticket);
    void set_immediate_parent_resume(std::function<bool(std::uint64_t)> resume) {
        if(executing_)throw std::logic_error("immediate resume handler cannot reenter controller");
        immediate_parent_resume_=std::move(resume);
    }
    void set_schedule_idle_clock(std::function<std::optional<std::uint64_t>()> read,
                                 std::function<void(std::uint64_t)> write) {
        if(executing_)throw std::logic_error("idle clock handler cannot reenter controller");
        schedule_idle_clock_read_=std::move(read);schedule_idle_clock_write_=std::move(write);
    }
    bool interrupt(); // Main idle only; not action cancellation.
    bool notify(); // Resume this entity's waiting IdleForInput instruction immediately.
    std::size_t size() const {return queue_.size();} // Includes active front.
    bool bound_to(const Vm& vm) const noexcept {return vm_==&vm;}
    bool active() const {return active_.has_value();}
    bool waiting() const {return wait_.has_value();}
    const std::optional<InteractionCompletion>& completion() const {return completion_;}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    friend class EntityThreadControllers;
    using Action=InteractionActionSnapshot;
    using Active=InteractionActiveSnapshot;
    using Wait=InteractionWaitSnapshot;
    std::optional<bool> live_action_tree(const Vm&) const;
    bool valid_snapshot(const InteractionQueueSnapshot&) const;
    bool valid_snapshot_with_vm(const InteractionQueueSnapshot&,const VmSnapshot&) const;
    void publish_snapshot(InteractionQueueSnapshot&&) noexcept;
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    PrimitiveOutcome detached_attempt_push(const Action&,const VmFrame&,const Instruction&,const VmDispatch&,
        const VmInstructionLease&,std::uint64_t source_run,const std::optional<VmFrameWrite>&);
    bool validate();
    // Producer-only derived header binding after an actual source queue edit.
    // Never called by decode/validation to repair an invalid saved UID.
    void rebind_current_source_headers() noexcept;
    bool evaluate_queue_priorities();
    bool enqueue_impl(RoutineKey,VmContext,const std::vector<std::int16_t>&,
        std::int16_t,InteractionQueueMode,std::uint32_t,bool,bool,bool,std::optional<RoutineKey>,bool,
        std::optional<std::int32_t>,InteractionArgumentMode=InteractionArgumentMode::legacy_metadata,
        InteractionCheckContext=InteractionCheckContext::default_zero,
        std::optional<InteractionIconOwnerSnapshot> icon_owner={},std::optional<InteractionLabel> label={});
    World& world_;std::int16_t actor_id_;std::size_t capacity_;
    std::function<bool()> thread_interrupt_;
    std::function<bool(const InteractionActionSnapshot&)> queue_skipped_handler_;
    VmPrimitive check_primitive_;
    std::function<VmPrimitive(const VmMemory&)> check_primitive_factory_;
    std::function<bool(std::uint64_t)> immediate_parent_resume_;
    std::function<std::optional<std::uint64_t>()> schedule_idle_clock_read_;
    std::function<void(std::uint64_t)> schedule_idle_clock_write_;
    bool immediate_parking_=false,immediate_resuming_=false;
    Vm* vm_=nullptr;std::uint64_t actor_generation_=0,run_generation_=0,sequence_=0;
    std::uint64_t clock_=0,idle_start_=0;std::optional<std::uint64_t> last_tick_,resume_tick_;
    std::deque<Action> queue_;std::vector<Active> suspended_active_;
    std::optional<Active> active_;std::optional<Wait> wait_;
    bool eligibility_check_active_=false;

    InteractionQueueProfile profile_=InteractionQueueProfile::ts1_priority;
    std::uint16_t next_queue_uid_=0;
    std::optional<InteractionCompletion> completion_;bool interrupted_=false,executing_=false;
    std::string diagnostic_;
    bool fresh_=true;
};
// Native TS1 PushInteraction priority decoding and ordered enqueue path.
// Resource lookup and resident-thread ownership are supplied by the host; this
// primitive never creates another VM or substitutes the primary resident.
struct Ts1PushInteractionRequest {
    std::int16_t actor,source;
    std::uint8_t interaction;
    bool push_head,push_tail;
    std::uint8_t priority_code;
    std::int16_t priority;
    InteractionQueueMode mode;
    // The host may use this for presentation; it does not affect execution.
    std::optional<std::int16_t> custom_icon;
};
struct Ts1PushInteractionAction {
    RoutineKey key;CodeOwner owner;std::uint32_t flags=0;
    std::optional<RoutineKey> check={};
    std::optional<InteractionLabel> label={};
};
struct Ts1PushInteractionTarget {Vm* vm=nullptr;InteractionQueue* queue=nullptr;};
using Ts1PushInteractionLookup=std::function<std::optional<Ts1PushInteractionAction>(const Ts1PushInteractionRequest&)>;
// The original live dispatch lets the host authenticate same-VM owner admission.
using Ts1PushInteractionContextLookup=std::function<std::optional<Ts1PushInteractionAction>(
    const Ts1PushInteractionRequest&,const VmFrame&,const Instruction&,const VmDispatch&)>;
using Ts1PushInteractionThread=std::function<Ts1PushInteractionTarget(std::int16_t)>;
// TS1 GetAction selects TTAB.TTAIndex (label_index), not serialized row order.
// Missing/undecoded tables return null; duplicate identifiers are malformed.
const Interaction* ts1_interaction_by_index(const Ttab&,std::uint8_t);
VmPrimitive ts1_push_interaction_primitive(const World&,Ts1PushInteractionLookup,Ts1PushInteractionThread);
VmPrimitive ts1_push_interaction_primitive(const World&,Ts1PushInteractionContextLookup,Ts1PushInteractionThread);
}
