#pragma once

#include "odh/hit_code_target.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace odh {

// Both resources are borrowed and immutable for the catalog's entire lifetime.
// The caller supplies their load order; no filesystem scan or priority is used.
struct HitEntryGroup {
    const HitFile* hit = nullptr;
    const Hot* hot = nullptr;
};

struct HitEntryCatalogLimits {
    std::size_t groups = 64;
    std::uint64_t track_data_rows = 65536; // Includes parser-counted same-file replacements.
    std::uint64_t serialized_bytes = 16 * 1024 * 1024;
    std::size_t collision_records = 4096;
    std::size_t event_name_bytes = 256;
};

struct HitEntryCatalogUsage {
    std::size_t groups = 0;
    std::uint64_t track_data_rows = 0;
    std::uint64_t serialized_bytes = 0;
    std::size_t unique_keys = 0;
    std::size_t collision_records = 0;
};

struct HitEntryCollision {
    std::uint32_t key = 0;
    std::size_t previous_group = 0, replacement_group = 0;
    std::uint32_t previous_pc = 0, replacement_pc = 0;
};

struct ResolvedNativeHitEvent {
    std::size_t event_group = 0;
    std::uint32_t track_id = 0, entry_key = 0;
    const HotEvent* event = nullptr;
    const HotTrack* track = nullptr;
    HitCodeTarget entry;
};

// Immutable, resource-aware TrackData index. Each value retains the HIT paired
// with the HOT that supplied it. A later group replaces a duplicate key;
// the caller must determine and provide the group order. Same-file row policy
// belongs to read_hot's selected profile. HIT ENTP remains
// separate: this catalog does not merge it or use it as a fallback.
// Per-file parser limits still apply before construction. These aggregate
// limits bound parsed rows (including replacements) and serialized resource
// extents, not C++ RSS. Collision records describe cross-group replacements.
class HitEntryCatalog {
public:
    explicit HitEntryCatalog(const std::vector<HitEntryGroup>&,
                             HitEntryCatalogLimits = {});

    std::optional<HitCodeTarget> lookup(std::uint32_t key) const noexcept;
    // Event registration/group choice is explicit. This only resolves the
    // type-1 event and Track from that same selected HOT, then its global
    // entry key. Native global Track/Patch replacement instead belongs to
    // NativeHitObjectCatalog::resolve_global_track_event. This local lookup
    // does not choose a patch,
    // hitlist, group admission, or source. The returned metadata is borrowed.
    ResolvedNativeHitEvent resolve_event(std::size_t event_group,
                                         std::string_view name,
                                         bool loop = false) const;

    const HitEntryCatalogUsage& usage() const noexcept { return usage_; }
    const std::vector<HitEntryCollision>& collisions() const noexcept { return collisions_; }

private:
    struct Entry { std::size_t group; std::uint32_t pc; };
    std::vector<HitEntryGroup> groups_;
    std::map<std::uint32_t, Entry> entries_;
    std::vector<HitEntryCollision> collisions_;
    HitEntryCatalogLimits limits_;
    HitEntryCatalogUsage usage_;
};

} // namespace odh
