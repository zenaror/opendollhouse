#include "linux_text_renderer.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        odh::RasterFrame frame;
        frame.width = 128;
        frame.height = 32;
        frame.pixels.assign(128 * 32, {10, 20, 30, 255});
        frame.depth.assign(frame.pixels.size(), 0.0F);
        odh_host::TextOverlayStyle style;
        style.x = 2;
        style.y = 2;
        style.width = 116;
        style.height = 24;
        style.font_size_px = 14;
        style.padding = 2;
        style.foreground = {250, 240, 230, 255};
        style.background = {30, 40, 50, 220};

        const auto basic = odh_host::draw_text_overlay(frame, "Move In 5", 1, style);
        require(basic.glyphs == 7, "ASCII letters and digits are rendered");
        require(basic.missing_glyphs == 0 && basic.pixels_changed > 100,
                "overlay draws readable letters and a background panel");
        require(std::find(frame.pixels.begin(), frame.pixels.end(),
                          std::array<std::uint8_t, 4>{250, 240, 230, 255}) != frame.pixels.end(),
                "glyph pixels use the requested foreground color");
        require(frame.pixels[0] == std::array<std::uint8_t, 4>{10, 20, 30, 255},
                "pixels outside the overlay remain unchanged");
        require(frame.depth[0] == 0.0F, "text does not change scene depth");

        const auto localized = odh_host::draw_text_overlay(frame, "Família", 14, style);
        require(localized.missing_glyphs == 0,
                "common accented Latin letters use readable base glyphs");
        const auto other_script = odh_host::draw_text_overlay(frame, "日本語", 15, style);
        require(other_script.missing_glyphs == 3 && other_script.glyphs == 3,
                "unsupported scripts are visibly marked instead of disappearing");

        bool invalid_utf8 = false;
        try {
            (void)odh_host::draw_text_overlay(frame, std::string_view("\xc3(", 2), 1, style);
        } catch (const std::invalid_argument&) {
            invalid_utf8 = true;
        }
        require(invalid_utf8, "malformed UTF-8 is rejected");

        std::cout << "built-in bitmap text fallback passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
