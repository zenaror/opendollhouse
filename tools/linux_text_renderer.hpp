#pragma once

#include "odh/raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <optional>
#include <vector>

namespace odh_host {

struct TextOverlayStyle {
    std::int32_t x=4,y=204,width=312,height=30;
    std::int32_t font_size_px=15,padding=5;
    std::array<std::uint8_t,4> foreground{255,255,255,255};
    std::array<std::uint8_t,4> background{25,25,30,220};
    bool wrap_lines=false; // Preserve legacy single-line labels by default.
    std::size_t first_line=0; // Presentation only, clamped to the last full page.
    bool centered=false; // Button labels; false retains the language's default alignment.
    bool right_aligned=false; // Explicit UI alignment, independent of text direction.
    // Optional exact pixel viewport into the full shaped wrapped document.
    // Leaves first_line paging unchanged when absent. No UTF-8 is sliced.
    std::optional<int> pixel_scroll_y;
};

struct TextOverlayResult {
    std::size_t glyphs=0,missing_glyphs=0,pixels_changed=0;
    std::size_t line_count=1,first_line=0,visible_lines=1,max_first_line=0;
    bool has_more=false;
};

struct TextOverlayExtent { int width=0,height=0; };
struct TextOverlayLineRange {int top=0,bottom=0;};
// Optional prepared line ranges from the SAME registered shaper. Empty means
// unsupported (or empty text); consumers must retain natural document layout.
std::vector<TextOverlayLineRange> text_overlay_line_ranges(std::string_view,std::uint8_t,int,int);
// Uses the same font and word wrapping as painting, without allocating a frame.
TextOverlayExtent measure_text_overlay(std::string_view utf8,std::uint8_t language,
                                      int font_size_px,int wrap_width=0);

// A platform installs both callbacks together; measurement and paint must use
// the same shaping/font policy. Registration is scoped to the calling UI thread.
struct TextOverlayBackend {
    TextOverlayExtent (*measure)(std::string_view,std::uint8_t,int,int)=nullptr;
    TextOverlayResult (*draw)(odh::RasterFrame&,std::string_view,std::uint8_t,TextOverlayStyle)=nullptr;
    std::vector<TextOverlayLineRange> (*lines)(std::string_view,std::uint8_t,int,int)=nullptr;
};
TextOverlayBackend set_text_overlay_backend(TextOverlayBackend);
bool text_overlay_backend_available() noexcept;

// Linux host edge: shape UTF-8 with Pango, use system font fallback, and blend
// the bounded result into the caller's already-rasterized scene. The portable
// game core remains independent of Pango and system fonts.
TextOverlayResult draw_text_overlay(odh::RasterFrame&,std::string_view utf8,
                                    std::uint8_t language,TextOverlayStyle={});

} // namespace odh_host
