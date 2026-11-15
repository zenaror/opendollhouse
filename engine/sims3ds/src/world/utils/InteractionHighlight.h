/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace FSO { namespace LotView { namespace Utils {
inline uint32_t InteractionHighlightPixel(uint32_t color,float sceneDepth,uint32_t mask,float maskDepth) {
    if(!std::isfinite(maskDepth) || maskDepth>sceneDepth+.001f)return color;
    // DVD fireplace captures: immediate additive light on the visible object,
    // approximately24 per opaque channel in its RGB565 output; alpha edges
    // receive only their authored coverage. No outline or invented replacement art.
    const uint32_t amount=(24u*(mask>>24)+127u)/255u;
    const auto channel=[&](unsigned shift){return std::min<uint32_t>(255u,((color>>shift)&255u)+amount);};
    return channel(0)|(channel(8)<<8)|(channel(16)<<16)|(color&0xff000000u);
}
}}}
