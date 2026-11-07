#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace odh_host {

// Presentation does not decide how much VM work a true simulation tick gets.
// Integrated gameplay can renew the same bounded grants with or without a
// window. Standalone diagnostic probes retain their historical total budget.
enum class PersonWorkBudgetPolicy { total_probe,per_tick_gameplay };
struct PersonSessionPolicy {
    bool windowed=false,explicit_ticks=false;
    std::uint64_t requested_ticks=256;
    static constexpr std::uint64_t instructions=10000;
    static constexpr std::uint64_t max_probe_instructions=1000000;
    std::uint64_t probe_instructions=instructions;
    // Trailing optional field preserves existing aggregate callers: absence
    // retains their legacy windowed/gameplay versus nonwindowed/probe choice.
    std::optional<PersonWorkBudgetPolicy> work_budget_policy;
    bool renews_work_budget() const noexcept {
        return work_budget_policy?*work_budget_policy==PersonWorkBudgetPolicy::per_tick_gameplay:windowed;
    }
    std::uint64_t tick_limit() const {
        return windowed&&!explicit_ticks?std::numeric_limits<std::uint32_t>::max():requested_ticks;
    }
    std::uint64_t entity_budget(std::uint64_t total_steps) const {
        return renews_work_budget()?instructions:(total_steps<probe_instructions?probe_instructions-total_steps:0);
    }
    std::uint64_t tick_budget(std::uint64_t total_steps,std::size_t entities) const {
        if(!renews_work_budget())return entity_budget(total_steps);
        // Fund every registered entity's bounded slice. A smaller global cap
        // makes the scheduler's fair-share division truncate legitimate VM
        // work on large lots, even when later sleeping entities leave work
        // unused. The per-entity grant remains unchanged; this is not a new
        // source VM loop limit. Saturate the aggregate without wrapping.
        constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
        return entities>maximum/instructions?maximum:
            instructions*static_cast<std::uint64_t>(entities);
    }
    bool instruction_limit_reached(std::uint64_t total_steps) const {
        return !renews_work_budget()&&total_steps>=probe_instructions;
    }
    bool limit_reached(std::uint64_t ticks,std::uint64_t total_steps) const {
        return ticks>=tick_limit()||instruction_limit_reached(total_steps);
    }
};

enum class PersonDiagnostic : std::size_t {
    trace,neighbor_reads,global_reads,tuning_reads,random_reads,zoning_reads,
    neighbor_next,entity_next,relationship,inventory,animation,sound,entry_point5,
    route_movement,count
};

// Preserve bounded diagnostics without changing VM outcomes. Count-only logs
// retain a prefix; byte limits may drop a large record and retain a later small
// one. The fixed categories and saturating counters have constant storage.
class PersonDiagnosticRetention {
public:
    static constexpr std::array<const char*,static_cast<std::size_t>(PersonDiagnostic::count)> names={
        "trace","neighbor_reads","global_reads","tuning_reads","random_reads","zoning_reads",
        "neighbor_next","entity_next","relationship","inventory","animation","sound","entry_point5",
        "route_movement"};
    bool retain(PersonDiagnostic kind,std::size_t size,std::size_t limit,
                std::uint64_t bytes=0,std::uint64_t retained_bytes=0,
                std::uint64_t byte_limit=std::numeric_limits<std::uint64_t>::max()) {
        if(size<limit&&retained_bytes<=byte_limit&&bytes<=byte_limit-retained_bytes)return true;
        auto& count=dropped_[static_cast<std::size_t>(kind)];
        if(count!=std::numeric_limits<std::uint64_t>::max())++count;
        return false;
    }
    const auto& dropped() const {return dropped_;}
    bool truncated() const {for(const auto value:dropped_)if(value)return true;return false;}
private:
    std::array<std::uint64_t,static_cast<std::size_t>(PersonDiagnostic::count)> dropped_{};
};

} // namespace odh_host
