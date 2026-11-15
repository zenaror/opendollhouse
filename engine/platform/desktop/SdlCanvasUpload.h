/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace odh::desktop {

// SDL retains the texture between presentations. Compare bands of the finished
// canvas so every overlay, transition and loading screen follows the same rules.
class SdlCanvasUpload {
public:
    SDL_Rect paintedCursor{};
    std::uint64_t bytes = 0;
    void Invalidate() { valid_ = false; }

    bool Upload(SDL_Texture* texture, const std::vector<Uint32>& pixels, int width, int height) {
        if (width <= 0 || height <= 0 || pixels.size() != std::size_t(width) * height)
            return false;
        if (width != width_ || height != height_) {
            width_ = width;
            height_ = height;
            valid_ = false;
            if (!noCache_) {
                try { previous_.resize(pixels.size()); }
                catch (const std::bad_alloc&) { noCache_ = true; }
            }
        }
        auto write = [&](int y, int rows) {
            const SDL_Rect area{0, y, width, rows};
            const auto first = std::size_t(y) * width;
            const auto size = std::size_t(rows) * width * sizeof(Uint32);
            if (SDL_UpdateTexture(texture, &area, pixels.data() + first, width * sizeof(Uint32)) != 0) {
                valid_ = false;
                return false;
            }
            bytes += size;
            if (!noCache_) std::memcpy(previous_.data() + first, pixels.data() + first, size);
            return true;
        };
        if (!valid_) {
            if (!write(0, height)) return false;
            valid_ = !noCache_;
        } else {
            // A scroll usually changes most bands. Switch to one full upload as
            // soon as enough damage accumulates, with a bounded number of calls.
            SDL_Rect spans[8]{};
            unsigned count = 0;
            int changedRows = 0;
            constexpr int band = 16;
            for (int y = 0; y < height; y += band) {
                const int rows = std::min(band, height - y);
                const bool hasCursor = paintedCursor.h > 0 && y < paintedCursor.y + paintedCursor.h
                                       && paintedCursor.y < y + rows;
                const auto first = std::size_t(y) * width;
                if (!hasCursor && std::memcmp(previous_.data() + first, pixels.data() + first,
                                              std::size_t(rows) * width * sizeof(Uint32)) == 0)
                    continue;
                changedRows += rows;
                if (changedRows * 2 >= height ||
                    (count == 8 && y != spans[count - 1].y + spans[count - 1].h)) {
                    spans[0] = {0, 0, width, height};
                    count = 1;
                    break;
                }
                if (count && spans[count - 1].y + spans[count - 1].h == y)
                    spans[count - 1].h += rows;
                else
                    spans[count++] = {0, y, width, rows};
            }
            for (unsigned i = 0; i < count; ++i)
                if (!write(spans[i].y, spans[i].h)) return false;
        }
        paintedCursor = {};
        return true;
    }
private:
    std::vector<Uint32> previous_;
    int width_ = 0, height_ = 0;
    bool valid_ = false, noCache_ = false;
};
}
