// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace FSO { namespace Common { namespace TS1 {
struct EnvironmentTunings {
    float ObjectLightFactor = 30;
    float AreaClip = 60, AreaZero = -30, AreaFull = 30;
    float LightZero = -20, LightFull = 20;
    float ImpactAreaMin = 10, ImpactAreaMax = 45, ImpactMultiplier = 10;
    float OutdoorImpactFactor = 2, OutdoorGoodFactor = 1, OutdoorDivisor = 3;
    float OutdoorDay = 0, OutdoorTwilight = -20, OutdoorNight = -50;
    float FloorZero = -40, FloorFull = 0, WallZero = -40, WallFull = 0;
};

// Geometry is collected on architecture changes, independently of graphics.
// Object sums are refreshed when placement or authored contribution changes.
struct EnvironmentInputs {
    int32_t HalfTiles = 0, CoveredHalfTiles = 0;
    int32_t WallCount = 0, CoveredWallCount = 0;
    int32_t GroundGoodCount = 0, GoodObjectCount = 0;
    int32_t RoomImpact = 0, WindowLight = 0, ArtificialLight = 0;
};
struct EnvironmentResult { float Score = 0, LightFactor = 0; };

// Original object-position lookup resolves split cells through neighboring
// whole cells. A diagonal does not unconditionally select one stored half.
inline int32_t ResolveEnvironmentRoom(const std::vector<uint32_t>& map, int width, int height,
    int tileX, int tileY, int subX, int subY, uint8_t segments) {
    auto valid = [=](int x,int y) { return x > 0 && y > 0 && x < width-1 && y < height-1; };
    auto whole = [&](int x,int y) {
        const size_t index = size_t(y) * size_t(width) + size_t(x);
        if (index >= map.size()) return int32_t(-5);
        const auto value = map[index];
        const auto low = uint16_t(value), high = uint16_t(value >> 16);
        return low != 0 && low == high ? int32_t(low) : int32_t(-5);
    };
    if (!valid(tileX,tileY)) return -5;
    const auto direct = whole(tileX,tileY);
    if (direct != -5) return direct;
    int dx=0, dy=0;
    if (segments & 16) {
        dx = dy = subX + subY < 16 ? -1 : 1;
    } else if (segments & 32) {
        dx = subX < subY ? -1 : 1;
        dy = -dx;
    } else return -5;
    if (!valid(tileX+dx,tileY)) return -5;
    const auto first = whole(tileX+dx,tileY);
    if (first != -5) return first;
    return valid(tileX,tileY+dy) ? whole(tileX,tileY+dy) : -5;
}

inline float EnvironmentDaylight(int period) {
    return period == 2 ? 0.0f : (period == 1 || period == 3 ? .5f : 1.0f);
}
inline EnvironmentResult EvaluateEnvironment(const EnvironmentInputs& in,
    const EnvironmentTunings& t, bool outside, float daylight) {
    EnvironmentResult result;
    if (outside) {
        result.LightFactor = daylight;
        result.Score = in.RoomImpact * t.OutdoorImpactFactor +
            float(in.GroundGoodCount + in.GoodObjectCount) * t.OutdoorGoodFactor;
        return result;
    }
    if (in.HalfTiles <= 0) return result; // absent/dummy room, never a real scored interior
    const float area = float(in.HalfTiles / 2); // DVD uses integer half-count division
    const float artificial = float((double(in.ArtificialLight) * double(.01f) / area) * t.ObjectLightFactor);
    const double natural = ((double(in.WindowLight) * double(.01f) / area) * daylight) * t.ObjectLightFactor;
    const float naturalBounded = float(std::min(double(daylight), natural));
    double lightSum = double(naturalBounded) + artificial;
    if (lightSum < double(.01f)) lightSum = 0;
    else if (lightSum > 0.9900000002235174) lightSum = 1;
    const float light = float(lightSum);
    result.LightFactor = light;
    const float impactArea = std::max(t.ImpactAreaMin, std::min(area, t.ImpactAreaMax));
    // Preserve the original's stored area/light terms and wider final sum.
    // This avoids an extra rounding step at every addition on non-x87 hosts.
    const float areaScore = float(double(std::min(area, t.AreaClip)) / t.AreaClip *
        float(t.AreaFull - t.AreaZero) + t.AreaZero);
    const float lightScore = float(double(light) * float(t.LightFull - t.LightZero) + t.LightZero);
    const double wallScore = double(in.CoveredWallCount) / in.WallCount *
        float(t.WallFull - t.WallZero) + t.WallZero;
    const double floorScore = double(in.CoveredHalfTiles) / in.HalfTiles *
        float(t.FloorFull - t.FloorZero) + t.FloorZero;
    const float score = float(wallScore + floorScore +
        double(in.RoomImpact) * t.ImpactMultiplier / impactArea + lightScore + areaScore);
    // Match the original branch order, including its lower result for an
    // undefined degenerate-room ratio. Ordinary occupied rooms have both counts.
    result.Score = !(score >= -100.0f) ? -100.0f : std::min(100.0f, score);
    return result;
}
inline float EvaluateOutdoorEnvironment(float sum, const EnvironmentTunings& t, int period) {
    float result = sum / t.OutdoorDivisor;
    if (period == 0) result += t.OutdoorDay;
    else if (period == 1 || period == 3) result += t.OutdoorTwilight;
    else if (period == 2) result += t.OutdoorNight;
    return !(result >= -100.0f) ? -100.0f : std::min(100.0f, result);
}
}}}
