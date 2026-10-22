#pragma once

#include "odh/vm.hpp"
#include <map>

namespace odh {

// A deterministic, bounded host scheduler for independent entity VM states.
// It shares one logical tick, runs controller hooks at the tick boundaries,
// and caps both aggregate and per-entity VM instruction work.
class EntityVmScheduler {
public:
    using BeforeRun = std::function<bool(std::uint64_t)>;
    using AfterRun = std::function<bool()>;
    using Trace = std::function<void(std::int16_t,const VmTrace&)>;

    explicit EntityVmScheduler(std::size_t entity_limit=64):entity_limit_(entity_limit) {}
    bool add(std::int16_t entity,Vm&,BeforeRun before={},AfterRun after={});
    bool remove(std::int16_t entity);
    bool tick(std::uint64_t tick,std::uint64_t instruction_budget,
              std::uint64_t per_entity_budget,const Trace& trace={});
    std::size_t size() const {return entities_.size();}
    std::uint64_t instructions_last_tick() const {return instructions_last_tick_;}
    const std::string& diagnostic() const {return diagnostic_;}
private:
    struct Entity {Vm* vm;BeforeRun before;AfterRun after;};
    std::map<std::int16_t,Entity> entities_;
    std::size_t entity_limit_;
    // When a very small global budget cannot give every entity one instruction,
    // this cursor rotates which IDs receive a slice. Dispatch order stays sorted.
    std::optional<std::int16_t> budget_cursor_;
    std::optional<std::uint64_t> last_tick_;
    std::uint64_t instructions_last_tick_=0;
    bool ticking_=false,faulted_=false;
    std::string diagnostic_;
};

} // namespace odh
