#pragma once
#include "odh/formats.hpp"

namespace odh {
struct ObjtLimits {
    std::uint64_t payload_bytes = 1024U * 1024U;
    std::uint32_t entries = 4096; // Includes every serialized GUID-zero entry.
    std::uint64_t name_bytes = 256U * 1024U; // Cumulative, excluding NUL/padding.
    std::uint32_t name_length = 4096;
};
struct ObjtMetadata {
    std::uint16_t unknown1a, init_tree_version, unknown2a, main_tree_version;
    std::uint16_t type_id, objd_type;
};
struct ObjtEntry {
    std::uint32_t guid;
    // GUID-zero entries serialize ONLY the GUID. They remain in entries and
    // have no metadata/name/padding/version extension; they are not type ID0.
    std::optional<ObjtMetadata> metadata;
    std::string name; // Raw byte string, without NUL; no encoding conversion.
    std::optional<std::uint8_t> name_padding; // Present for even name length.
    std::optional<std::uint32_t> version_extra; // Raw bits, signed version >2.
};
struct Objt {
    std::uint32_t reserved, version;
    std::array<std::uint8_t,4> magic;
    std::vector<ObjtEntry> entries; // Serialized index, including GUID0 entries.
    std::uint64_t remaining_bytes = 0;
    // Reference resolution is entries[type-1], NOT a search by metadata.type_id.
    // Type0/out-of-range returns null. A GUID0 entry can be returned; callers
    // must not fabricate a resource definition for that sentinel.
    const ObjtEntry* entry_for_type(std::uint16_t type) const;
};
// Uncounted LE OBJT structural profile. Header/magic are retained rather than
// whitelisted. Version is interpreted as signed32 ONLY for the >2 branch.
// GUID0 consumes four bytes and continues; it is not an end marker. Therefore
// there is no distinguishable unknown tail: successful traversal reaches EOF
// (remaining_bytes=0), while an incomplete trailing entry is a FormatError.
// All names/entries own their storage independently of View/stream lifetime.
Objt read_objt(const View&, const ObjtLimits& = {});
}
