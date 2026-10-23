#pragma once

#include "odh/interaction.hpp"
#include "odh/sleep.hpp"

namespace odh {
struct EntityThreadControllersSnapshot {
    SleepSnapshot sleep;
    InteractionQueueSnapshot interactions;
    bool thread_interrupt=false;
};

// Controllers that belong to one entity execution thread. Each instance owns
// an independent Sleep clock and interaction queue while sharing the World.
// World and the bound VM must stay valid while these controllers are used.
class EntityThreadControllers {
public:
    EntityThreadControllers(World& world,std::int16_t actor,
                            std::function<void()> rng_cycle={})
        :sleep_(world,std::move(rng_cycle)),interactions_(world,actor) {
        sleep_.set_thread_interrupt_source([this]{return consume_thread_interrupt();});
        interactions_.set_thread_interrupt_source([this]{return consume_thread_interrupt();});
    }
    EntityThreadControllers(const EntityThreadControllers&)=delete;
    EntityThreadControllers& operator=(const EntityThreadControllers&)=delete;
    EntityThreadControllers(EntityThreadControllers&&)=delete;
    EntityThreadControllers& operator=(EntityThreadControllers&&)=delete;

    bool bind(Vm& vm) {return sleep_.bind(vm)&&interactions_.bind(vm);}
    EntityThreadControllersSnapshot snapshot() const {
        return {sleep_.snapshot(),interactions_.snapshot(),thread_interrupt_};
    }
    // Both services must be fresh and already bound to the same restored VM.
    // Recreate equivalent World/resource/RNG callbacks before restoring. The
    // value-only snapshot omits these bindings. Stage both copies before any
    // publication so invalid values and allocation failures are atomic.
    bool restore_snapshot(const EntityThreadControllersSnapshot& state) {
        if(!fresh_||sleep_.vm_!=interactions_.vm_||!sleep_.valid_snapshot(state.sleep)||
           !interactions_.valid_snapshot(state.interactions))return false;
        auto candidate=state;
        sleep_.publish_snapshot(std::move(candidate.sleep));
        interactions_.publish_snapshot(std::move(candidate.interactions));
        thread_interrupt_=candidate.thread_interrupt;fresh_=false;
        return true;
    }
    VmPrimitive sleep_primitive() {return sleep_.primitive();}
    VmPrimitive interaction_primitive() {return interactions_.primitive();}
    bool advance(std::uint64_t tick) {
        fresh_=false;
        return interactions_.tick(tick)&&sleep_.tick(tick);
    }
    bool observe() {fresh_=false;return interactions_.observe();}
    bool notify() {
        fresh_=false;
        thread_interrupt_=true;
        if(sleep_.active())return sleep_.notify();
        if(interactions_.waiting())return interactions_.notify();
        return true;
    }
    Sleep& sleep() {return sleep_;}
    InteractionQueue& interactions() {return interactions_;}
    bool thread_interrupted() const {return thread_interrupt_;}

private:
    bool consume_thread_interrupt() {
        const bool value=thread_interrupt_;thread_interrupt_=false;return value;
    }
    Sleep sleep_;
    InteractionQueue interactions_;
    bool thread_interrupt_=false;
    bool fresh_=true;
};

} // namespace odh
