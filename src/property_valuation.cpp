#include "odh/property_valuation.hpp"
#include <limits>

namespace odh {
namespace {
bool fits_word(std::int64_t value) noexcept {
    return value>=std::numeric_limits<std::int32_t>::min() && value<=std::numeric_limits<std::int32_t>::max();
}
}
PropertyValuation inspect_property_valuation(const PropertyValuationInput& input) {
    using R=PropertyValuationReason;
    PropertyValuation result;
    if(input.profile!=PropertyValuationProfile::comparison_pc24)return result;
    // Both components use the same supplied raw architecture's dimension and
    // caller-pinned native state, even when one component later rejects it.
    result.objects=inspect_property_object_assets(input.world,input.definitions,
        input.complete_partition,input.complete_inventory,input.architecture.width);
    result.geometry=inspect_property_architecture_value(input.architecture,input.prices);
    if(result.objects.status!=PropertyObjectAssetsStatus::resolved) {
        result.reason=R::object_assets_unresolved;return result;
    }
    if(result.geometry.status!=PropertyArchitectureValueStatus::resolved) {
        result.reason=R::architecture_unresolved;return result;
    }
    //0041607b combines geometry with the architecture-object bucket BEFORE
    //0042730f multiplies it. Lot base and primary objects are added afterward.
    const auto combined=std::int64_t(result.geometry.total)+*result.objects.architecture_objects;
    if(!fits_word(combined)){result.reason=R::combined_architecture_overflow;return result;}
    const auto scale=comparison_pc24_truncate_scale(static_cast<std::int32_t>(combined),input.multiplier_bits);
    result.scale_reason=scale.reason;
    if(!scale.value){result.reason=R::scale_unresolved;return result;}
    const auto total=std::int64_t(input.lot_base)+*result.objects.primary+*scale.value;
    if(!fits_word(total)){result.reason=R::property_value_overflow;return result;}
    // Publish the numeric financial result only after every component/operation
    // resolved. No partial combined/scaled totals escape a rejected candidate.
    result.combined_architecture=static_cast<std::int32_t>(combined);
    result.scaled_architecture=*scale.value;
    result.property_value=static_cast<std::int32_t>(total);
    result.status=PropertyValuationStatus::resolved;result.reason=R::none;return result;
}
const char* property_valuation_status_name(PropertyValuationStatus value) noexcept {
    switch(value){case PropertyValuationStatus::resolved:return "resolved";case PropertyValuationStatus::unresolved:return "unresolved";}return "invalid";
}
const char* property_valuation_reason_name(PropertyValuationReason value) noexcept {
    switch(value){
    case PropertyValuationReason::none:return "none";
    case PropertyValuationReason::unsupported_profile:return "unsupported_profile";
    case PropertyValuationReason::object_assets_unresolved:return "object_assets_unresolved";
    case PropertyValuationReason::architecture_unresolved:return "architecture_unresolved";
    case PropertyValuationReason::combined_architecture_overflow:return "combined_architecture_overflow";
    case PropertyValuationReason::scale_unresolved:return "scale_unresolved";
    case PropertyValuationReason::property_value_overflow:return "property_value_overflow";
    }return "invalid";
}
} // namespace odh
