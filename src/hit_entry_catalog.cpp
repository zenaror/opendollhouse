#include "odh/hit_entry_catalog.hpp"

#include <limits>
#include <string>

namespace odh {
namespace {
[[noreturn]] void fail(const char* why) {
    throw FormatError(std::string("HIT entry catalog: ") + why);
}
void charge(std::uint64_t& total, std::uint64_t amount, std::uint64_t limit) {
    if (amount > limit - total) fail("aggregate budget exceeded");
    total += amount;
}
std::uint32_t code_end(const HitFile& hit) {
    if (hit.major_version != 1 || (hit.minor_version != 0 && hit.minor_version != 8) ||
        hit.bytes.size() > std::numeric_limits<std::uint32_t>::max())
        fail("unsupported HIT version or size");
    const auto end = hit.entry_table_offset.value_or(hit.bytes.size());
    if (end > hit.bytes.size() || end <= 16) fail("invalid HIT code region");
    return static_cast<std::uint32_t>(end);
}
}

HitEntryCatalog::HitEntryCatalog(const std::vector<HitEntryGroup>& input,
                                 HitEntryCatalogLimits limits) : limits_(limits) {
    if (!limits.groups || !limits.track_data_rows || !limits.serialized_bytes ||
        !limits.event_name_bytes || limits.event_name_bytes > 65536 ||
        input.empty() || input.size() > limits.groups)
        fail("invalid group or catalog budget");

    // Validate the full input and aggregate budgets before allocating an index
    // proportional to the number of TrackData rows.
    usage_.groups = input.size();
    for (const auto& group : input) {
        if (!group.hit || !group.hot) fail("null borrowed resource");
        const auto end = code_end(*group.hit);
        charge(usage_.serialized_bytes, group.hit->bytes.size(), limits.serialized_bytes);
        charge(usage_.serialized_bytes, group.hot->logical_bytes, limits.serialized_bytes);
        charge(usage_.track_data_rows, group.hot->track_data.size(), limits.track_data_rows);
        charge(usage_.track_data_rows, group.hot->track_data_replacements,
               limits.track_data_rows);
        for (const auto& row : group.hot->track_data)
            if (row.second < 16 || row.second >= end)
                fail("TrackData PC outside paired HIT code region");
    }

    groups_ = input;
    for (std::size_t group = 0; group < groups_.size(); ++group) {
        for (const auto& row : groups_[group].hot->track_data) {
            const auto found = entries_.find(row.first);
            if (found == entries_.end()) {
                entries_.emplace(row.first, Entry{group, row.second});
            } else {
                if (collisions_.size() >= limits.collision_records)
                    fail("collision record budget exceeded");
                collisions_.push_back({row.first, found->second.group, group,
                                       found->second.pc, row.second});
                found->second = {group, row.second};
            }
        }
    }
    usage_.unique_keys = entries_.size();
    usage_.collision_records = collisions_.size();
}

std::optional<HitCodeTarget> HitEntryCatalog::lookup(std::uint32_t key) const noexcept {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return std::nullopt;
    return HitCodeTarget{groups_[found->second.group].hit, found->second.pc};
}

ResolvedNativeHitEvent HitEntryCatalog::resolve_event(std::size_t event_group,
                                                       std::string_view name,
                                                       bool loop) const {
    if (event_group >= groups_.size()) fail("event group outside catalog");
    if (loop) fail("loop request outside supported native event profile");
    if (name.empty() || name.size() > limits_.event_name_bytes ||
        name.find('\0') != std::string_view::npos)
        fail("invalid or over-budget event name");
    std::string folded(name);
    for (auto& c : folded)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    const auto& hot = *groups_[event_group].hot;
    const auto found = hot.events.find(folded);
    if (found == hot.events.end()) fail("event missing in selected HOT group");
    const auto& event = found->second;
    if (!event.unsupported_reason.empty() || !event.event_type || *event.event_type != 1 ||
        !event.track_id)
        fail("unsupported or unresolved event mapping");
    const auto track = hot.tracks.find(*event.track_id);
    if (track == hot.tracks.end()) fail("track missing in selected HOT group");
    // subroutine_offset is the older local selection profile. Native entry
    // resolution uses CSV7/HitlistID as a key in the global TrackData map.
    if (!track->second.hitlist_id) fail("unresolved native entry key");
    const auto target = lookup(*track->second.hitlist_id);
    if (!target) fail("native entry key absent from TrackData catalog");
    return {event_group, *event.track_id, *track->second.hitlist_id,
            &event, &track->second, *target};
}

} // namespace odh
