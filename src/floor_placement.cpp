#include "odh/floor_placement.hpp"

namespace odh {

FloorPlacementResult floor_change_valid(std::uint16_t floor, std::int8_t level,
                                        std::uint16_t flags, bool game_object,
                                        std::uint16_t level_offset) noexcept {
    using namespace placement_flag;
    if (floor == 65535) {
        return (flags & (allow_on_pool | require_pool)) == 0
                   ? FloorPlacementResult::cant_place_on_water
                   : FloorPlacementResult::allowed;
    }
    if ((flags & require_pool) != 0) return FloorPlacementResult::must_place_on_pool;
    if (floor == 65534) {
        return (flags & (on_water | require_water)) == 0
                   ? FloorPlacementResult::cant_place_on_water
                   : FloorPlacementResult::allowed;
    }
    if ((flags & require_water) != 0) return FloorPlacementResult::must_place_on_water;
    if (floor == 0) {
        if (level == 1) {
            return (flags & on_terrain) == 0 ? FloorPlacementResult::not_allowed_on_terrain
                                             : FloorPlacementResult::allowed;
        }
        return (flags & in_air) == 0 && level_offset == 0
                   ? FloorPlacementResult::cant_place_in_air
                   : FloorPlacementResult::allowed;
    }
    if (game_object && (flags & on_floor) == 0 &&
        (level_offset == 0 || (flags & in_air) == 0)) {
        return FloorPlacementResult::not_allowed_on_floor;
    }
    return FloorPlacementResult::allowed;
}

} // namespace odh
