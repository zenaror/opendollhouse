#include "odh/hit_object_catalog.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F action, const std::string& reason) {
    bool rejected = false;
    try { action(); }
    catch (const odh::FormatError& error) {
        rejected = std::string(error.what()).find(reason) != std::string::npos;
    }
    require(rejected, "object catalog must reject the specified invalid profile");
}
odh::HitFile hit() {
    odh::HitFile file;
    file.major_version = 1;
    file.minor_version = 8;
    file.bytes.resize(32);
    return file;
}
odh::Hot native_hot(const std::string& text) {
    std::istringstream stream(text);
    odh::Hsm symbols;
    return odh::read_hot(odh::View(stream, text.size()), &symbols, {},
                         odh::HotReadProfile::native_entry_map);
}
const char* old_hot =
    "[TrackData]\n100=16\n"
    "[EventMapping]\nold=1,7\n"
    "[Track]\n7=0,old,1024,2,0,0,20,100,70\n";
const char* newer_hot =
    "[Track]\n7=0,newer,1024,2,0,0,20,300,80\n";
const char* latest_hot =
    "[Track]\n7=0,latest,1024,2,0,0,20,400,90\n";
const char* entries_hot =
    "[TrackData]\n300=16\n400=17\n";
}

int main() { try {
    auto a_hit = hit(), b_hit = hit(), c_hit = hit(), d_hit = hit();
    auto a = native_hot(old_hot), b = native_hot(newer_hot);
    auto c = native_hot(entries_hot), d = native_hot(latest_hot);
    const std::vector<odh::HitEntryGroup> groups{
        {&a_hit, &a}, {&b_hit, &b}, {&c_hit, &c}};
    const odh::HitEntryCatalog entries(groups);
    const odh::NativeHitObjectCatalog objects(groups, entries);
    const auto resolved = objects.resolve_global_track_event(0, "OLD");
    require(resolved.event_group == 0 && resolved.event == &a.events.at("old") &&
            resolved.track.object.id == 7 && resolved.track.object.group == 1 &&
            resolved.track.object.track == &b.tracks.at(7) &&
            resolved.track.entry_key == 300 && resolved.track.entry.file == &c_hit &&
            resolved.track.entry.pc == 16,
            "event A resolves winning Track B and global TrackData in HIT C");
    require(objects.lookup_object(7) &&
            objects.lookup_object(7)->kind == odh::HotObjectKind::track &&
            objects.lookup_track(7)->track == &b.tracks.at(7) &&
            !objects.lookup_patch(7) && !objects.lookup_object(99),
            "typed lookup exposes the winning object without a local-track fallback");
    require(objects.usage().object_records == 2 && objects.usage().unique_keys == 1 &&
            objects.usage().collision_records == 1 &&
            objects.usage().same_kind_replacements == 1 &&
            objects.usage().cross_kind_replacements == 0 &&
            objects.collisions()[0].previous_group == 0 &&
            objects.collisions()[0].replacement_group == 1 &&
            objects.collisions()[0].previous_line != 0 &&
            objects.collisions()[0].replacement_line != 0,
            "raw registration rows and collision provenance are retained");
    require(objects.resolve_track(7).object.track == &b.tracks.at(7),
            "direct TrackID resolution uses global object ownership");
    auto event_only = native_hot("[EventMapping]\nremote=1,7\n");
    const std::vector<odh::HitEntryGroup> event_only_groups{
        {&a_hit, &event_only}, {&b_hit, &b}, {&c_hit, &c}};
    const odh::HitEntryCatalog event_only_entries(event_only_groups);
    const odh::NativeHitObjectCatalog event_only_objects(event_only_groups,
                                                         event_only_entries);
    require(event_only_objects.resolve_global_track_event(0, "remote")
                .track.object.track == &b.tracks.at(7),
            "selected EventMapping needs no Track row in its own HOT");

    auto patch_elsewhere = native_hot(
        "[Patch]\n80=0,external,\"external.xa\",0,0\n");
    const std::vector<odh::HitEntryGroup> patch_elsewhere_groups{
        {&a_hit, &a}, {&b_hit, &b}, {&c_hit, &c}, {&d_hit, &patch_elsewhere}};
    const odh::HitEntryCatalog patch_elsewhere_entries(patch_elsewhere_groups);
    const odh::NativeHitObjectCatalog patch_elsewhere_objects(
        patch_elsewhere_groups, patch_elsewhere_entries);
    require(patch_elsewhere_objects.resolve_track(7).object.track == &b.tracks.at(7) &&
            *b.tracks.at(7).patch_id == 80 &&
            patch_elsewhere_objects.lookup_patch(80)->group == 3 &&
            patch_elsewhere_objects.lookup_patch(80)->patch ==
                &patch_elsewhere.patches.at(80),
            "Track metadata and winning Patch can come from separate HOT groups");

    const std::vector<odh::HitEntryGroup> later_groups{
        {&a_hit, &a}, {&b_hit, &b}, {&d_hit, &d}, {&c_hit, &c}};
    const odh::HitEntryCatalog later_entries(later_groups);
    const odh::NativeHitObjectCatalog later(later_groups, later_entries);
    require(later.resolve_global_track_event(0, "old").track.object.track ==
                &d.tracks.at(7) &&
            later.resolve_track(7).entry.file == &c_hit &&
            later.resolve_track(7).entry.pc == 17,
            "last processed Track registration wins independently of entry resource");
    const std::vector<odh::HitEntryGroup> reverse_groups{
        {&a_hit, &a}, {&d_hit, &d}, {&b_hit, &b}, {&c_hit, &c}};
    const odh::HitEntryCatalog reverse_entries(reverse_groups);
    const odh::NativeHitObjectCatalog reverse(reverse_groups, reverse_entries);
    require(reverse.lookup_track(7)->track == &b.tracks.at(7),
            "caller-supplied group order changes the winning Track");

    auto patch_last = native_hot(
        "[Track]\n7=0,track,1024,2,0,0,20,300,0\n"
        "[Patch]\n7=0,patch,\"patch.xa\",0,0\n");
    const std::vector<odh::HitEntryGroup> patch_groups{
        {&a_hit, &patch_last}, {&c_hit, &c}};
    const odh::HitEntryCatalog patch_entries(patch_groups);
    const odh::NativeHitObjectCatalog patch_objects(patch_groups, patch_entries);
    require(patch_objects.lookup_object(7)->kind == odh::HotObjectKind::patch &&
            !patch_objects.lookup_track(7) &&
            patch_objects.lookup_patch(7)->patch == &patch_last.patches.at(7) &&
            patch_objects.usage().cross_kind_replacements == 1 &&
            patch_objects.collisions()[0].previous_kind == odh::HotObjectKind::track &&
            patch_objects.collisions()[0].replacement_kind == odh::HotObjectKind::patch,
            "later Patch masks an earlier Track in the shared object catalog");
    rejects([&] { patch_objects.resolve_track(7); }, "different object kind");
    auto track_last = native_hot(
        "[Patch]\n7=0,patch,\"patch.xa\",0,0\n"
        "[Track]\n7=0,track,1024,2,0,0,20,300,0\n");
    const std::vector<odh::HitEntryGroup> track_groups{
        {&a_hit, &track_last}, {&c_hit, &c}};
    const odh::HitEntryCatalog track_entries(track_groups);
    const odh::NativeHitObjectCatalog track_objects(track_groups, track_entries);
    require(track_objects.lookup_track(7)->track == &track_last.tracks.at(7) &&
            !track_objects.lookup_patch(7) &&
            track_objects.usage().cross_kind_replacements == 1,
            "later Track masks an earlier Patch in the same HOT");
    auto patch_across = native_hot(
        "[Patch]\n7=0,across,\"across.xa\",0,0\n");
    const std::vector<odh::HitEntryGroup> across_groups{
        {&a_hit, &a}, {&b_hit, &patch_across}, {&c_hit, &c}};
    const odh::HitEntryCatalog across_entries(across_groups);
    const odh::NativeHitObjectCatalog across_objects(across_groups, across_entries);
    require(across_objects.lookup_object(7)->kind == odh::HotObjectKind::patch &&
            across_objects.lookup_patch(7)->group == 1 &&
            !across_objects.lookup_track(7) &&
            across_objects.collisions()[0].previous_group == 0 &&
            across_objects.collisions()[0].replacement_group == 1 &&
            across_objects.usage().cross_kind_replacements == 1,
            "cross-group Patch registration masks a Track with the same numeric key");

    auto duplicate = native_hot(
        "[Track]\n7=0,first,1024,2,0,0,20,100,0\n"
        "7=0,second,1024,2,0,0,20,300,0\n"
        "[Patch]\n7=0,patch,\"patch.xa\",0,0\n");
    const std::vector<odh::HitEntryGroup> duplicate_groups{
        {&a_hit, &duplicate}, {&c_hit, &c}};
    const odh::HitEntryCatalog duplicate_entries(duplicate_groups);
    const odh::NativeHitObjectCatalog duplicate_objects(duplicate_groups,
                                                        duplicate_entries);
    require(duplicate_objects.usage().object_records == 3 &&
            duplicate_objects.usage().same_kind_replacements == 1 &&
            duplicate_objects.usage().cross_kind_replacements == 1 &&
            duplicate_objects.lookup_patch(7)->patch == &duplicate.patches.at(7) &&
            duplicate_objects.collisions()[0].replacement_line <
                duplicate_objects.collisions()[1].replacement_line,
            "same-file duplicate rows remain charged and ordered before cross-kind winner");

    auto no_entry = native_hot(
        "[Track]\n7=0,new,1024,2,0,0,20,999,0\n");
    auto entp_only = hit();
    entp_only.entry_points[999] = 16;
    const std::vector<odh::HitEntryGroup> missing_entry_groups{
        {&a_hit, &a}, {&b_hit, &no_entry}, {&entp_only, &c}};
    const odh::HitEntryCatalog missing_entries(missing_entry_groups);
    const odh::NativeHitObjectCatalog missing_objects(missing_entry_groups,
                                                       missing_entries);
    rejects([&] { missing_objects.resolve_global_track_event(0, "old"); },
            "entry key absent from global TrackData");
    rejects([&] { objects.resolve_track(99); }, "TrackID absent");
    rejects([&] { objects.resolve_global_track_event(1, "old"); },
            "event missing in selected HOT group");
    rejects([&] { objects.resolve_global_track_event(0, "old", true); },
            "loop request");

    auto low_record_limit = odh::HitObjectCatalogLimits{};
    low_record_limit.object_records = 1;
    rejects([&] { odh::NativeHitObjectCatalog invalid(groups, entries,
                                                       low_record_limit); },
            "object record budget");
    auto low_collision_limit = odh::HitObjectCatalogLimits{};
    low_collision_limit.collision_records = 1;
    rejects([&] { odh::NativeHitObjectCatalog invalid(duplicate_groups,
                    duplicate_entries, low_collision_limit); }, "collision record budget");
    auto low_group_limit = odh::HitObjectCatalogLimits{};
    low_group_limit.groups = 2;
    rejects([&] { odh::NativeHitObjectCatalog invalid(groups, entries,
                                                       low_group_limit); },
            "invalid group or object budget");
    auto low_name_limit = odh::HitObjectCatalogLimits{};
    low_name_limit.event_name_bytes = 2;
    const odh::NativeHitObjectCatalog short_names(groups, entries, low_name_limit);
    rejects([&] { short_names.resolve_global_track_event(0, "old"); },
            "over-budget event name");

    auto no_profile = a;
    no_profile.native_object_order.reset();
    const std::vector<odh::HitEntryGroup> no_profile_groups{{&a_hit, &no_profile}};
    const odh::HitEntryCatalog no_profile_entries(no_profile_groups);
    rejects([&] { odh::NativeHitObjectCatalog invalid(no_profile_groups,
                    no_profile_entries); }, "registration order is absent");
    auto zero_id = a;
    zero_id.native_object_order->push_back({odh::HotObjectKind::track, 0, 5});
    const std::vector<odh::HitEntryGroup> zero_groups{{&a_hit, &zero_id}};
    const odh::HitEntryCatalog zero_entries(zero_groups);
    rejects([&] { odh::NativeHitObjectCatalog invalid(zero_groups, zero_entries); },
            "zero object ID");
    auto bad_kind = a;
    bad_kind.native_object_order->push_back(
        {static_cast<odh::HotObjectKind>(99), 7, 5});
    const std::vector<odh::HitEntryGroup> kind_groups{{&a_hit, &bad_kind}};
    const odh::HitEntryCatalog kind_entries(kind_groups);
    rejects([&] { odh::NativeHitObjectCatalog invalid(kind_groups, kind_entries); },
            "invalid native object kind");
    auto missing_metadata = a;
    missing_metadata.native_object_order->push_back({odh::HotObjectKind::patch, 7, 5});
    const std::vector<odh::HitEntryGroup> metadata_groups{{&a_hit, &missing_metadata}};
    const odh::HitEntryCatalog metadata_entries(metadata_groups);
    rejects([&] { odh::NativeHitObjectCatalog invalid(metadata_groups,
                    metadata_entries); }, "Patch registration has no parsed metadata");
    auto unresolved_track = native_hot(
        "[Track]\n7=0,unknown,1024,2,0,0,20,UnknownEntry,0\n");
    const std::vector<odh::HitEntryGroup> unresolved_groups{
        {&a_hit, &unresolved_track}};
    const odh::HitEntryCatalog unresolved_entries(unresolved_groups);
    const odh::NativeHitObjectCatalog unresolved_objects(unresolved_groups,
                                                         unresolved_entries);
    require(unresolved_objects.lookup_track(7)->track ==
                &unresolved_track.tracks.at(7),
            "unresolved Track metadata may still occupy the global object key");
    rejects([&] { unresolved_objects.resolve_track(7); },
            "Track metadata or entry key is unresolved");

    std::cout << checks << " HIT object catalog checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
}
