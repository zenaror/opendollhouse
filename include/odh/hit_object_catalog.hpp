#pragma once

#include "odh/hit_entry_catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace odh {

struct HitObjectCatalogLimits {
    std::size_t groups = 64;
    std::uint64_t object_records = 65536; // Raw Track/Patch registrations, including replacements.
    std::size_t collision_records = 4096;
    std::size_t event_name_bytes = 256;
};

struct HitObjectCatalogUsage {
    std::size_t groups = 0;
    std::uint64_t object_records = 0;
    std::size_t unique_keys = 0;
    std::size_t collision_records = 0;
    std::uint64_t same_kind_replacements = 0;
    std::uint64_t cross_kind_replacements = 0;
};

struct HitObjectCollision {
    std::uint32_t id = 0;
    HotObjectKind previous_kind = HotObjectKind::track;
    HotObjectKind replacement_kind = HotObjectKind::track;
    std::size_t previous_group = 0, replacement_group = 0;
    std::uint64_t previous_line = 0, replacement_line = 0;
};

// Exactly one metadata pointer is set for a known object kind. It and its HOT
// must outlive the catalog. A Track/Patch replacement can change the kind of
// an ID; callers must consult kind rather than treating every key as TrackID.
struct NativeHitObjectRef {
    std::uint32_t id = 0;
    HotObjectKind kind = HotObjectKind::track;
    std::size_t group = 0;
    std::uint64_t line = 0;
    const HotTrack* track = nullptr;
    const HotPatch* patch = nullptr;
};

struct NativeHitTrackRef {
    std::uint32_t id = 0;
    std::size_t group = 0;
    std::uint64_t line = 0;
    const HotTrack* track = nullptr;
};

struct NativeHitPatchRef {
    std::uint32_t id = 0;
    std::size_t group = 0;
    std::uint64_t line = 0;
    const HotPatch* patch = nullptr;
};

struct ResolvedGlobalHitTrack {
    NativeHitTrackRef object;
    std::uint32_t entry_key = 0;
    HitCodeTarget entry;
};

struct ResolvedGlobalNativeHitEvent {
    std::size_t event_group = 0;
    const HotEvent* event = nullptr;
    ResolvedGlobalHitTrack track;
};

// Replays only the native Track/Patch registrations in caller-supplied HOT
// group and source-line order. The original manager map holds other object
// types too; this index is limited to the two proven registration callbacks.
// Every Hot must have native_object_order engaged, even when empty. Borrowed
// HOT/HIT resources and entry_catalog must remain immutable and outlive this
// object; HitResourceBank can provide that ownership. The two catalogs must
// be built from the same group order. This does not choose event-group order,
// patch playback, source, admission, or a controller lifecycle.
class NativeHitObjectCatalog {
public:
    NativeHitObjectCatalog(const std::vector<HitEntryGroup>& groups,
                           const HitEntryCatalog& entry_catalog,
                           HitObjectCatalogLimits limits = {});

    std::optional<NativeHitObjectRef> lookup_object(std::uint32_t id) const noexcept;
    std::optional<NativeHitTrackRef> lookup_track(std::uint32_t id) const noexcept;
    std::optional<NativeHitPatchRef> lookup_patch(std::uint32_t id) const noexcept;

    // Track metadata follows the winning object registration by TrackID;
    // bytecode follows the separate global TrackData catalog by CSV7 key.
    // Neither lookup uses a local Track, PatchID, or HIT ENTP fallback.
    ResolvedGlobalHitTrack resolve_track(std::uint32_t id) const;
    // EventMapping is read only from the explicitly selected HOT group.
    // This does not infer a global namespace or automatic group selection.
    ResolvedGlobalNativeHitEvent resolve_global_track_event(
        std::size_t event_group, std::string_view name, bool loop = false) const;

    const HitObjectCatalogUsage& usage() const noexcept { return usage_; }
    const std::vector<HitObjectCollision>& collisions() const noexcept { return collisions_; }

private:
    struct Entry {
        HotObjectKind kind;
        std::size_t group;
        std::uint64_t line;
    };
    std::vector<HitEntryGroup> groups_;
    const HitEntryCatalog& entry_catalog_;
    std::map<std::uint32_t, Entry> objects_;
    std::vector<HitObjectCollision> collisions_;
    HitObjectCatalogLimits limits_;
    HitObjectCatalogUsage usage_;
};

} // namespace odh
