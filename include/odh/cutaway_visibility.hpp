// SPDX-License-Identifier: MPL-2.0
// Source: pinned ObjectComponent.Update/CutawayTests and VMEntity.SetWallUse.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace odh {
// Native direction is the GetValue notch0..7. Diagonals use NORTH as source.
std::uint8_t source_adjacent_wall(std::int16_t wall_placement_flags,std::uint8_t direction);
std::optional<std::array<std::int32_t,2>> source_cutaway_tile(float x,float y) noexcept;
struct FullCutawayVisibilityInput {
    bool hide_for_cutaway=false,previous_hidden=false,walls_down=false;
    std::int16_t object_level=1,view_level=1;
    unsigned build_mode=0;
    float x=0,y=0; // Actual ObjectComponent.Position, including -.5/SLOT offset.
    std::uint32_t width=0,height=0;
    // State of the wall at the ROUND-TO-EVEN graphics tile, not logical TileX.
    std::uint8_t wall_segments=0;
    std::optional<std::uint8_t> adjacent_wall,occupied_walls;
};
// Null means missing state can change the result. Unknown wall-use metadata
// is never replaced by zero; full-map invariance can still prove a result.
std::optional<bool> full_cutaway_hidden(const FullCutawayVisibilityInput&) noexcept;
}
