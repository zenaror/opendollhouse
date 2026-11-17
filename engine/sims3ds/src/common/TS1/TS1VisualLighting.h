// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "core/math.h"
#include "common/TS1/TS1Environment.h"
#include "common/TS1/TS1SpatialLighting.h"

namespace FSO { namespace Common { namespace TS1 {

// DVD software palette contract. A base palette and LUT row 31 are distinct.
inline uint16_t PackVisual565(st::Color color, uint8_t tone) {
    const unsigned r = color.R >> 3, g = color.G >> 2, b = color.B >> 3;
    if (tone == 32) return uint16_t((r << 11) | (g << 5) | b);
    constexpr float inv31 = 1.0f / 31.0f, inv63 = 1.0f / 63.0f;
    const unsigned red = unsigned(double(r * tone) * inv31 + .5);
    const unsigned green = unsigned(double(g * (2 * tone)) * inv63 + .5);
    const unsigned blue = unsigned(double(b * (std::max<unsigned>(1, tone) - 1)) * inv31 + 1.5);
    return uint16_t((red << 11) | (green << 5) | blue);
}
inline st::Color PresentVisual565(st::Color color, uint8_t tone) {
    const auto p = PackVisual565(color, tone);
    const unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
    return st::Color((r << 3) | (r >> 2), (g << 2) | (g >> 4),
        (b << 3) | (b >> 2), color.A);
}
inline uint8_t VisualTone(float factor, int period) {
    factor = std::clamp(factor, .5f, period == 0 ? 1.0f : .8f);
    if (std::abs(factor - 1.0f) <= 1e-6f) return 32;
    return uint8_t(unsigned(double(factor) * 255.0 + .5) >> 3);
}
inline uint8_t VisualByte(float factor) {
    return uint8_t(unsigned(double(factor) * 255.0 + .5));
}
inline uint8_t VisualIntensity(float factor, int period) {
    return VisualByte(std::clamp(factor, .5f, period == 0 ? 1.0f : .8f));
}
inline float DirectVisualFactor(float light, bool pool, bool overhead, int lamps, int people) {
    return pool || (overhead && lamps == 0 && people != 0 && light < .8f) ? .8f : light;
}
inline uint16_t CanonicalRoomID(int story, unsigned local) {
    return uint16_t(((story - 1) << 10) | local);
}

// An explicit TS1 cache key space: two samples per logical environment room,
// map-byte and direct split-side factor. Entity/routing/graphical IDs are unchanged.
// No per-pixel plane is added. Legacy outside/preview keys remain outside this range.
struct VisualLighting {
    static constexpr uint16_t FirstKey = 32768;
    static constexpr uint16_t BaseKey = 65531;
    bool Enabled = false;
    int Width = 0, Height = 0;
    uint64_t Generation = 0, GeometryGeneration = 0;
    std::vector<std::vector<uint32_t>> Maps; // renderer snapshot; copied only with geometry
    std::vector<uint8_t> Grid;
    std::vector<uint8_t> Tones; // even: byte path; odd: direct room path
    // Spatial samples add authored mask bytes before quantizing to a LUT row.
    // A change within one row still invalidates spatially dependent layers.
    std::vector<uint8_t> Intensities;
    uint64_t IntensityGeneration = 0;
    std::vector<std::vector<SpatialCell>> SpatialMaps;
    uint64_t SpatialGeneration = 0;

    const SpatialCell* SpatialAt(int x, int y, int story) const {
        if (!Enabled || story < 1 || size_t(story) > SpatialMaps.size() ||
            x < 0 || y < 0 || x >= Width || y >= Height) return nullptr;
        const auto& cells = SpatialMaps[story - 1];
        const size_t index = size_t(y) * Width + x;
        return index < cells.size() ? &cells[index] : nullptr;
    }

    bool IsKey(uint16_t key) const {
        return Enabled && (key == BaseKey || (key >= FirstKey && size_t(key - FirstKey) < Tones.size()));
    }
    st::Color Transform(st::Color color, uint16_t key) const {
        return PresentVisual565(color, key == BaseKey ? 32 : Tones[key - FirstKey]);
    }
    // Half is in the logical map's canonical orientation. -1 uses the position
    // resolver (actual object subtiles, or explicit tile-center for architecture).
    int Sample(int x, int y, int subX, int subY, int story, uint8_t segments, int half = -1) const {
        if (!Enabled || story < 1 || story > 2 || size_t(story) > Maps.size() ||
            x < 0 || y < 0 || x >= Width || y >= Height) return -1;
        const auto& map = Maps[story - 1];
        const size_t index = size_t(y) * Width + x;
        if (index >= map.size()) return -1;
        const auto cell = map[index];
        const uint16_t low = uint16_t(cell), high = uint16_t(cell >> 16);
        int room = low;
        if (low != high) {
            room = half >= 0 ? int(half ? high : low) :
                ResolveEnvironmentRoom(map, Width, Height, x, y, subX, subY, segments);
        }
        if (room <= 0 || size_t(room * 2 + 1) >= Tones.size()) return -1;
        return FirstKey + room * 2 + int(low != high);
    }
};
}}}
