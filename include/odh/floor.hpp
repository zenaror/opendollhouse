#pragma once

#include "odh/bitmap.hpp"
#include "odh/projection.hpp"
#include "odh/raster.hpp"
#include "odh/sprite.hpp"

namespace odh {

// FreeSO SPR2.FloorCopy profile: pad an odd width on the right with transparent
// black, then replace each pixel with its half-width/half-height wrapped peer
// when that peer's alpha is >=254. The resulting texture must be fully opaque;
// partial alpha and transparency are explicitly outside this CPU floor profile.
// Checks source extent and destination budgets before allocating. Source pixels,
// indices, Z and metadata remain untouched; no partially converted image escapes.
BitmapImage floor_texture(const Spr2Frame&, BitmapLimits = {});

enum class FloorHalf {
    whole,
    horizontal_first,  // TR, BR, BL; TopLeftPattern in the reference wall record.
    horizontal_second, // TL, TR, BL; TopLeftStyle.
    vertical_first,    // TL, TR, BR; TopLeftPattern.
    vertical_second   // BR, BL, TL; TopLeftStyle.
};

// One tile's projected geometry using the same shared depth as objects/avatars.
// TS1 levels 1..2 have Z=(level-1)*2.95 tile units. Planar coordinates are finite
// within [-32768,32767]. Texture extent uses the default BitmapLimits and must
// be fully opaque. The four world-space corners use the FreeSO terrain diamond
// UVs; odd camera rotations swap U/V. Alignment rounds uv*texture_size plus the
// projected flattened (Z=0) corner to nearest, ties even, and reverses the screen
// offset. This bounded CPU arithmetic is not bitwise GPU/original-game proof.
// Altitudes are explicit TL/TR/BR/BL tile-unit offsets added to the story height;
// they do not affect UV alignment's flattened Z=0 projection. No lighting,
// altitude decoding, rooms, water/pool policy or mutations occur.
std::vector<RasterTriangle> floor_triangles(
    const WorldProjection&, float x, float y, std::uint8_t level,
    const BitmapImage&, std::size_t texture_index,
    FloorHalf = FloorHalf::whole, bool align_uv = true,
    std::array<float, 4> altitude = {});

} // namespace odh
