// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/object_group.hpp"
#include "odh/property_object_assets.hpp"
#include "odh/property_architecture_value.hpp"
#include "odh/property_valuation.hpp"
#include "odh/lot.hpp"
#include "odh/named_float_constants.hpp"
#include "odh/architecture_resource_catalog.hpp"
#include <filesystem>
namespace odh_host {
// Read from the authenticated selected Global FCNS4 and unique original SIMI.
// Cached SIMI object/architecture totals are deliberately never consumed.
// PC24 is an explicit comparison profile, not an inferred host rounding mode.
struct HousePropertyPolicy {
    std::int32_t lot_base=0;
    std::uint32_t multiplier_bits=0;
    odh::PropertyValuationProfile profile{};
    bool multiplier_defaulted=false;
};
HousePropertyPolicy read_house_property_policy(const odh::View& selected_global,
    const odh::Simi& unique_original_simi,odh::PropertyValuationProfile,
    odh::NamedFloatLimits={});
struct HousePropertyInput {
    const odh::World& world;
    const odh::SavedWorld& original_world;
    const odh::DefinitionIndex& definitions;
    const std::vector<odh::ObjectGroup>& dynamic_groups;
    const std::vector<odh::WorldEntityReference>& standalone_creations;
    const odh::SavedArchitecture& retained_terrain;
    const odh::SavedArchitecture& current_geometry;
    const odh::PreparedArchitecture& original_underlay;
    const odh::PropertyArchitecturePrices& prices;
    const HousePropertyPolicy& policy;
};
// Pure same-state composition of the existing identity-aware inventory,
// retained-terrain geometry and valuation kernel. Does not authorize eviction,
// update family state, delete objects, move money or publish any checkpoint.
odh::PropertyValuation inspect_house_property(const HousePropertyInput&);
// Current generation/resource-authenticated groups own live topology first.
// Original groups are fallback only for unshadowed complete surviving groups;
// fully current-owned survivors may have legitimately split/joined/retired.
// Partial current coverage, stale groups and reused identities fail closed.
std::vector<odh::SavedMultitileGroup> house_asset_partition(const odh::World&,
    const odh::SavedWorld&,const odh::DefinitionIndex&,const std::vector<odh::ObjectGroup>&,
    const std::vector<odh::WorldEntityReference>& authenticated_standalone_creations={});
// Rendering deliberately omits aux grids to avoid applying overlays twice.
// Financial/UI consumers retain exact current terrain separately. Underlay is
// the original prepared material domain with pool/water overlays suppressed.
odh::SavedArchitecture house_retained_terrain(const odh::SavedArchitecture& retained_terrain,
    const odh::SavedArchitecture& current_geometry);
odh::SavedArchitecture house_metric_architecture(const odh::SavedArchitecture& retained_terrain,
    const odh::SavedArchitecture& current_geometry,const odh::PreparedArchitecture& original_underlay);
// Same prepared ID domain as the current runtime architecture. Stock structural
// prices follow the original valuation table, not the editor's dictionary quirk.
odh::PropertyArchitecturePrices house_prepared_prices(const std::filesystem::path& build,
    const std::filesystem::path& floors,const std::filesystem::path& walls,
    const odh::ArchitectureResourceCatalog&);
}
