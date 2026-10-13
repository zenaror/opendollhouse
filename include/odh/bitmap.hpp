#pragma once

#include "odh/formats.hpp"

namespace odh {

struct BitmapLimits {
    std::uint32_t width = 2048, height = 2048, pixels = 1048576;
};

struct BitmapImage {
    std::uint32_t width, height;
    std::uint16_t bit_count;
    bool top_down; // Original file orientation; output rows always start at top.
    std::vector<std::array<std::uint8_t, 4>> pixels; // RGBA, opaque alpha.
    std::uint64_t remaining_bytes;
};

// BMP file header + 40-byte BITMAPINFOHEADER, BI_RGB, 8-bit indexed or
// 24-bit BGR only. Reserved file fields must be zero. Palette reserved bytes
// are ignored; no Sims color-key or transparency semantics are inferred.
BitmapImage read_bmp(const View&, BitmapLimits = {});

} // namespace odh
