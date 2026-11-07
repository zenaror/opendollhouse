// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/architecture_resource_catalog.hpp"

namespace odh {
// Native mutable grids. Original IFF arrays remain immutable terrain/provenance.
// Resource names bind dynamic IDs to provider identities across process reloads.
struct ArchitectureMaterialBinding {
    ArchitectureResourceFamily family=ArchitectureResourceFamily::floor;
    std::uint16_t id=0;
    std::string name;
};
struct ArchitectureSnapshot {
    PreparedArchitecture prepared;
    std::vector<ArchitectureMaterialBinding> materials; // Sorted family/id.
};
struct ArchitectureSnapshotLimits {
    std::size_t bytes=1024*1024,materials=16384,name_bytes=512*1024;
    std::uint32_t dimension=64;
};
void validate_architecture_snapshot(const ArchitectureSnapshot&,const ArchitectureSnapshotLimits& = {});
std::vector<std::uint8_t> encode_architecture_snapshot(const ArchitectureSnapshot&,const ArchitectureSnapshotLimits& = {});
ArchitectureSnapshot decode_architecture_snapshot(const std::vector<std::uint8_t>&,const ArchitectureSnapshotLimits& = {});
ArchitectureSnapshot bind_architecture_snapshot(const PreparedArchitecture&,const ArchitectureResourceCatalog&,
    const ArchitectureSnapshotLimits& = {});
// Resolves stored names against the actual current catalog; never keeps a stale
// process-assigned ID when its resource is absent or ambiguous.
PreparedArchitecture resolve_architecture_snapshot(const ArchitectureSnapshot&,const ArchitectureResourceCatalog&,
    const ArchitectureSnapshotLimits& = {});
}
