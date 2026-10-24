#include "linux_text_renderer.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace odh_host {
namespace {
using Glyph = std::array<std::uint8_t, 7>;
constexpr std::array<Glyph, 26> letters{{
    {{0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}}, {{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}},
    {{0x0f,0x10,0x10,0x10,0x10,0x10,0x0f}}, {{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}},
    {{0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}}, {{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}},
    {{0x0f,0x10,0x10,0x17,0x11,0x11,0x0f}}, {{0x11,0x11,0x11,0x1f,0x11,0x11,0x11}},
    {{0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}}, {{0x07,0x02,0x02,0x02,0x12,0x12,0x0c}},
    {{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {{0x10,0x10,0x10,0x10,0x10,0x10,0x1f}},
    {{0x11,0x1b,0x15,0x15,0x11,0x11,0x11}}, {{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {{0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}}, {{0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}},
    {{0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}}, {{0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}},
    {{0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}}, {{0x1f,0x04,0x04,0x04,0x04,0x04,0x04}},
    {{0x11,0x11,0x11,0x11,0x11,0x11,0x0e}}, {{0x11,0x11,0x11,0x11,0x0a,0x0a,0x04}},
    {{0x11,0x11,0x11,0x15,0x15,0x15,0x0a}}, {{0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}},
    {{0x11,0x11,0x0a,0x04,0x04,0x04,0x04}}, {{0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}}
}};
constexpr std::array<Glyph, 10> digits{{
    {{0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}}, {{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}},
    {{0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}}, {{0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}},
    {{0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}}, {{0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e}},
    {{0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e}}, {{0x1f,0x01,0x02,0x04,0x08,0x08,0x08}},
    {{0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}}, {{0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e}}
}};

Glyph glyph(char value) {
    if (value >= 'a' && value <= 'z') value = static_cast<char>(value - 'a' + 'A');
    if (value >= 'A' && value <= 'Z') return letters[static_cast<std::size_t>(value - 'A')];
    if (value >= '0' && value <= '9') return digits[static_cast<std::size_t>(value - '0')];
    switch (value) {
    case '.': return {{0,0,0,0,0,0x0c,0x0c}};
    case ',': return {{0,0,0,0,0x0c,0x0c,0x08}};
    case ':': return {{0,0x0c,0x0c,0,0x0c,0x0c,0}};
    case ';': return {{0,0x0c,0x0c,0,0x0c,0x0c,0x08}};
    case '-': return {{0,0,0,0x1f,0,0,0}};
    case '_': return {{0,0,0,0,0,0,0x1f}};
    case '+': return {{0,0x04,0x04,0x1f,0x04,0x04,0}};
    case '=': return {{0,0,0x1f,0,0x1f,0,0}};
    case '/': return {{0x01,0x02,0x02,0x04,0x08,0x08,0x10}};
    case '\\': return {{0x10,0x08,0x08,0x04,0x02,0x02,0x01}};
    case '(': return {{0x02,0x04,0x08,0x08,0x08,0x04,0x02}};
    case ')': return {{0x08,0x04,0x02,0x02,0x02,0x04,0x08}};
    case '[': return {{0x0e,0x08,0x08,0x08,0x08,0x08,0x0e}};
    case ']': return {{0x0e,0x02,0x02,0x02,0x02,0x02,0x0e}};
    case '<': return {{0x02,0x04,0x08,0x10,0x08,0x04,0x02}};
    case '>': return {{0x10,0x08,0x04,0x02,0x04,0x08,0x10}};
    case '?': return {{0x0e,0x11,0x01,0x02,0x04,0,0x04}};
    case '!': return {{0x04,0x04,0x04,0x04,0x04,0,0x04}};
    case '\'': return {{0x04,0x04,0x02,0,0,0,0}};
    case '"': return {{0x0a,0x0a,0x0a,0,0,0,0}};
    case '|': return {{0x04,0x04,0x04,0x04,0x04,0x04,0x04}};
    default: return {{0x1f,0x11,0x15,0x11,0x15,0x11,0x1f}};
    }
}

bool decode_utf8(std::string_view text, std::vector<char>& output,
                 std::size_t& missing) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::uint32_t value = 0;
        std::size_t count = 0;
        if (first < 0x80) { value = first; count = 1; }
        else if ((first & 0xe0) == 0xc0) { value = first & 0x1f; count = 2; }
        else if ((first & 0xf0) == 0xe0) { value = first & 0x0f; count = 3; }
        else if ((first & 0xf8) == 0xf0) { value = first & 0x07; count = 4; }
        else return false;
        if (count > text.size() - i) return false;
        for (std::size_t n = 1; n < count; ++n) {
            const auto next = static_cast<unsigned char>(text[i + n]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
            (count == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff)) return false;
        char ascii = '?';
        if (value < 0x80) ascii = static_cast<char>(value);
        else if ((value >= 0xc0 && value <= 0xc5) || (value >= 0xe0 && value <= 0xe5)) ascii = 'A';
        else if (value == 0xc7 || value == 0xe7) ascii = 'C';
        else if ((value >= 0xc8 && value <= 0xcb) || (value >= 0xe8 && value <= 0xeb)) ascii = 'E';
        else if ((value >= 0xcc && value <= 0xcf) || (value >= 0xec && value <= 0xef)) ascii = 'I';
        else if (value == 0xd1 || value == 0xf1) ascii = 'N';
        else if ((value >= 0xd2 && value <= 0xd6) || (value >= 0xf2 && value <= 0xf6) || value == 0xd8 || value == 0xf8) ascii = 'O';
        else if ((value >= 0xd9 && value <= 0xdc) || (value >= 0xf9 && value <= 0xfc)) ascii = 'U';
        else if (value == 0xdd || value == 0xfd || value == 0xff) ascii = 'Y';
        else if (value == 0xdf) { output.push_back('S'); ascii = 'S'; }
        if (ascii == '?') ++missing;
        output.push_back(ascii);
        i += count;
    }
    return true;
}

std::uint8_t blend(std::uint8_t before, std::uint8_t after, unsigned alpha) {
    return static_cast<std::uint8_t>((static_cast<unsigned>(after) * alpha +
        static_cast<unsigned>(before) * (255U - alpha) + 127U) / 255U);
}
}

TextOverlayResult draw_text_overlay(odh::RasterFrame& frame, std::string_view utf8,
                                    std::uint8_t language, TextOverlayStyle style) {
    if (language < 1 || language > 20 || utf8.empty() || utf8.size() > 4096 ||
        frame.width == 0 || frame.height == 0 ||
        frame.pixels.size() != static_cast<std::size_t>(frame.width) * frame.height ||
        frame.width > 8192 || frame.height > 8192 || style.x < 0 || style.y < 0 ||
        style.x > 8192 || style.y > 8192 || style.width <= 0 || style.width > 8192 ||
        style.height <= 0 || style.height > 8192 || style.font_size_px < 1 ||
        style.font_size_px > 256 || style.padding < 0 || style.padding > 4096 ||
        style.padding >= style.width / 2 || style.padding >= style.height / 2)
        throw std::invalid_argument("bitmap text overlay input outside supported bounds");

    TextOverlayResult result;
    std::vector<char> characters;
    characters.reserve(utf8.size());
    if (!decode_utf8(utf8, characters, result.missing_glyphs))
        throw std::invalid_argument("bitmap text overlay input is not valid UTF-8");

    const int scale = std::max(1, std::min(style.font_size_px / 7,
        (style.height - style.padding * 2) / 7));
    const int advance = 6 * scale;
    const int capacity = (style.width - style.padding * 2) / advance;
    if (capacity <= 0) throw std::invalid_argument("bitmap text overlay has no room for a glyph");
    if (characters.size() > static_cast<std::size_t>(capacity)) {
        characters.resize(static_cast<std::size_t>(capacity));
        if (capacity >= 3) {
            characters[characters.size() - 3] = '.';
            characters[characters.size() - 2] = '.';
            characters[characters.size() - 1] = '.';
        }
    }

    const int x0 = style.x + style.padding;
    const int y0 = style.y + (style.height - 7 * scale) / 2;
    const int x_end = std::min<int>(static_cast<int>(frame.width), style.x + style.width);
    const int y_end = std::min<int>(static_cast<int>(frame.height), style.y + style.height);
    const int x_begin = std::max(0, style.x);
    const int y_begin = std::max(0, style.y);
    for (int y = y_begin; y < y_end; ++y) {
        for (int x = x_begin; x < x_end; ++x) {
            auto& pixel = frame.pixels[static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)];
            const bool in_background = x >= style.x && x < style.x + style.width &&
                y >= style.y && y < style.y + style.height;
            if (!in_background) continue;
            const auto before = pixel;
            const auto alpha = style.background[3];
            pixel[0] = blend(pixel[0], style.background[0], alpha);
            pixel[1] = blend(pixel[1], style.background[1], alpha);
            pixel[2] = blend(pixel[2], style.background[2], alpha);
            pixel[3] = 255;
            if (pixel != before) ++result.pixels_changed;
        }
    }

    for (std::size_t index = 0; index < characters.size(); ++index) {
        const auto pattern = glyph(characters[index]);
        if (characters[index] != ' ') ++result.glyphs;
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((pattern[static_cast<std::size_t>(row)] &
                     (0x10U >> static_cast<unsigned>(column))) == 0) continue;
                for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
                    const int x = x0 + static_cast<int>(index) * advance + static_cast<int>(column) * scale + dx;
                    const int y = y0 + row * scale + dy;
                    if (x < x_begin || x >= x_end || y < y_begin || y >= y_end) continue;
                    auto& pixel = frame.pixels[static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)];
                    const auto before = pixel;
                    pixel[0] = blend(pixel[0], style.foreground[0], style.foreground[3]);
                    pixel[1] = blend(pixel[1], style.foreground[1], style.foreground[3]);
                    pixel[2] = blend(pixel[2], style.foreground[2], style.foreground[3]);
                    pixel[3] = 255;
                    if (pixel != before) ++result.pixels_changed;
                }
            }
        }
    }
    return result;
}
} // namespace odh_host
