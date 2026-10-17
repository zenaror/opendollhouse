#pragma once

#include "odh/hit_entry_catalog.hpp"
#include "odh/hit_object_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace odh {

// Caller-supplied load order. Views and their seekable streams need only live
// through construction; no filesystem, executable, archive, or CD is opened.
struct HitResourceGroupInput {
    View hsm;
    View hit;
    View hot;
};

struct HitResourceBankLimits {
    HitResourceLimits per_file{};
    HitEntryCatalogLimits catalog{};
    std::uint64_t source_bytes = 16 * 1024 * 1024; // Sum of all three View extents.
    std::uint64_t symbol_records = 65536; // Raw HSM rows, including duplicates.
    std::size_t symbol_entries = 32768; // Unique, case-sensitive names.
    std::uint64_t symbol_name_bytes = 1024 * 1024; // Unique name byte lengths.
    HitObjectCatalogLimits objects{};
};

struct HitResourceBankUsage {
    std::size_t groups = 0;
    std::uint64_t source_bytes = 0;
    std::uint64_t symbol_records = 0;
    std::size_t symbol_entries = 0;
    std::uint64_t symbol_replacements = 0;
    std::uint64_t symbol_name_bytes = 0;
};

// Owns HIT bytes and parsed HOT metadata. Its final HSM namespace contains
// last-wins values, while a HOT's resolved numeric fields keep the HSM
// snapshot available when that HOT was parsed. first_lines are local line
// numbers from the first group defining each exact name, not global offsets.
// Borrowed pointers obtained from hit(), hot(), entries() or objects() require the bank
// to stay alive. Noncopyable/nonmovable so paired HIT addresses remain stable.
class HitResourceBank {
public:
    explicit HitResourceBank(const std::vector<HitResourceGroupInput>&,
                             HitResourceBankLimits = {});
    HitResourceBank(const HitResourceBank&) = delete;
    HitResourceBank& operator=(const HitResourceBank&) = delete;
    HitResourceBank(HitResourceBank&&) = delete;
    HitResourceBank& operator=(HitResourceBank&&) = delete;

    const HitFile& hit(std::size_t index) const;
    const Hot& hot(std::size_t index) const;
    const Hsm& symbols() const noexcept { return symbols_; }
    const HitEntryCatalog& entries() const noexcept { return *entries_; }
    const NativeHitObjectCatalog& objects() const noexcept { return *objects_; }
    const HitResourceBankUsage& usage() const noexcept { return usage_; }

private:
    struct OwnedGroup { HitFile hit; Hot hot; };
    std::vector<OwnedGroup> groups_;
    Hsm symbols_;
    std::unique_ptr<HitEntryCatalog> entries_;
    std::unique_ptr<NativeHitObjectCatalog> objects_;
    HitResourceBankUsage usage_;
};

} // namespace odh
