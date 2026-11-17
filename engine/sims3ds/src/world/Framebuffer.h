/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims: the software render target that replaces MonoGame's back buffer / RenderTarget2D pairs.
// Color is RGBA8888 packed little-endian (R | G<<8 | B<<16 | A<<24, i.e. bytes R,G,B,A in memory),
// Depth is the per-pixel world depth written by the z-buffered sprite techniques of 2DWorldBatch.fx
// (smaller = closer to the camera, cleared to +inf), see utils/WorldCamera.h for the depth function.
#pragma once
#include <cstdint>
#include <vector>
#include <limits>
#include <algorithm>
#include <cstring>
#include <new>
#include "core/math.h"

namespace FSO { namespace LotView {

class Framebuffer {
public:
    int32_t Width = 0;
    int32_t Height = 0;
    std::vector<uint32_t> Color;
    std::vector<float> Depth;
    /// FSO_CPP (deferred room light): when non-empty (same size as Color), the rasterizers write unlit colour and the
    /// room id of each pixel here; the light is applied when the layer is composited (World2D), so a lighting change
    /// (time of day, a lamp switched) only re-composites instead of re-rendering every sprite.
    std::vector<uint16_t> Room;
    /// Room id for pixels lit by the outside colour (terrain, roofs).
    static const uint16_t OUTSIDE_LIGHT_ROOM = 65532;
    // Cached RGB is premultiplied. Mixed light domains are replayed with each
    // fragment lit before blending; no room id is reserved for this state.
    std::vector<uint64_t> Prelit;
    bool HasPrelit = false; // conservative layer dependency, reset by a full clear
    bool PrelitAll = false; // allocation failure: cache the whole layer lit
    bool ResolvePrelit = false; // replay preserves the newly discovered mask
    size_t NewPrelitPixels = 0;
    const st::Color* CacheLight = nullptr;
    st::Color CacheOutsideLight = st::Color::White();
    bool DeferredOutsideTint = false; // direct replay preserves clamp-then-outside-tint order
    // Potential dependency, even with no active source, Lighting off or an
    // ambient base-palette bypass. A new source must invalidate that cache.
    bool HasSpatialSurface = false;
    static constexpr int TRANSPARENCY_TILE = 16;
    // Conservative coverage, independent of the lighting dependency above.
    // Partial clears retain blocks; a full clear rebuilds exact coverage.
    std::vector<uint64_t> Transparency;
    bool TransparencyAll = false;
    int TransparencyColumns() const { return (Width + TRANSPARENCY_TILE - 1) / TRANSPARENCY_TILE; }
    int TransparencyRows() const { return (Height + TRANSPARENCY_TILE - 1) / TRANSPARENCY_TILE; }
    bool IsTransparentTile(size_t tile) const {
        return TransparencyAll || (!Transparency.empty() && (Transparency[tile >> 6] & (uint64_t{1} << (tile & 63))));
    }
    void MarkTransparency(size_t pixel) {
        if (TransparencyAll) return;
        if (Transparency.empty()) {
            const size_t tiles = size_t(TransparencyColumns()) * TransparencyRows();
            try { Transparency.assign((tiles + 63) / 64, 0); }
            catch (const std::bad_alloc&) { TransparencyAll = true; return; }
        }
        const size_t tile = (pixel / Width / TRANSPARENCY_TILE) * TransparencyColumns() +
            (pixel % Width / TRANSPARENCY_TILE);
        Transparency[tile >> 6] |= uint64_t{1} << (tile & 63);
    }
    void ShiftTransparency(int sx, int sy) {
        if (TransparencyAll || Transparency.empty()) return;
        auto previous = std::move(Transparency);
        Transparency.clear();
        const int columns = TransparencyColumns(), rows = TransparencyRows();
        for (int y = 0; y < rows && !TransparencyAll; ++y) for (int x = 0; x < columns; ++x) {
            const size_t tile = size_t(y) * columns + x;
            if (!(previous[tile >> 6] & (uint64_t{1} << (tile & 63)))) continue;
            const int x0 = std::max(0, x * TRANSPARENCY_TILE + sx);
            const int y0 = std::max(0, y * TRANSPARENCY_TILE + sy);
            const int x1 = std::min(Width, (x + 1) * TRANSPARENCY_TILE + sx);
            const int y1 = std::min(Height, (y + 1) * TRANSPARENCY_TILE + sy);
            for (int yy = y0 / TRANSPARENCY_TILE; yy * TRANSPARENCY_TILE < y1; ++yy)
                for (int xx = x0 / TRANSPARENCY_TILE; xx * TRANSPARENCY_TILE < x1; ++xx)
                    MarkTransparency(size_t(yy * TRANSPARENCY_TILE) * Width + xx * TRANSPARENCY_TILE);
        }
    }
    bool IsPrelit(size_t pixel) const {
        return PrelitAll || (!Prelit.empty() && (Prelit[pixel >> 6] & (uint64_t{1} << (pixel & 63))));
    }
    void MarkPrelit(size_t pixel) {
        HasPrelit = true;
        if (PrelitAll) return;
        if (Prelit.empty()) {
            try { Prelit.assign((Color.size() + 63) / 64, 0); }
            catch (const std::bad_alloc&) { PrelitAll = true; ++NewPrelitPixels; return; }
        }
        auto& bits = Prelit[pixel >> 6];
        const auto bit = uint64_t{1} << (pixel & 63);
        if (!(bits & bit)) { bits |= bit; ++NewPrelitPixels; }
    }
    void ClearPrelit(size_t pixel) {
        if (!ResolvePrelit && !PrelitAll && !Prelit.empty())
            Prelit[pixel >> 6] &= ~(uint64_t{1} << (pixel & 63));
    }
    void ResetPrelit() {
        if (ResolvePrelit) return;
        Prelit.clear(); HasPrelit = PrelitAll = false; NewPrelitPixels = 0;
        HasSpatialSurface = false;
        Transparency.clear(); TransparencyAll = false;
    }
    st::Color CachedLight(uint16_t room) const {
        if (!CacheLight || room == 65535) return st::Color::White();
        return room == OUTSIDE_LIGHT_ROOM ? CacheOutsideLight : CacheLight[room];
    }
    /// Clip rectangle for the rasterizers (_2DWorldBatch sprites, TriangleRaster), exclusive max. World2D uses it to
    /// redraw only the strip of a cached layer that scrolled into view.
    int32_t ClipX0 = 0, ClipY0 = 0, ClipX1 = 0x7FFFFFFF, ClipY1 = 0x7FFFFFFF;
    void SetClip(int32_t x0, int32_t y0, int32_t x1, int32_t y1) { ClipX0 = x0; ClipY0 = y0; ClipX1 = x1; ClipY1 = y1; }
    void ResetClip() { ClipX0 = 0; ClipY0 = 0; ClipX1 = 0x7FFFFFFF; ClipY1 = 0x7FFFFFFF; }
    /// Clears colour and depth inside [x0,x1) x [y0,y1).
    void ClearRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba) {
        x0 = std::max<int32_t>(0, x0); y0 = std::max<int32_t>(0, y0); x1 = std::min<int32_t>(Width, x1); y1 = std::min<int32_t>(Height, y1);
        for (int32_t y = y0; y < y1; y++) {
            std::fill(Color.begin() + (size_t)y * Width + x0, Color.begin() + (size_t)y * Width + x1, rgba);
            std::fill(Depth.begin() + (size_t)y * Width + x0, Depth.begin() + (size_t)y * Width + x1, std::numeric_limits<float>::infinity());
            if (!Room.empty()) std::fill(Room.begin() + (size_t)y * Width + x0, Room.begin() + (size_t)y * Width + x1, (uint16_t)65535);
            if (!Prelit.empty() && !ResolvePrelit && !PrelitAll)
                for (int32_t x = x0; x < x1; ++x) ClearPrelit((size_t)y * Width + x);
        }
    }

