// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ui_bitmap.hpp"
#include <string_view>

namespace odh_host {
// Observed original tooltip: black 1px outline, RGB(255,255,230) interior,
// one line, positioned above the pointer with a 14px gap. General UI uses
// names; catalog uses its price, red when unaffordable. Font10 and text
// padding are provisional; the exact original font has not been authenticated.
// Empty, multiline/description, over-budget or non-fitting names return none.
// Invalid UTF8/language/scale/frame bounds throw before painting.
std::optional<odh::UiBitmapRect> original_tooltip_layout(std::uint32_t width,std::uint32_t height,
    std::string_view name,std::uint8_t language,std::array<std::int32_t,2> pointer,float scale=1);
// Reposition a previously measured box in the same viewport.
odh::UiBitmapRect position_original_tooltip(std::uint32_t width,std::uint32_t height,int box_width,int box_height,
    std::array<std::int32_t,2> pointer,float scale=1);
// Only RGB pixels change: depth, owners and raster counters remain untouched.
std::optional<odh::UiBitmapRect> draw_original_tooltip(odh::RasterFrame&,std::string_view name,
    std::uint8_t language,std::array<std::int32_t,2> pointer,float scale=1,
    std::array<std::uint8_t,4> foreground={0,0,0,255});
}
