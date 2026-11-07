#pragma once
#include "odh/world.hpp"

namespace odh {
struct SleepActorSnapshot {std::int16_t id;std::uint64_t generation,run_generation;};
struct SleepJobSnapshot {
    std::uint64_t ticket,idle_start,deadline;
    std::size_t argument;
    // Captured ScheduleTickIn override. Absent means the natural deadline;
    // later RunEveryFrame changes cannot alter an already scheduled wait.
    std::optional<std::uint64_t> next_evaluation{};
};
struct SleepSnapshot {
    std::optional<SleepActorSnapshot> actor;
    std::optional<SleepJobSnapshot> job;
    std::optional<std::uint64_t> last_tick;
    std::uint64_t clock=0;
    bool interrupted=false,notify_resume=false;
    std::string diagnostic;
};
// Bounded clocked adapter for FreeSO 4c6b3e8 VMSleep (opcode 0). World and the
// single bound VM must outlive this stable controller. Caller references may
// retain a dead body; natural completion then yields until the next tick.
// An absent RNG callback is reported.
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
    // Optional shared Simantics ScheduleIdleStart. A missing reader, or a
    // reader returning nullopt, preserves the standalone Sleep timing path.
    void set_schedule_idle_clock(std::function<std::optional<std::uint64_t>()> read,
                                 std::function<void(std::uint64_t)> write) {
        schedule_idle_clock_read_=std::move(read);schedule_idle_clock_write_=std::move(write);
    }
    // Sampled only when Sleep schedules/reschedules, like pinned7e76
    // VMScheduler.ScheduleTickIn. Callback must not reenter this controller.
    void set_run_every_frame_source(std::function<bool()> source) {run_every_frame_=std::move(source);}
    bool evaluated_this_tick() const {return evaluated_this_tick_;}
    // Start this controller on an already-running World clock. This is only
    // valid before the controller has processed any state or tick.
    // Optional actor binds a newly prepared Main before its first dispatch, so
    // a same-tick save owns an exact execution identity without running Sleep.
    bool join_at(std::uint64_t current_tick,std::optional<std::int16_t> actor = {});
    // Original source recovery, outside dispatch. Authenticate the exact live
    // actor, then retire only this service's wait; do not complete its VM ticket
    // or alter Args, RNG, clocks, queues or shared ScheduleIdleStart. An absent
    // actor is admitted only through the actual VM root caller identity.
    // Between discard and rebase, normal service work and snapshots are refused.
    bool discard_for_source_recovery(WorldEntityReference);
    // Pair with the successful discard after Main has been restarted without
    // dispatch. Retain clock/last_tick and bind the same actor to the newer run.
    // A missing Main may leave the restarted VM idle (returned_false).
    bool rebase_after_source_recovery(WorldEntityReference);
    // Read-only proof for the bound host while this service is retired. VM
    // bookkeeping may still exist; no Sleep job can publish its old ticket.
    bool source_recovery_detached(WorldEntityReference,const Vm&) const;
    // Headlined entities run every frame in the reference scheduler. Reevaluate
    // their wait even before its natural completion deadline.
    bool tick(std::uint64_t,bool defer_evaluation=false);
    // Service a job deferred by tick(now,true), without advancing the clock.
    bool service_due();
    // True only when the active job is due at this exact current/future clock
    // boundary. This does not evaluate or mutate it.
    bool dispatch_due(std::uint64_t now) const;
    // Park an already-injected immediate child while retaining its parent's
    // pending Sleep frame in Vm's immediate continuation.
    bool park_for_immediate(std::uint64_t parent_ticket);
    // After resume_immediate restores that exact parent Sleep pending ticket,
    // release it for normal VM dispatch without writing its frame or RNG.
    bool retry_immediate_parent(std::uint64_t parent_ticket);
    // Source-thread Notify only signals Interrupt. At the actual thread slot,
    // after immediate selection, release this exact job for budgeted dispatch.
    // This does not change Args, ScheduleIdleStart, RNG, or notify_resume.
    bool retry_notified_wait(std::uint64_t ticket);
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
    std::function<bool()> run_every_frame_;
    std::function<std::optional<std::uint64_t>()> schedule_idle_clock_read_;
    std::function<void(std::uint64_t)> schedule_idle_clock_write_;
    Vm* vm_=nullptr;
    std::optional<Actor> actor_;
    std::optional<Job> job_;
    std::optional<std::uint64_t> last_tick_;
    std::uint64_t clock_=0;
    bool interrupted_=false,notify_resume_=false,executing_=false;
    std::string diagnostic_;
    bool fresh_=true;
    bool evaluated_this_tick_=false;
    std::optional<std::uint64_t> source_recovery_run_;
};
} // namespace odh
