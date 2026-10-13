#pragma once
#include "odh/world.hpp"
#include <deque>

namespace odh {
struct InteractionCompletion {std::uint64_t uid,ticket,tick;bool result;};
// Explicit runnable FIFO actions for one avatar/VM. This adapter implements the
// Main-only IdleForInput slice, not TTAB eligibility/priorities/cancellation.
// World and VM must outlive it. Host validates with observe before each run/budget slice and after completion, then
// tick once per consecutive simulation tick, before resuming VM execution.
class InteractionQueue {
public:
    InteractionQueue(World& world,std::int16_t actor,std::size_t capacity=32)
        :world_(world),actor_id_(actor),capacity_(capacity) {}
    bool bind(Vm&);
    bool enqueue(RoutineKey,VmContext,const std::vector<std::int16_t>& args={});
    VmPrimitive primitive();
    bool observe(); // Validates identities and removes completion in the same host tick.
    bool tick(std::uint64_t); // Enforces next-tick resume and wakes idle waits.
    bool interrupt(); // Main idle only; not action cancellation.
    std::size_t size() const {return queue_.size();} // Includes active front.
    bool active() const {return active_.has_value();}
    const std::optional<InteractionCompletion>& completion() const {return completion_;}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    struct Action {std::uint64_t uid,target_generation;VmCall call;};
    struct Active {std::uint64_t uid,ticket;};
    struct Wait {std::uint64_t ticket,deadline;};
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool validate();
    World& world_;std::int16_t actor_id_;std::size_t capacity_;
    Vm* vm_=nullptr;std::uint64_t actor_generation_=0,run_generation_=0,sequence_=0;
    std::uint64_t clock_=0,idle_start_=0;std::optional<std::uint64_t> last_tick_,resume_tick_;
    std::deque<Action> queue_;std::optional<Active> active_;std::optional<Wait> wait_;
    std::optional<InteractionCompletion> completion_;bool interrupted_=false,executing_=false;
    std::string diagnostic_;
};
}
