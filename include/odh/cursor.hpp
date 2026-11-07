// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"

namespace odh {
struct CursorImage {
    std::uint16_t width=0,height=0,hotspot_x=0,hotspot_y=0;
    // Top-down owned RGBA. Ordinary transparent pixels have alpha0; opaque
    // black/white pixels alpha255. Inversion cannot be represented by RGBA:
    // invert[i]==1 means complement the underlying RGB, not transparent black.
    std::vector<std::array<std::uint8_t,4>> pixels;
    std::vector<std::uint8_t> invert;
};
struct WindowsCursor {std::vector<CursorImage> images;};
struct CursorLimits {
    std::uint64_t bytes=1024*1024;
    std::uint32_t images=16,width=256,height=256,pixels=65536;
};
// Original TS1 corpus profile: CUR type2 directory, BITMAPINFOHEADER40,
// bottom-up uncompressed1bpp DIB, canonical black/white palette and DWORD row
// strides. Decode XOR then AND planes with all four logical combinations.
// All directory images are validated, including nonselected resolutions.
// PNG/color/compressed/other DIB variants fail explicitly; no icon guessing.
WindowsCursor read_windows_cur(const View&,CursorLimits={});
}
