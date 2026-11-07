#pragma once
#include "odh/saved_architecture.hpp"
#include <map>

namespace odh {
struct PropertyArchitecturePrices {
    // Caller-certified SAME resource-ID domain as the supplied architecture.
    // Raw house aliases and prepared/catalog IDs require different providers.
    // A complete provider map certifies that absent floor/wallpaper IDs cost0.
    std::map<std::uint16_t,std::int32_t> structural, floors, wallpaper;
    bool floor_lookup_complete=false, wallpaper_lookup_complete=false;
    std::optional<std::int32_t> pool_tile_price;
};
struct PropertyArchitectureValueLimits { std::size_t price_entries=65536; };
enum class PropertyArchitectureValueStatus { resolved, unresolved };
enum class PropertyArchitectureValueReason {
    none, invalid_extent, missing_flags, missing_pools, price_budget,
    unknown_segments, missing_neighbor_style, missing_structural_price,
    incomplete_floor_lookup, incomplete_wallpaper_lookup, missing_pool_price,
    arithmetic_overflow
};
struct PropertyArchitectureValue {
    PropertyArchitectureValueStatus status=PropertyArchitectureValueStatus::unresolved;
    PropertyArchitectureValueReason reason=PropertyArchitectureValueReason::invalid_extent;
    // Structural sum BEFORE its one final signed /2. Surfaces includes floors,
    // wallpaper and pool tiles; total=structural/2+surfaces_and_pools.
    std::int32_t structural=0, surfaces_and_pools=0, total=0;
    bool has_architecture=false;
};
// Static comparison-sample0047b178 math only. Caller establishes snapshot and
// price-resource provenance. Requires raw square1..64 grids and both own-story
// flags. No mutation, resource discovery, refund, lot base or object valuation.
PropertyArchitectureValue inspect_property_architecture_value(const SavedArchitecture&,
    const PropertyArchitecturePrices&,const PropertyArchitectureValueLimits& = {});
const char* property_architecture_value_status_name(PropertyArchitectureValueStatus) noexcept;
const char* property_architecture_value_reason_name(PropertyArchitectureValueReason) noexcept;
} // namespace odh
