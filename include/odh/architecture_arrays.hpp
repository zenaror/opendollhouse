#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace odh {
struct ArchitectureArrayLimits {
    std::size_t encoded_bytes = 1024 * 1024;
    std::size_t decoded_bytes = 1024 * 1024;
    std::uint32_t dimension = 1024;
    std::uint32_t stride = 16;
};
struct ArchitectureArray {
    std::int32_t zero = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t stride = 0;
    std::int32_t unknown = 0;
    // Encoded payload includes headers, opcodes and padding unchanged. Data is
    // byte RLE expansion in serialized order, with terminal fill applied.
    std::vector<std::uint8_t> encoded;
    std::vector<std::uint8_t> data;
    std::size_t consumed_bytes = 0;
    std::vector<std::uint8_t> trailing_bytes; // Uninterpreted bytes after exact output extent.
    std::size_t raw_runs = 0;
    std::size_t fill_runs = 0;
    std::size_t zero_opcodes = 0;
};
ArchitectureArray read_architecture_array(const std::vector<std::uint8_t>& bytes,
                                         const ArchitectureArrayLimits& limits = {});
// Explicit column-major transpose: dest=(x*height+y)*stride. Square grids match
// FreeSO; no resize, road flip, semantic normalization or resource remapping.
std::vector<std::uint8_t> transpose_architecture_array(const ArchitectureArray& array);
struct ArchitectureMapLimits {
    std::size_t encoded_bytes = 1024 * 1024;
    std::size_t entries = 4096;
    std::size_t string_bytes = 256 * 1024;
    std::size_t string_length = 4096;
    std::size_t entry_metadata_bytes = 1024;
};
struct ArchitectureMapEntry {
    std::string name; // Original byte sequence, no Unicode conversion.
    std::vector<std::uint8_t> name_padding;
    std::int32_t unknown = 0;
    std::uint8_t id = 0;
    std::vector<std::uint8_t> unknown2;
};
struct ArchitectureMap {
    std::int32_t zero = 0;
    std::int32_t version = 0;
    std::int32_t signature = 0;
    std::uint16_t chunk_id = 0;
    std::vector<ArchitectureMapEntry> entries;
    std::vector<std::uint8_t> encoded;
};
// FLRm and WALm have identical payload structure. No dictionary overwrite or
// precedence is applied: duplicate names/IDs remain separate ordered records.
ArchitectureMap read_architecture_map(const std::vector<std::uint8_t>& bytes,
                                     std::uint16_t chunk_id,
                                     const ArchitectureMapLimits& limits = {});
}