    Framebuffer() {}
    Framebuffer(int32_t w, int32_t h) { Resize(w, h); }

    void Resize(int32_t w, int32_t h) {
        if (w == Width && h == Height && !Color.empty()) return;
        Width = w; Height = h;
        Color.assign((size_t)w * h, 0);
        Depth.assign((size_t)w * h, std::numeric_limits<float>::infinity());
        if (!Room.empty()) Room.assign((size_t)w * h, 65535);
        ResolvePrelit = false;
        ResetPrelit();
    }

    /// Enables/disables the per-pixel room ids (deferred lighting).
    void EnableRoom(bool on) { if (on) Room.assign(Color.size(), 65535); else { Room.clear(); Room.shrink_to_fit(); } ResetPrelit(); }
    void Clear(uint32_t rgba) {
        ResetPrelit();
        std::fill(Color.begin(), Color.end(), rgba);
        if (!Room.empty()) std::fill(Room.begin(), Room.end(), (uint16_t)65535);
        std::fill(Depth.begin(), Depth.end(), std::numeric_limits<float>::infinity());
    }

    static uint32_t Pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
    }

    /// Convert to RGB565 (e.g. for the 3DS framebuffer); dst must hold Width*Height entries.
    void ToRGB565(uint16_t* dst) const {
        size_t n = Color.size();
        for (size_t i = 0; i < n; i++) {
            uint32_t c = Color[i];
            dst[i] = (uint16_t)(((c & 0xF8) << 8) | ((c >> 5) & 0x7E0) | ((c >> 19) & 0x1F));
        }
    }
};

}}
