#include "odh/object_construction.hpp"

namespace odh {

ObjectConstructionProfile object_construction_profile(
    const ObjectDefinition& definition, const SlotTable& table,
    const ObjectConstructionLimits& limits) {
    if (definition.is_multitile)
        return {ObjectConstructionStatus::unsupported_multitile, {}};
    if (!definition.slot_table_id || !definition.num_attributes)
        return {ObjectConstructionStatus::missing_runtime_fields, {}};

    std::size_t slots = 0;
    for (const auto& record : table.records) {
        if (record.type != 0) continue;
        if (slots == limits.containment_slots)
            return {ObjectConstructionStatus::resource_limit_exceeded, {}};
        ++slots;
    }
    if (*definition.num_attributes > limits.attributes)
        return {ObjectConstructionStatus::resource_limit_exceeded, {}};

    return {ObjectConstructionStatus::ready,
            {static_cast<std::size_t>(*definition.num_attributes), slots}};
}

} // namespace odh
