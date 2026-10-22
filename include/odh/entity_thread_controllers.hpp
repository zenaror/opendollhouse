#pragma once

#include "odh/interaction.hpp"
#include "odh/sleep.hpp"

namespace odh {

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
    VmPrimitive sleep_primitive() {return sleep_.primitive();}
    VmPrimitive interaction_primitive() {return interactions_.primitive();}
    bool advance(std::uint64_t tick) {
        return interactions_.tick(tick)&&sleep_.tick(tick);
    }
    bool observe() {return interactions_.observe();}
    bool notify() {
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
};

} // namespace odh
