#pragma once
#include "odh/world.hpp"

namespace odh {
struct SleepActorSnapshot {std::int16_t id;std::uint64_t generation,run_generation;};
struct SleepJobSnapshot {std::uint64_t ticket,idle_start,deadline;std::size_t argument;};
struct SleepSnapshot {
    std::optional<SleepActorSnapshot> actor;
    std::optional<SleepJobSnapshot> job;
    std::optional<std::uint64_t> last_tick;
    std::uint64_t clock=0;
    bool interrupted=false,notify_resume=false;
    std::string diagnostic;
};
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
    SleepSnapshot snapshot() const;
    // Restore into a fresh controller already bound to the restored VM and an
    // equivalent World, with equivalent RNG/interrupt callbacks. No callback,
    // pointer or VM state is restored. Invalid state returns false; allocation
    // exceptions propagate, leaving all destination values unchanged.
    bool restore_snapshot(const SleepSnapshot&);
    VmPrimitive primitive();
    void set_thread_interrupt_source(std::function<bool()> consume) {thread_interrupt_=std::move(consume);}
    bool tick(std::uint64_t);
    bool interrupt();
    // Release an active wait for normal, budgeted VM dispatch after Notify.
    bool notify();
    bool active() const {return job_.has_value();}
    const std::string& diagnostic() const {return diagnostic_;}
    std::uint64_t clock() const {return clock_;}
private:
    friend class EntityThreadControllers;
    using Actor=SleepActorSnapshot;
    using Job=SleepJobSnapshot;
    bool valid_snapshot(const SleepSnapshot&) const;
    void publish_snapshot(SleepSnapshot&&) noexcept;
    PrimitiveOutcome request(const VmFrame&,const Instruction&,const VmDispatch&);
    bool validate();
    bool evaluate();
    void random_cycle();
    World& world_;
    std::function<void()> rng_cycle_;
    std::function<bool()> thread_interrupt_;
    Vm* vm_=nullptr;
    std::optional<Actor> actor_;
    std::optional<Job> job_;
    std::optional<std::uint64_t> last_tick_;
    std::uint64_t clock_=0;
    bool interrupted_=false,notify_resume_=false,executing_=false;
    std::string diagnostic_;
    bool fresh_=true;
};
} // namespace odh
