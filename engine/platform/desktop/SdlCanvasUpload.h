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

    bool Upload(SDL_Texture* texture, const std::vector<Uint32>& pixels, int width, int height,
                bool keepPaintedCursor = false) {
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
        // When the cursor is stationary, its composited pixels can stay in the
        // texture until a canvas update overwrites any part of its footprint.
        SDL_Rect retainedCursor{};
        if (keepPaintedCursor) {
            retainedCursor = paintedCursor;
            paintedCursor = {};
        }
        auto write = [&](const SDL_Rect& area) {
            const auto first = std::size_t(area.y) * width + area.x;
            const auto rowBytes = area.w * sizeof(Uint32);
            if (SDL_UpdateTexture(texture, &area, pixels.data() + first, width * sizeof(Uint32)) != 0) {
                valid_ = false;
                return false;
            }
            if (SDL_HasIntersection(&area, &retainedCursor)) retainedCursor = {};
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
        // For a pan or a transition, uploading once is cheaper than looking
        // for narrow strips. Nine distributed changes are enough to prefer it;
        // a false positive only sends some extra pixels.
        auto broadChange = [&]() {
            for (int yi = 1; yi <= 3; ++yi) {
                const std::size_t row = (std::size_t(height) * yi / 4) * width;
                for (int xi = 1; xi <= 3; ++xi) {
                    const auto at = row + std::size_t(width) * xi / 4;
                    if (pixels[at] == previous_[at]) return false;
                }
            }
            return true;
        };
        if (!valid_ || broadChange()) {
            if (!write({0, 0, width, height})) return false;
            valid_ = !noCache_;
        } else {
            SDL_Rect spans[8]{};
            unsigned count = 0;
            std::size_t transferPixels = 0;
            const auto size = [](const SDL_Rect& r) { return std::size_t(r.w) * r.h; };
            constexpr int band = 16;
            for (int y = 0; y < height; y += band) {
                const int rows = std::min(band, height - y);
                const bool hasCursor = paintedCursor.h > 0 && y < paintedCursor.y + paintedCursor.h
                                       && paintedCursor.y < y + rows;
                const auto first = std::size_t(y) * width;
                if (!hasCursor && std::memcmp(previous_.data() + first, pixels.data() + first,
                                              std::size_t(rows) * width * sizeof(Uint32)) == 0)
                    continue;
                const SDL_Rect changed = ChangedBounds({0, y, width, rows}, pixels);
                if (changed.w == 0) continue;

                // A moving Sim may touch many bands while covering only a few
                // columns. Batch nearby pieces by their transfer area.
                SDL_Rect joined{};
                if (count) SDL_UnionRect(&spans[count - 1], &changed, &joined);
                if (count && (count == 8 || size(joined) <= size(spans[count - 1]) + size(changed) + 64)) {
                    transferPixels -= size(spans[count - 1]);
                    spans[count - 1] = joined;
                    transferPixels += size(joined);
                } else {
                    spans[count++] = changed;
                    transferPixels += size(changed);
                }
                if (transferPixels >= pixels.size() / 2) {
                    spans[0] = {0, 0, width, height};
                    count = 1;
                    break;
                }
            }
            for (unsigned i = 0; i < count; ++i)
                if (!write(spans[i])) return false;
        }
        paintedCursor = retainedCursor;
        return true;
    }
private:
    SDL_Rect ChangedBounds(const SDL_Rect& band, const std::vector<Uint32>& pixels) const {
        SDL_Rect bounds{};
        if (!SDL_IntersectRect(&band, &paintedCursor, &bounds)) bounds = {};
        for (int y = band.y; y < band.y + band.h; ++y) {
            const auto offset = std::size_t(y) * width_;
            const auto* row = pixels.data() + offset;
            const auto* previous = previous_.data() + offset;
            if (std::memcmp(row, previous, width_ * sizeof(Uint32)) == 0) continue;
            // Positions already inside the bounds cannot expand them. Avoid
            // comparing the long unchanged margins pixel by pixel on every row.
            const int oldLeft = bounds.w ? bounds.x : width_;
            const int oldRight = bounds.w ? bounds.x + bounds.w : 0;
            int left = oldLeft;
            if (std::memcmp(row, previous, oldLeft * sizeof(Uint32)) != 0)
                left = int(std::mismatch(row, row + oldLeft, previous).first - row);
            int right = oldRight;
            if (std::memcmp(row + oldRight, previous + oldRight, (width_ - oldRight) * sizeof(Uint32)) != 0) {
                right = width_;
                while (row[right - 1] == previous[right - 1]) --right;
            }
            const SDL_Rect changed{left, y, right - left, 1};
            SDL_UnionRect(&bounds, &changed, &bounds);
            if (bounds.w == width_) {
                // The remaining rows fit even if the whole band has scrolled.
                bounds.h = band.y + band.h - bounds.y;
                break;
            }
        }
        return bounds;
    }

    std::vector<Uint32> previous_;
    int width_ = 0, height_ = 0;
    bool valid_ = false, noCache_ = false;
};
}
