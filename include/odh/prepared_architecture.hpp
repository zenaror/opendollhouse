#pragma once

#include "odh/saved_architecture.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {
// Already-resolved FLRm/WALm byte aliases. Resource discovery, name matching
// and provider precedence belong to the caller. IDs 256..65533 form this
// bounded ODH profile; 65534/65535 remain water/pool sentinels.
struct ArchitectureResourceAliases {
    std::array<std::optional<std::uint16_t>, 256> floors{}, walls{};
    // IDs explicitly declared by FLRm for which no entry survived the
    // reference's successful-lookup-only dictionary construction.
    std::array<bool, 256> declared_unresolved_floors{};
};
struct ArchitecturePreparationOptions {
    bool flip_road = false;
    std::uint32_t target_size = 64;
};
struct ArchitecturePreparationLimits {
    std::size_t retained_bytes = 256 * 1024;
};
struct ArchitecturePreparationDiagnostics {
    // Counts cover the complete 64x64 source, including cells cropped away.
    // A lookup miss retains the current value (including any road swap),
    // matching Simitone RemapFloors/RemapWalls. Strict misses include
    // undeclared/global fallback IDs; unresolved references count only used
    // identities declared in FLRm with no final successful value. A base-floor
    // lookup is unused on a diagonal with explicit floor fields and beneath a
    // ground pool/water overlay.
    std::size_t floor_lookups = 0, floor_alias_misses = 0;
    std::size_t unresolved_floor_alias_references = 0;
    std::size_t wall_lookups = 0, wall_alias_misses = 0;
    std::array<bool, 256> missing_floor_aliases{}, missing_wall_aliases{};
};
struct PreparedArchitectureLevel {
    std::vector<std::uint16_t> floors;
    std::vector<SavedWallTile> walls;
};
struct PreparedArchitecture {
    std::uint32_t width = 0, height = 0;
    std::array<PreparedArchitectureLevel, 2> levels;
    // ResizeFlags: 1 exactly when (ground ARRY8 flag & 32) == 0.
    std::vector<std::uint8_t> buildable;
    ArchitecturePreparationDiagnostics diagnostics;
    bool floor_aliases_resolved() const noexcept {
        return diagnostics.floor_alias_misses == 0;
    }
    // Undeclared aliases retain their source byte under RemapFloors. Only a
    // used FLRm declaration with no resolved value makes identity ambiguous.
    bool floor_identity_unambiguous() const noexcept {
        return diagnostics.unresolved_floor_alias_references == 0;
    }
    bool wall_aliases_resolved() const noexcept {
        return diagnostics.wall_alias_misses == 0;
    }
};

// Bounded VMTS1ActivatorNew.ConvertArchitecture preparation profile. Requires
// 64x64 floors/walls on both stories, ground ARRY8 flags and pool/water grids
// of exact source extent. Upper-story flags are not consulted by the reference.
// Applies DecodeWalls normalization; RemapFloors using the original floor and
// ground flags on both stories; RemapWalls and diagonal fallback; ground-only
// pool then water overlays; top-left crop to target_size (1..64).
// Missing alias lookups retain the current value, including a prior road swap,
// and are reported. Inputs are never
// modified. Throws std::runtime_error for malformed/unsupported input or budget.
// This prepares geometry/material IDs only, not terrain, objects or live rooms.
PreparedArchitecture prepare_saved_architecture(
    const SavedArchitecture&, const ArchitectureResourceAliases&,
    const ArchitecturePreparationOptions& options = {},
    const ArchitecturePreparationLimits& limits = {});
} // namespace odh
