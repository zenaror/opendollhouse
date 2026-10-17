#include "odh/hit_object_catalog.hpp"

#include <string>

namespace odh {
namespace {
[[noreturn]] void fail(const char* reason) {
    throw FormatError(std::string("HIT object catalog: ") + reason);
}
}

NativeHitObjectCatalog::NativeHitObjectCatalog(
        const std::vector<HitEntryGroup>& groups,
        const HitEntryCatalog& entry_catalog, HitObjectCatalogLimits limits)
    : entry_catalog_(entry_catalog), limits_(limits) {
    if (groups.empty() || !limits_.groups || groups.size() > limits_.groups ||
        !limits_.object_records || !limits_.collision_records ||
        !limits_.event_name_bytes || limits_.event_name_bytes > 65536)
        fail("invalid group or object budget");

    // Charge every registration row, including same-file and cross-kind
    // replacements, before allocating the global index. Parsed maps alone
    // would lose both the native registration order and raw row count.
    usage_.groups = groups.size();
    for (const auto& group : groups) {
        if (!group.hit || !group.hot) fail("null borrowed resource");
        if (!group.hot->native_object_order)
            fail("native object registration order is absent");
        const auto& order = *group.hot->native_object_order;
        if (static_cast<std::uint64_t>(order.size()) >
            limits_.object_records - usage_.object_records)
            fail("object record budget exceeded");
        usage_.object_records += static_cast<std::uint64_t>(order.size());
        for (const auto& record : order) {
            if (!record.id) fail("zero object ID in native registration order");
            switch (record.kind) {
            case HotObjectKind::track:
                if (group.hot->tracks.find(record.id) == group.hot->tracks.end())
                    fail("Track registration has no parsed metadata");
                break;
            case HotObjectKind::patch:
                if (group.hot->patches.find(record.id) == group.hot->patches.end())
                    fail("Patch registration has no parsed metadata");
                break;
            default:
                fail("invalid native object kind");
            }
        }
    }

    groups_ = groups;
    for (std::size_t group_index = 0; group_index < groups_.size(); ++group_index) {
        for (const auto& record : *groups_[group_index].hot->native_object_order) {
            const Entry replacement{record.kind, group_index, record.line};
            const auto [position, inserted] = objects_.emplace(record.id, replacement);
            if (inserted) continue;
            if (collisions_.size() >= limits_.collision_records)
                fail("collision record budget exceeded");
            const auto previous = position->second;
            collisions_.push_back({record.id, previous.kind, record.kind,
                                   previous.group, group_index, previous.line,
                                   record.line});
            if (previous.kind == record.kind) ++usage_.same_kind_replacements;
            else ++usage_.cross_kind_replacements;
            position->second = replacement;
        }
    }
    usage_.unique_keys = objects_.size();
    usage_.collision_records = collisions_.size();
}

std::optional<NativeHitObjectRef> NativeHitObjectCatalog::lookup_object(
        std::uint32_t id) const noexcept {
    const auto found = objects_.find(id);
    if (found == objects_.end()) return std::nullopt;
    const auto& entry = found->second;
    NativeHitObjectRef result{id, entry.kind, entry.group, entry.line, nullptr, nullptr};
    const auto& hot = *groups_[entry.group].hot;
    if (entry.kind == HotObjectKind::track)
        result.track = &hot.tracks.find(id)->second;
    else
        result.patch = &hot.patches.find(id)->second;
    return result;
}

std::optional<NativeHitTrackRef> NativeHitObjectCatalog::lookup_track(
        std::uint32_t id) const noexcept {
    const auto object = lookup_object(id);
    if (!object || object->kind != HotObjectKind::track) return std::nullopt;
    return NativeHitTrackRef{id, object->group, object->line, object->track};
}

std::optional<NativeHitPatchRef> NativeHitObjectCatalog::lookup_patch(
        std::uint32_t id) const noexcept {
    const auto object = lookup_object(id);
    if (!object || object->kind != HotObjectKind::patch) return std::nullopt;
    return NativeHitPatchRef{id, object->group, object->line, object->patch};
}

ResolvedGlobalHitTrack NativeHitObjectCatalog::resolve_track(std::uint32_t id) const {
    const auto object = lookup_object(id);
    if (!object) fail("TrackID absent from global object catalog");
    if (object->kind != HotObjectKind::track)
        fail("TrackID resolves to a different object kind");
    const auto& track = *object->track;
    if (!track.unsupported_reason.empty() || !track.hitlist_id)
        fail("Track metadata or entry key is unresolved");
    const auto entry = entry_catalog_.lookup(*track.hitlist_id);
    if (!entry) fail("Track entry key absent from global TrackData catalog");
    return {{id, object->group, object->line, object->track},
            *track.hitlist_id, *entry};
}

ResolvedGlobalNativeHitEvent NativeHitObjectCatalog::resolve_global_track_event(
        std::size_t event_group, std::string_view name, bool loop) const {
    if (event_group >= groups_.size()) fail("event group outside object catalog");
    if (loop) fail("loop request outside supported native event profile");
    if (name.empty() || name.size() > limits_.event_name_bytes ||
        name.find('\0') != std::string_view::npos)
        fail("invalid or over-budget event name");
    std::string folded(name);
    for (auto& c : folded)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    const auto& events = groups_[event_group].hot->events;
    const auto found = events.find(folded);
    if (found == events.end()) fail("event missing in selected HOT group");
    const auto& event = found->second;
    if (!event.unsupported_reason.empty() || !event.event_type ||
        *event.event_type != 1 || !event.track_id)
        fail("unsupported or unresolved event mapping");
    return {event_group, &event, resolve_track(*event.track_id)};
}

} // namespace odh
