#pragma once
#include "odh/world.hpp"

namespace odh {
// Bounded clocked adapter for FreeSO 4c6b3e8 VMSleep (opcode 0). World and the
// single bound VM must outlive this stable controller. Dead-caller next-tick
// behavior is outside this World profile. An absent RNG callback is reported.
// Unlike reference VM stepping, scheduled reevaluation retains one pending
// ticket and does not add instruction dispatches to Vm::steps().
class Sleep {
public:
    explicit Sleep(World& world,std::function<void()> rng_cycle={})
        :world_(world),rng_cycle_(std::move(rng_cycle)) {}
    Sleep(const Sleep&)=delete;
    Sleep& operator=(const Sleep&)=delete;
    Sleep(Sleep&&)=delete;
    Sleep& operator=(Sleep&&)=delete;
    bool bind(Vm&);
    VmPrimitive primitive();
    bool tick(std::uint64_t);
    bool interrupt();
    // Release an active wait for normal, budgeted VM dispatch after Notify.
    bool notify();
    bool active() const {return job_.has_value();}
    const std::string& diagnostic() const {return diagnostic_;}
    std::uint64_t clock() const {return clock_;}
private:
    struct Actor {std::int16_t id;std::uint64_t generation,run_generation;};
    struct Job {std::uint64_t ticket,idle_start,deadline;std::size_t argument;};
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool validate();
    bool evaluate();
    void random_cycle();
    World& world_;
    std::function<void()> rng_cycle_;
    Vm* vm_=nullptr;
    std::optional<Actor> actor_;
    std::optional<Job> job_;
    std::optional<std::uint64_t> last_tick_;
    std::uint64_t clock_=0;
    bool interrupted_=false,notify_resume_=false,executing_=false;
    std::string diagnostic_;
};
} // namespace odh
