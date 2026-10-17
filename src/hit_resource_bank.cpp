#include "odh/hit_resource_bank.hpp"

#include <limits>
#include <string>
#include <utility>

namespace odh {
namespace {
[[noreturn]] void fail(const char* reason) {
    throw FormatError(std::string("HIT resource bank: ") + reason);
}
void charge(std::uint64_t& total, std::uint64_t amount, std::uint64_t limit,
            const char* reason) {
    if (total > limit || amount > limit - total) fail(reason);
    total += amount;
}
}

HitResourceBank::HitResourceBank(const std::vector<HitResourceGroupInput>& input,
                                 HitResourceBankLimits limits) {
    if (input.empty() || !limits.catalog.groups || input.size() > limits.catalog.groups ||
        !limits.source_bytes || !limits.symbol_records || !limits.symbol_entries ||
        !limits.symbol_name_bytes)
        fail("invalid group or bank budget");

    // Reject oversized source declarations before reserving storage for groups
    // or parsing any resource. View::read still detects a truncated stream.
    std::uint64_t catalog_source_bytes = 0;
    for (const auto& group : input) {
        if (group.hsm.size() > limits.per_file.input_bytes ||
            group.hsm.size() > limits.per_file.text_bytes ||
            group.hot.size() > limits.per_file.input_bytes ||
            group.hot.size() > limits.per_file.text_bytes ||
            group.hit.size() > limits.per_file.input_bytes ||
            group.hit.size() > limits.per_file.hit_bytes ||
            group.hsm.size() > std::numeric_limits<std::size_t>::max() ||
            group.hot.size() > std::numeric_limits<std::size_t>::max() ||
            group.hit.size() > std::numeric_limits<std::size_t>::max())
            fail("per-file source budget exceeded");
        charge(usage_.source_bytes, group.hsm.size(), limits.source_bytes,
               "aggregate source budget exceeded");
        charge(usage_.source_bytes, group.hit.size(), limits.source_bytes,
               "aggregate source budget exceeded");
        charge(usage_.source_bytes, group.hot.size(), limits.source_bytes,
               "aggregate source budget exceeded");
        charge(catalog_source_bytes, group.hit.size(), limits.catalog.serialized_bytes,
               "catalog serialized budget exceeded");
        charge(catalog_source_bytes, group.hot.size(), limits.catalog.serialized_bytes,
               "catalog serialized budget exceeded");
    }

    groups_.reserve(input.size());
    usage_.groups = input.size();
    for (const auto& group : input) {
        auto local_hsm = read_hsm(group.hsm, limits.per_file,
                                  HsmReadProfile::native_namespace);
        charge(usage_.symbol_records, local_hsm.records, limits.symbol_records,
               "symbol record budget exceeded");
        charge(symbols_.logical_bytes, local_hsm.logical_bytes, limits.source_bytes,
               "symbol byte budget exceeded");
        // Charge unique key bytes and entry count before inserting each name.
        // A failure destroys this partially built bank through RAII.
        for (const auto& [name, value] : local_hsm.constants) {
            const auto found = symbols_.constants.find(name);
            if (found == symbols_.constants.end()) {
                if (usage_.symbol_entries >= limits.symbol_entries)
                    fail("symbol entry budget exceeded");
                charge(usage_.symbol_name_bytes, name.size(), limits.symbol_name_bytes,
                       "symbol name byte budget exceeded");
                symbols_.constants.emplace(name, value);
                symbols_.first_lines.emplace(name, local_hsm.first_lines.at(name));
                ++usage_.symbol_entries;
            } else {
                found->second = value;
            }
        }
        // Local HSM rows are already merged into the snapshot. Do not give
        // HOT a pointer to a temporary local parser result.
        auto hit_file = read_hit(group.hit, limits.per_file);
        auto hot_file = read_hot(group.hot, &symbols_, limits.per_file,
                                 HotReadProfile::native_entry_map);
        groups_.push_back(OwnedGroup{std::move(hit_file), std::move(hot_file)});
    }
    symbols_.records = usage_.symbol_records;
    symbols_.duplicate_names = usage_.symbol_records - usage_.symbol_entries;
    usage_.symbol_replacements = symbols_.duplicate_names;

    std::vector<HitEntryGroup> paired;
    paired.reserve(groups_.size());
    for (const auto& group : groups_)
        paired.push_back(HitEntryGroup{&group.hit, &group.hot});
    entries_ = std::make_unique<HitEntryCatalog>(paired, limits.catalog);
}

const HitFile& HitResourceBank::hit(std::size_t index) const {
    if (index >= groups_.size()) fail("HIT group index outside bank");
    return groups_[index].hit;
}

const Hot& HitResourceBank::hot(std::size_t index) const {
    if (index >= groups_.size()) fail("HOT group index outside bank");
    return groups_[index].hot;
}

} // namespace odh
