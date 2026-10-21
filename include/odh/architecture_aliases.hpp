#pragma once

#include "odh/architecture_arrays.hpp"
#include "odh/prepared_architecture.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace odh {

// An explicit ordered catalog. `name` is the provider's dynamic resource key
// (the basename without its final extension); `id` is the caller's assigned
// ODH runtime ID. Later duplicate canonical names override earlier entries.
struct ArchitectureCatalogResource {
    std::string name;
    std::uint16_t id = 0;
};

struct ArchitectureAliasResolverLimits {
    std::size_t catalog_entries = 65536;
    std::size_t map_entries = 4096;
    std::size_t name_bytes = 1024 * 1024;
    std::size_t name_length = 4096;
};

struct ArchitectureAliasResolutionDiagnostics {
    std::size_t floor_catalog_duplicates = 0;
    std::size_t wall_catalog_duplicates = 0;
    std::size_t floor_map_entries = 0;
    std::size_t wall_map_entries = 0;
    std::size_t floor_map_resolved = 0;
    std::size_t wall_map_resolved = 0;
    std::size_t floor_map_misses = 0;
    std::size_t wall_map_misses = 0;
    std::size_t floor_map_overwrites = 0;
    std::size_t wall_map_overwrites = 0;
    std::size_t unsupported_floor_catalog_names = 0;
    std::size_t unsupported_wall_catalog_names = 0;
    std::size_t unsupported_floor_map_names = 0;
    std::size_t unsupported_wall_map_names = 0;
    std::array<bool, 256> unresolved_floor_aliases{};
    std::array<bool, 256> unresolved_wall_aliases{};
    // Final declared IDs with no successful dictionary value. Unlike the
    // historical arrays above, a failed duplicate does not taint an ID when
    // any duplicate successfully resolved, matching BuildFloorDict.
    std::array<bool, 256> declared_unresolved_floor_aliases{};
    std::array<bool, 256> declared_unresolved_wall_aliases{};
};

struct ResolvedArchitectureAliases {
    ArchitectureResourceAliases aliases;
    ArchitectureAliasResolutionDiagnostics diagnostics;
    bool floor_map_complete() const noexcept {
        return diagnostics.floor_map_misses == 0;
    }
    bool wall_map_complete() const noexcept {
        return diagnostics.wall_map_misses == 0;
    }
};

// Resolves the already-selected FLRm and WALm records against explicit,
// ordered catalogs. The caller selects map chunks and assigns catalog IDs;
// this function does neither. Map names follow Simitone's rule: take bytes
// before the first dot, then lowercase invariantly. Catalog keys are the
// provider keys (basename without final extension), lowercased in full.
// Only ASCII names are normalized; non-ASCII names are reported as unsupported
// and left unresolved rather than locale-folded. Inputs are never modified.
// Catalog IDs must be unique after duplicate-name precedence is applied and
// lie in the bounded ODH range 256..65533. Throws std::runtime_error on limits
// or invalid winning catalog IDs.
ResolvedArchitectureAliases resolve_architecture_aliases(
    const ArchitectureMap& floor_map, const ArchitectureMap& wall_map,
    const std::vector<ArchitectureCatalogResource>& floor_catalog,
    const std::vector<ArchitectureCatalogResource>& wall_catalog,
    const ArchitectureAliasResolverLimits& limits = {});

} // namespace odh
