#pragma once

#include "odh/raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace odh_host {

struct TextOverlayStyle {
    std::int32_t x=4,y=204,width=312,height=30;
    std::int32_t font_size_px=15,padding=5;
    std::array<std::uint8_t,4> foreground{255,255,255,255};
    std::array<std::uint8_t,4> background{25,25,30,220};
};

struct TextOverlayResult {
    std::size_t glyphs=0,missing_glyphs=0,pixels_changed=0;
};

// Linux host edge: shape UTF-8 with Pango, use system font fallback, and blend
// the bounded result into the caller's already-rasterized scene. The portable
// game core remains independent of Pango and system fonts.
TextOverlayResult draw_text_overlay(odh::RasterFrame&,std::string_view utf8,
                                    std::uint8_t language,TextOverlayStyle={});

} // namespace odh_host
