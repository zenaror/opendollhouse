#pragma once
#include "odh/formats.hpp"

namespace odh {
// Structural limits only: no runtime identity, ordering or person-field defaults.
struct NbrsLimits {
    std::uint64_t payload_bytes = 1024 * 1024;
    std::uint64_t decoded_bytes = 512 * 1024; // Cumulative raw name/person/value bytes.
    std::uint32_t entries = 4096;
    std::uint32_t name_bytes = 256;
    std::uint64_t total_name_bytes = 64 * 1024;
    std::uint64_t relationships = 16384;
    std::uint64_t relationship_values = 65536;
};
struct NeighborRelationship {
    std::int32_t key_count;
    std::int32_t key;
    std::vector<std::int32_t> values; // Preserve disk int32; never narrow to short.
};
struct NeighborRecord {
    std::int32_t version;
    std::optional<std::int32_t> unknown3;
    std::string name; // Raw single-byte C string, no encoding conversion.
    std::optional<std::uint8_t> name_padding; // Present for even byte lengths.
    std::int32_t mystery_zero;
    std::int32_t person_mode;
    std::optional<std::vector<std::int16_t>> person_data; // Exactly 80 or 256 words.
    std::int16_t neighbor_id;
    std::uint32_t guid;
    std::int32_t unknown_neg_one;
    std::vector<NeighborRelationship> relationships;
};
struct NeighborEntry {
    std::int32_t marker;
    std::optional<NeighborRecord> record; // Marker 0 is only 4 bytes; no implied ID.
};
struct Nbrs {
    std::uint32_t reserved;
    std::uint32_t version;
    std::vector<NeighborEntry> entries; // Serialized order, including placeholders.
    std::uint64_t remaining_bytes;
};
// SRBN containers 62/63 (native surveyed), 73 (fixed FreeSO writer); records 4/10.
// Unknown layouts, negative counts and duplicate relation keys fail explicitly.
Nbrs read_nbrs(const View&, const NbrsLimits& = {});
} // namespace odh
