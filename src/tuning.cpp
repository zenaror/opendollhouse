#include "odh/tuning.hpp"
#include <stdexcept>
#include <utility>

namespace odh {
namespace {
void count_tables(const TuningTables& tables, const TuningLimits& limits,
                  std::size_t& table_count, std::size_t& constant_count) {
    if (tables.size() > limits.tables - table_count)
        throw std::invalid_argument("BCON tuning table limit exceeded");
    table_count += tables.size();
    for (const auto& entry : tables) {
        if (entry.second.constants.size() > 255)
            throw std::invalid_argument("BCON tuning table exceeds byte-sized constant count");
        if (entry.second.constants.size() > limits.constants - constant_count)
            throw std::invalid_argument("BCON tuning constant limit exceeded");
        constant_count += entry.second.constants.size();
    }
}
} // namespace

BconTuning::BconTuning(TuningResources resources, TuningLimits limits)
    : resources_(std::move(resources)) {
    if (resources_.owners.size() > limits.owners)
        throw std::invalid_argument("BCON tuning owner limit exceeded");
    std::size_t tables = 0, constants = 0;
    count_tables(resources_.globals, limits, tables, constants);
    for (const auto& entry : resources_.owners) {
        const auto& owner = entry.second;
        switch (owner.semiglobal_state) {
        case TuningSemiState::unknown:
        case TuningSemiState::absent:
            if (!owner.semiglobal_tables.empty())
                throw std::invalid_argument("BCON tuning semiglobal state contradicts tables");
            break;
        case TuningSemiState::loaded: break;
        default: throw std::invalid_argument("invalid BCON tuning semiglobal state");
        }
        count_tables(owner.private_tables, limits, tables, constants);
        count_tables(owner.semiglobal_tables, limits, tables, constants);
    }
}

TuningAddress BconTuning::address(std::int16_t query) {
    const auto bits = static_cast<std::uint16_t>(query);
    const auto table = static_cast<std::uint16_t>(bits >> 7);
    const auto index = static_cast<std::uint8_t>(bits & 127);
    if (table < 64) return {RoutineScope::private_object, static_cast<std::uint16_t>(4096 + table), index};
    if (table < 128) return {RoutineScope::semiglobal, static_cast<std::uint16_t>(8192 + table - 64), index};
    if (table < 192) return {RoutineScope::global, static_cast<std::uint16_t>(256 + table - 128), index};
    // Legacy extended encoding retains the raw table (including signed query bits).
    return {RoutineScope::private_object, static_cast<std::uint16_t>(4096 + table), index};
}

std::optional<TuningValue> BconTuning::read(CodeOwner owner, std::int16_t query) const {
    const auto location = address(query);
    const TuningTables* tables = &resources_.globals;
    bool fallback = false;
    if (location.scope != RoutineScope::global) {
        const auto found = resources_.owners.find(owner);
        if (found == resources_.owners.end()) return std::nullopt;
        const auto& bank = found->second;
        tables = &bank.private_tables;
        if (location.scope == RoutineScope::semiglobal) {
            if (bank.semiglobal_state == TuningSemiState::unknown) return std::nullopt;
            fallback = bank.semiglobal_state == TuningSemiState::absent;
            if (!fallback) tables = &bank.semiglobal_tables;
        }
    }
    const auto table = tables->find(location.table);
    if (table == tables->end() || location.index >= table->second.constants.size()) return std::nullopt;
    const auto word = table->second.constants[location.index];
    const auto value = word < 0x8000 ? static_cast<std::int16_t>(word)
        : static_cast<std::int16_t>(static_cast<std::int32_t>(word) - 0x10000);
    return TuningValue{value, location, owner, fallback};
}
} // namespace odh
