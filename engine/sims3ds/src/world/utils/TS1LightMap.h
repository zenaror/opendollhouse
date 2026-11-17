// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace FSO { namespace LotView { namespace Utils {

struct TS1LightMask {
    const uint8_t* Data = nullptr;
    int Width = 0, Height = 0;
    bool Read(int x, int y, uint8_t& value) const {
        if (!Data || x < 0 || y < 0 || x >= Width || y >= Height) return false;
        value = Data[size_t(y) * Width + x];
        return true;
    }
};

// Immutable authored samples for one zoom. Keep only the selected zoom's
// payloads; pointers remain valid until another successful Load or Reset.
class TS1LightMap {
public:
    static constexpr size_t FileBytes = 2271104;
    using Reader = std::function<bool(size_t, size_t, std::vector<uint8_t>&)>;
    bool Load(int zoom, size_t fileBytes, const Reader& read, std::string& error);
    bool Prepare(const std::string& basePath, int zoom);
    void Reset();
    bool Ready() const { return Zoom >= 1 && Zoom <= 3; }
    int SelectedZoom() const { return Zoom; }
    uint64_t Generation() const { return PublishedGeneration; }
    size_t ResidentBytes() const { return Floor.size() + Walls[0].size() + Walls[1].size(); }
    TS1LightMask FloorMask() const;
    TS1LightMask WallMask(int type, int dx, int dy) const;
    static void RotatePair(int& dx, int& dy, int rotation);
    bool FloorSample(int dx, int dy, int u, int v, uint8_t& value) const;

private:
    int Zoom = 0, AttemptedZoom = 0;
    std::string AttemptedBase;
    bool Attempted = false;
    uint64_t PublishedGeneration = 0;
    std::vector<uint8_t> Floor;
    std::array<std::vector<uint8_t>, 2> Walls;
};
}}}
