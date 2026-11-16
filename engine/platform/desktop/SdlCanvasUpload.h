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
        auto write = [&](const SDL_Rect& area) {
            const auto first = std::size_t(area.y) * width + area.x;
            const auto rowBytes = area.w * sizeof(Uint32);
            if (SDL_UpdateTexture(texture, &area, pixels.data() + first, width * sizeof(Uint32)) != 0) {
                valid_ = false;
                return false;
            }
            bytes += rowBytes * area.h;
            if (!noCache_) {
                if (area.w == width) {
                    std::memcpy(previous_.data() + first, pixels.data() + first, rowBytes * area.h);
                } else {
                    for (int y = 0; y < area.h; ++y) {
                        const auto row = first + std::size_t(y) * width;
                        std::memcpy(previous_.data() + row, pixels.data() + row, rowBytes);
                    }
                }
            }
            return true;
        };
        if (!valid_) {
            if (!write({0, 0, width, height})) return false;
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
            for (unsigned i = 0; i < count; ++i) {
                SDL_Rect area = spans[i];
                // Small changes such as a rollover or price often occupy only
                // a few columns. Avoid a second scan on scrolling or busy scenes.
                if (changedRows <= 4 * band && area.h < height)
                    area = ChangedBounds(area, pixels);
                if (area.w > 0 && !write(area)) return false;
            }
        }
        paintedCursor = {};
        return true;
    }
private:
    SDL_Rect ChangedBounds(const SDL_Rect& band, const std::vector<Uint32>& pixels) const {
        SDL_Rect bounds{};
        SDL_IntersectRect(&band, &paintedCursor, &bounds);
        for (int y = band.y; y < band.y + band.h; ++y) {
            const auto offset = std::size_t(y) * width_;
            const auto* row = pixels.data() + offset;
            const auto* previous = previous_.data() + offset;
            if (std::memcmp(row, previous, width_ * sizeof(Uint32)) == 0) continue;
            const int left = int(std::mismatch(row, row + width_, previous).first - row);
            int right = width_;
            while (right > left + 1 && row[right - 1] == previous[right - 1]) --right;
            const SDL_Rect changed{left, y, right - left, 1};
            SDL_UnionRect(&bounds, &changed, &bounds);
        }
        return bounds;
    }

    std::vector<Uint32> previous_;
    int width_ = 0, height_ = 0;
    bool valid_ = false, noCache_ = false;
};
}
