#pragma once

#include "odh/formats.hpp"
#include <array>
#include <map>

namespace odh {
struct HitResourceLimits {
    std::uint64_t input_bytes = 4 * 1024 * 1024;
    std::uint64_t text_bytes = 2 * 1024 * 1024;
    std::uint32_t line_bytes = 16384;
    std::uint32_t records = 32768;
    std::uint32_t hitlist_ids = 262144; // Aggregate expanded IDs, including overwritten rows.
    std::uint64_t hit_bytes = 1024 * 1024;
    std::uint32_t entry_points = 8192;
};
struct Hsm {
    std::map<std::string, std::int32_t> constants;
    std::map<std::string, std::uint64_t> first_lines;
    std::uint64_t logical_bytes = 0, records = 0, duplicate_names = 0;
};
enum class HsmReadProfile { simitone_local, native_namespace };
// logical_bytes is the complete serialized input extent, including BOM, line
// endings and duplicates; it is not the C++ allocation footprint.
// The default Simitone-local profile folds ASCII and keeps the first duplicate.
// Native namespace preserves name bytes and replaces exact duplicate values.
// Both profiles count duplicate rows and keep the first source line.
Hsm read_hsm(const View&, const HitResourceLimits& = {},
             HsmReadProfile = HsmReadProfile::simitone_local);

struct HotEvent {
    std::optional<std::uint32_t> event_type, track_id;
    std::string unsupported_reason;
    // EventMapping CSV fields 2..4. Omitted trailing words are known zero;
    // explicitly empty, unresolved or out-of-range words remain unknown.
    std::array<std::optional<std::int32_t>,3> argument_overrides{{0,0,0}};
};
struct HotTrack {
    std::optional<std::uint32_t> subroutine_offset, hitlist_id, patch_id;
    std::optional<std::uint32_t> volume, arg_type, ducking_priority, control_group;
    std::optional<std::uint32_t> sound_pressure_level;
    std::string subroutine_label, unsupported_reason;
};
struct HotPatch {
    std::string name, filename; // Unquoted filename bytes; original slashes preserved.
    bool looped = false, piano = false;
    // Native Patch scan discards bytes between the second comma and the first
    // quote. Retain them for provenance when that prefix contains extra data.
    std::string ignored_pre_filename{};
};
struct HotHitlist {
    std::vector<std::uint32_t> ids;
    std::string raw, unsupported_reason;
};
struct Hot {
    std::map<std::uint32_t, std::uint32_t> track_data;
    std::map<std::string, HotEvent> events;
    std::map<std::uint32_t, HotTrack> tracks;
    std::map<std::uint32_t, HotPatch> patches;
    std::map<std::uint32_t, HotHitlist> hitlists;
    std::map<std::string, std::int32_t> equates;
    std::optional<std::int32_t> version, load_priority;
    std::uint64_t logical_bytes = 0, records = 0, duplicate_records = 0;
    std::uint64_t track_data_replacements = 0;
    std::uint64_t unsupported_records = 0, unknown_records = 0;
};
enum class HotReadProfile { simitone_local, native_entry_map };
// HOT dictionary rows overwrite within this file (counted), as in FreeSO.
// The default Simitone-local profile rejects duplicate TrackData and infers a
// local subroutine_offset, marking a missing local offset unsupported. The
// native-entry-map profile retains input-order last-wins TrackData rows and
// leaves entry resolution to a caller's explicit cross-resource catalog;
// track_data_replacements counts only duplicate TrackData rows in that profile.
// Neither profile chooses cross-file precedence or emulates native parse faults.
// Decimal fields use HOT equates. Simitone-local @labels use ASCII-lower HSM
// lookup (and retain numeric fallback); native @labels use exact HSM names,
// including digit-only names, without numeric fallback. Unresolved
// symbols remain optional and carry a reason, never an invented numeric zero.
// Under the local profile, entry offset follows TrackData hitlist then patch
// precedence. The explicit subroutine label is retained, not used as a
// source-level bytecode fallback.
// logical_bytes counts serialized input extent, not C++ allocation footprint.
Hot read_hot(const View&, const Hsm* = nullptr, const HitResourceLimits& = {},
             HotReadProfile = HotReadProfile::simitone_local);

struct HitFile {
    std::uint32_t major_version = 0, minor_version = 0;
    std::vector<std::uint8_t> bytes;
    std::optional<std::uint64_t> entry_table_offset; // First byte of ENTP marker.
    std::map<std::uint32_t, std::uint32_t> entry_points;
};
// Observed HIT!/TRAX major 1 minor 0 or 8 profile. Retains bounded bytes for a
// separate interpreter. First ENTP marker is scanned; a present table must end
// with EENT and have unique IDs and addresses within [16, table marker).
// The input stream belongs to the caller; no source-level fallback is used.
HitFile read_hit(const View&, const HitResourceLimits& = {});
}
