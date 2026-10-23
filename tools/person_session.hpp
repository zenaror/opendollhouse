#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace odh_host {

// Window sessions renew bounded work grants each tick. Diagnostic probes keep
// their historical total budget, including bootstrap, for reproducible output.
struct PersonSessionPolicy {
    bool windowed=false,explicit_ticks=false;
    std::uint64_t requested_ticks=256;
    static constexpr std::uint64_t instructions=10000;
    static constexpr std::uint64_t max_probe_instructions=1000000;
    std::uint64_t probe_instructions=instructions;
    std::uint64_t tick_limit() const {
        return windowed&&!explicit_ticks?std::numeric_limits<std::uint32_t>::max():requested_ticks;
    }
    std::uint64_t entity_budget(std::uint64_t total_steps) const {
        return windowed?instructions:(total_steps<probe_instructions?probe_instructions-total_steps:0);
    }
    std::uint64_t tick_budget(std::uint64_t total_steps,std::size_t entities) const {
        // The host scheduler accepts at most 64 entities. Keep that aggregate
        // bound here as well, including for malformed policy callers.
        return windowed?instructions*(entities<64?entities:64):entity_budget(total_steps);
    }
    bool instruction_limit_reached(std::uint64_t total_steps) const {
        return !windowed&&total_steps>=probe_instructions;
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
