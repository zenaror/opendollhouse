#pragma once

#include "odh/raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace odh {

// Draw an opaque game BMP onto a UI frame while treating the selected RGB
// color as transparent. This does not change depth or picking-owner buffers.
std::size_t blit_color_keyed_bitmap(RasterFrame& destination,
    const BitmapImage& source, std::int32_t x, std::int32_t y,
    std::array<std::uint8_t, 3> color_key);

} // namespace odh
