#include "odh/saved_floor_placement.hpp"
#include "odh/prepared_architecture.hpp"

namespace odh {

SavedFloorPlacementCheck check_saved_floor_placement(
    const SavedWorldEntity& entity, const PreparedArchitecture& architecture,
    std::int32_t x, std::int32_t y, std::int16_t level) {
    const auto floor = prepared_precise_floor(architecture, x, y, level);
    if (!floor || !architecture.floor_identity_unambiguous() ||
        !entity.definition.level_offset) return {};

    const auto flags = static_cast<std::uint16_t>(entity.saved.prefix.object_data[42]);
    const auto result = floor_change_valid(*floor, static_cast<std::int8_t>(level), flags,
        !entity.tail.person.has_value(), *entity.definition.level_offset);
    return {result == FloorPlacementResult::allowed ? SavedFloorPlacementState::allowed
                                                     : SavedFloorPlacementState::rejected,
            *floor, flags, *entity.definition.level_offset, result};
}

} // namespace odh
