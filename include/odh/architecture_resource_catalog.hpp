#pragma once

#include "odh/architecture_aliases.hpp"
#include "odh/formats.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace odh {

enum class ArchitectureResourceFamily { floor, wall };
enum class ArchitectureResourceSourceKind { loose_file, far1_archive };

// Sources are supplied in explicit precedence order. Later sources override
// earlier ones when their resource names canonicalize to the same key.
struct ArchitectureResourceSource {
    std::string path;
    ArchitectureResourceSourceKind kind = ArchitectureResourceSourceKind::loose_file;
};

// A reopenable locator. FAR offsets refer to the archive's pre-manifest data;
// loose-file offsets are zero. No commercial resource payload is retained.
struct ArchitectureResourceSourceHandle {
    std::size_t source_index = 0;
    std::size_t entry_index = 0;
    std::string entry_name;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct ArchitectureResourceRecord {
    ArchitectureCatalogResource catalog;
    ArchitectureResourceFamily family = ArchitectureResourceFamily::floor;
    ArchitectureResourceSourceHandle source;
};

struct ArchitectureResourceCatalogLimits {
    std::size_t sources = 256;
    std::size_t resource_entries = 65536;
    std::size_t path_bytes = 1024 * 1024;
    std::size_t name_bytes = 4 * 1024 * 1024;
    std::size_t path_length = 4096;
    std::size_t name_length = 4096;
    std::uint32_t far_entries_per_archive = 65536;
    std::uint64_t far_name_bytes_per_archive = 1024 * 1024;
};

struct ArchitectureResourceCatalogDiagnostics {
    std::size_t loose_sources = 0;
    std::size_t far_sources = 0;
    std::size_t ignored_extensions = 0;
    std::size_t duplicate_floor_names = 0;
    std::size_t duplicate_wall_names = 0;
    std::size_t unsupported_names = 0;
};

struct ArchitectureResourceCatalog {
    // Winners are emitted in stable family/name order, with IDs assigned from
    // 256 independently for floors and walls. IDs are ODH policy, not TS1 IDs.
    std::vector<ArchitectureResourceRecord> resources;
    // Paths and source kinds are stored once; a handle's source_index is an
    // index into this vector, avoiding a path copy per FAR entry.
    std::vector<ArchitectureResourceSource> sources;
    ArchitectureResourceCatalogDiagnostics diagnostics;

    std::vector<ArchitectureCatalogResource> floors() const;
    std::vector<ArchitectureCatalogResource> walls() const;
};

// Index explicit loose files and FAR1 archives. Directory iteration is not
// performed. A malformed source or any configured budget violation throws
// FormatError; unrelated extensions are ignored and counted. Only source
// metadata and FAR index names are read, never resource payload bytes.
ArchitectureResourceCatalog discover_architecture_resources(
    const std::vector<ArchitectureResourceSource>&,
    const ArchitectureResourceCatalogLimits& limits = {});

} // namespace odh
