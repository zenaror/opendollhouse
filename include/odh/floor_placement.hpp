#pragma once

#include <cstdint>

namespace odh {

enum class FloorPlacementResult {
    allowed,
    cant_place_on_water,
    must_place_on_pool,
    must_place_on_water,
    not_allowed_on_terrain,
    cant_place_in_air,
    not_allowed_on_floor,
};

namespace placement_flag {
constexpr std::uint16_t on_floor = 1u << 0;
constexpr std::uint16_t on_terrain = 1u << 1;
constexpr std::uint16_t on_water = 1u << 2;
constexpr std::uint16_t in_air = 1u << 9;
constexpr std::uint16_t allow_on_pool = 1u << 11;
constexpr std::uint16_t require_pool = 1u << 12;
constexpr std::uint16_t require_water = 1u << 13;
} // namespace placement_flag

// Mirrors Simitone VMEntity.FloorChangeValid only. This does not check bounds,
// wall changes, slopes, occupancy, rooms, or routing clearance.
FloorPlacementResult floor_change_valid(std::uint16_t floor, std::int8_t level,
                                        std::uint16_t flags, bool game_object,
                                        std::uint16_t level_offset) noexcept;

} // namespace odh
