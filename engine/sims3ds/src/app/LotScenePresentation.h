// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <cstdint>

namespace simapp {
// The caller supplies width*height source pixels and four times as much output.
// Source and destination may start at the same address (overlays already drawn).
inline void ExpandLotScene2x(const uint32_t* source, uint32_t* destination, int width, int height) {
    for (int y = height - 1; y >= 0; --y) {
        const auto* input = source + size_t(y) * width;
        auto* first = destination + size_t(2 * y) * (2 * width);
        auto* second = first + 2 * width;
        for (int x = width - 1; x >= 0; --x) {
            const auto pixel = input[x];
            first[2 * x] = first[2 * x + 1] = pixel;
            second[2 * x] = second[2 * x + 1] = pixel;
        }
    }
}
}
