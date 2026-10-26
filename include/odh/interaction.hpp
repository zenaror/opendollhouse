#pragma once
#include "odh/world.hpp"
#include <algorithm>
#include <deque>

namespace odh {
struct InteractionCompletion {std::uint64_t uid,ticket,tick;bool result;};
enum class InteractionQueueMode : std::uint8_t { normal, parent_idle, parent_exit, idle };
enum class InteractionQueueProfile : std::uint8_t { legacy_fifo, ts1_priority };
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
};
struct InteractionActiveSnapshot {std::uint64_t uid,ticket;};
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
};
// Native queue for one avatar/VM. The TS1 profile currently implements priority
// insertion and its persisted action metadata, while nested active prefixes,
// full eligibility, nested dispatch, and cancellation remain
// unsupported. World and VM must outlive it. Host observes before each run/budget
// slice and after completion, then ticks once per consecutive simulation tick.
class InteractionQueue {
public:
    InteractionQueue(World& world,std::int16_t actor,std::size_t capacity=32)
        :world_(world),actor_id_(actor),capacity_(capacity) {}
    bool bind(Vm&);
    InteractionQueueSnapshot snapshot() const;
    // Fresh controller, already bound to an equivalently restored World/VM,
    // with equivalent callbacks. Capacity is capped by the constructor value.
    // Invalid values return false; allocation failures throw. Neither changes
    // the destination. Capture/restore during controller or VM callbacks throws.
    bool restore_snapshot(const InteractionQueueSnapshot&);
    void set_thread_interrupt_source(std::function<bool()> consume) {thread_interrupt_=std::move(consume);}
    // Simple host queue request using inherited priority 1. A v1 FIFO-restored
    // queue retains FIFO semantics through this entry point.
    bool enqueue(RoutineKey,VmContext,const std::vector<std::int16_t>& args={},bool push_after_active=false);
    // Native PushInteraction path. A legacy v1 FIFO queue adopts priority order
    // on this first source action while preserving its existing action order.
    bool enqueue_ts1(RoutineKey,VmContext,const std::vector<std::int16_t>& args,
        std::int16_t priority,InteractionQueueMode mode,std::uint32_t flags,
        bool push_head,bool push_tail,std::optional<RoutineKey> check={});
    bool references_target(std::int16_t entity) const {
        return std::any_of(queue_.begin(),queue_.end(),[&](const InteractionActionSnapshot& action){
            return action.call.context.callee==entity;
        });
    }
    // Explicit synchronous check handlers, separate from asynchronous services.
    void set_check_primitive(VmPrimitive primitive) {check_primitive_=std::move(primitive);}
    VmPrimitive primitive();
    bool observe(); // Validates identities and removes completion in the same host tick.
    bool tick(std::uint64_t); // Enforces next-tick resume and wakes idle waits.
    bool interrupt(); // Main idle only; not action cancellation.
    bool notify(); // Resume this entity's waiting IdleForInput instruction immediately.
    std::size_t size() const {return queue_.size();} // Includes active front.
    bool active() const {return active_.has_value();}
    bool waiting() const {return wait_.has_value();}
    const std::optional<InteractionCompletion>& completion() const {return completion_;}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    friend class EntityThreadControllers;
    using Action=InteractionActionSnapshot;
    using Active=InteractionActiveSnapshot;
    using Wait=InteractionWaitSnapshot;
    bool valid_snapshot(const InteractionQueueSnapshot&) const;
    void publish_snapshot(InteractionQueueSnapshot&&) noexcept;
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool validate();
    bool evaluate_queue_priorities();
    bool enqueue_impl(RoutineKey,VmContext,const std::vector<std::int16_t>&,
        std::int16_t,InteractionQueueMode,std::uint32_t,bool,bool,bool,std::optional<RoutineKey>);
    World& world_;std::int16_t actor_id_;std::size_t capacity_;
    std::function<bool()> thread_interrupt_;
    VmPrimitive check_primitive_;
    Vm* vm_=nullptr;std::uint64_t actor_generation_=0,run_generation_=0,sequence_=0;
    std::uint64_t clock_=0,idle_start_=0;std::optional<std::uint64_t> last_tick_,resume_tick_;
    std::deque<Action> queue_;std::optional<Active> active_;std::optional<Wait> wait_;
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
};
struct Ts1PushInteractionTarget {Vm* vm=nullptr;InteractionQueue* queue=nullptr;};
using Ts1PushInteractionLookup=std::function<std::optional<Ts1PushInteractionAction>(const Ts1PushInteractionRequest&)>;
using Ts1PushInteractionThread=std::function<Ts1PushInteractionTarget(std::int16_t)>;
// TS1 GetAction selects TTAB.TTAIndex (label_index), not serialized row order.
// Missing/undecoded tables return null; duplicate identifiers are malformed.
const Interaction* ts1_interaction_by_index(const Ttab&,std::uint8_t);
VmPrimitive ts1_push_interaction_primitive(const World&,Ts1PushInteractionLookup,Ts1PushInteractionThread);
}
