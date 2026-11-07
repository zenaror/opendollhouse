#pragma once

#include "odh/raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace odh {

struct UiBitmapRect {
    std::int32_t x=0,y=0,width=0,height=0;
    bool contains(std::int32_t px,std::int32_t py) const noexcept {
        return width>0&&height>0&&px>=x&&py>=y&&std::int64_t(px)<std::int64_t(x)+width&&
            std::int64_t(py)<std::int64_t(y)+height;
    }
};

// Scale the selected crop into the destination with nearest-neighbor sampling.
//32-bit UI artwork composites its alpha; BMP profiles keep colour-key semantics.
// Clipping affects the output loop only, so partly hidden controls keep their phase.
std::size_t blit_color_keyed_bitmap(RasterFrame&,const BitmapImage&,UiBitmapRect destination,
    std::optional<UiBitmapRect> source_crop,std::array<std::uint8_t,3> color_key);

// Straight RGBA image with no colour key. In particular, opaque black and
// magenta are visible colours. Does not alter depth or picking tokens.
std::size_t blit_rgba_bitmap(RasterFrame&,const BitmapImage&,UiBitmapRect destination,
    std::optional<UiBitmapRect> source_crop=std::nullopt);

// Draw an opaque game BMP onto a UI frame while treating the selected RGB
// color as transparent. This does not change depth or picking-owner buffers.
std::size_t blit_color_keyed_bitmap(RasterFrame& destination,
    const BitmapImage& source, std::int32_t x, std::int32_t y,
    std::array<std::uint8_t, 3> color_key);

} // namespace odh
