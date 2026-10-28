#pragma once
#include "odh/property_architecture_value.hpp"
#include "odh/property_object_assets.hpp"
#include "odh/property_value_scale.hpp"

namespace odh {
// Zero is intentionally invalid: an omitted aggregate field cannot implicitly
// select the intended comparison application's precision profile.
enum class PropertyValuationProfile { comparison_pc24=1 };
struct PropertyValuationInput {
    const World& world;
    const DefinitionIndex& definitions;
    const std::vector<SavedMultitileGroup>& complete_partition;
    bool complete_inventory;
    const SavedArchitecture& architecture;
    const PropertyArchitecturePrices& prices;
    std::int32_t lot_base;
    std::uint32_t multiplier_bits;
    PropertyValuationProfile profile;
};
enum class PropertyValuationStatus { resolved, unresolved };
enum class PropertyValuationReason {
    none, unsupported_profile, object_assets_unresolved, architecture_unresolved,
    combined_architecture_overflow, scale_unresolved, property_value_overflow
};
struct PropertyValuation {
    PropertyValuationStatus status=PropertyValuationStatus::unresolved;
    PropertyValuationReason reason=PropertyValuationReason::unsupported_profile;
    PropertyObjectAssets objects;
    PropertyArchitectureValue geometry;
    // Absent when scaling was not attempted. Retains its reason without
    // exposing a partial scaled amount. All financial fields require resolved.
    std::optional<PropertyValueScaleReason> scale_reason;
    std::optional<std::int32_t> combined_architecture, scaled_architecture, property_value;
};
// Recomputes components from the supplied native state; accepts no preaggregated
// SIMI/object/architecture values. Caller must pin one authoritative snapshot,
// complete runtime group topology and matching raw-resource price identities.
// const references cannot certify concurrency, cross-house identity or provenance.
// comparison_pc24 explicitly selects the intended source precision context; its
// dominance over every original call path remains unproved. This does not alter
// Simi::purchase_value, add family cash, refund, sell, delete, execute or save.
PropertyValuation inspect_property_valuation(const PropertyValuationInput&);
const char* property_valuation_status_name(PropertyValuationStatus) noexcept;
const char* property_valuation_reason_name(PropertyValuationReason) noexcept;
} // namespace odh
