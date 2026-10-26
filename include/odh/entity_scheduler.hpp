#pragma once

#include "odh/vm.hpp"
#include <map>
#include <vector>

namespace odh {

// Value-only tick-boundary continuation. Recreate equivalent World/VM state and
// hooks before restoring; no pointer, code/resource or callback is serialized.
struct EntityVmSchedulerSnapshot {
    struct Entity {
        std::int16_t id = 0;
        std::uint64_t run_generation = 0, steps = 0;
        VmStatus status = VmStatus::ready;
        bool before = false, after = false, interrupt = false;
    };
    std::vector<Entity> entities; // Strictly ascending IDs.
    std::size_t entity_limit = 0;
    std::optional<std::int16_t> budget_cursor;
    std::optional<std::uint64_t> last_tick;
    std::uint64_t instructions_last_tick = 0;
    bool faulted = false;
    std::string diagnostic;
};

// A deterministic, bounded host scheduler for independent entity VM states.
// It shares one logical tick, runs controller hooks at the tick boundaries,
// and caps both aggregate and per-entity VM instruction work.
class EntityVmScheduler {
public:
    using BeforeRun = std::function<bool(std::uint64_t)>;
    using AfterRun = std::function<bool()>;
    using Interrupt = std::function<bool()>;
    using Trace = std::function<void(std::int16_t,const VmTrace&)>;

    explicit EntityVmScheduler(std::size_t entity_limit=64):entity_limit_(entity_limit) {}
    EntityVmSchedulerSnapshot snapshot() const;
    // Register rebuilt VMs/hooks first. Restore only before runtime operations;
    // configured entity limit is a ceiling. False or allocation failure leaves
    // destination untouched and fresh. Capture/restore during tick throws.
    bool restore_snapshot(const EntityVmSchedulerSnapshot&);
    bool add(std::int16_t entity,Vm&,BeforeRun before={},AfterRun after={},Interrupt interrupt={});
    bool remove(std::int16_t entity);
    // Request an interrupt for a registered thread. During a tick its
    // controller runs at the next VM safe point; a higher ID can still run in
    // the current tick, while a lower or equal ID waits for the next pass.
    bool interrupt(std::int16_t entity);
    bool tick(std::uint64_t tick,std::uint64_t instruction_budget,
              std::uint64_t per_entity_budget,const Trace& trace={});
    std::size_t size() const {return entities_.size();}
    // Ephemeral execution phase, not persistent session state. Synchronous
    // checks share callee registers during a tick and clone them outside it.
    bool running() const {return ticking_;}
    std::uint64_t instructions_last_tick() const {return instructions_last_tick_;}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    struct Entity {Vm* vm;BeforeRun before;AfterRun after;Interrupt interrupt;};
    std::map<std::int16_t,Entity> entities_;
    std::size_t entity_limit_;
    // When a very small global budget cannot give every entity one instruction,
    // this cursor rotates which IDs receive a slice. Dispatch order stays sorted.
    std::optional<std::int16_t> budget_cursor_;
    std::optional<std::int16_t> current_entity_;
    std::vector<std::int16_t> same_tick_ready_;
    std::vector<std::int16_t> pending_interrupts_;
    std::optional<std::uint64_t> last_tick_;
    std::uint64_t instructions_last_tick_=0;
    bool ticking_=false,faulted_=false,fresh_=true;
    std::string diagnostic_;
    void process_pending_interrupts();
};

} // namespace odh
