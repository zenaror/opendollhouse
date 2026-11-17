// SPDX-License-Identifier: MPL-2.0
#include "world/utils/TS1LightMap.h"
#include "content/WorldContentFiles.h"
#include "files/FilePool.h"
#include <cstdio>
#include <new>
#include <utility>

namespace FSO { namespace LotView { namespace Utils {
namespace {
constexpr std::array<int, 4> Radii{{4,4,3,2}};
constexpr std::array<size_t, 3> FloorOffsets{{1744256,1769344,1869696}};
struct Layout {
    size_t WallOffset[2][3]{};
    Layout() {
        size_t offset = 252416;
        for (int family = 0; family < 2; ++family) for (int z = 1; z <= 3; ++z) {
            WallOffset[family][z - 1] = offset;
            const size_t bytes = size_t(16 << (z - 1)) * ((58 << (z - 1)) + (8 << z));
            for (int radius : Radii) offset += size_t(2 * radius + 1) * bytes;
        }
    }
};
const Layout Authored;
}

bool TS1LightMap::Load(int zoom, size_t fileBytes, const Reader& read, std::string& error) {
    if (zoom < 1 || zoom > 3 || fileBytes != FileBytes || !read) {
        error = "invalid lightMap.dat size or zoom";
        return false;
    }
    try {
        std::vector<uint8_t> floor;
        std::array<std::vector<uint8_t>, 2> walls;
        const size_t wallBytes = size_t(30) * (16 << (zoom - 1)) * ((58 << (zoom - 1)) + (8 << zoom));
        const size_t floorBytes = size_t(224 << (zoom - 1)) * (112 << (zoom - 1));
        for (int family = 0; family < 2; ++family) {
            if (!read(Authored.WallOffset[family][zoom - 1], wallBytes, walls[family]) ||
                walls[family].size() != wallBytes) {
                error = "incomplete authored wall light masks";
                return false;
            }
        }
        if (!read(FloorOffsets[zoom - 1], floorBytes, floor) || floor.size() != floorBytes) {
            error = "incomplete authored floor light mask";
            return false;
        }
        // Publish only after every read succeeds. Never expose mixed zooms.
        Floor.swap(floor); Walls.swap(walls); Zoom = zoom; ++PublishedGeneration;
        return true;
    } catch (const std::bad_alloc&) {
        error = "insufficient memory for authored light masks";
        return false;
    }
}

bool TS1LightMap::Prepare(const std::string& basePath, int zoom) {
    if (Attempted && AttemptedBase == basePath && AttemptedZoom == zoom) return Ready() && Zoom == zoom;
    Attempted = true; AttemptedBase = basePath; AttemptedZoom = zoom;
    // The installation is immutable during a session. Resolve only on an
    // explicit root/zoom transition, never once per frame or fragment.
    const auto path = Content::WorldContentFiles::FindInstall(basePath, "GameData/lightMap.dat");
    std::string error;
    const auto size = path.empty() ? int64_t(-1) : Files::FilePool::Size(path, &error);
    const bool loaded = size >= 0 && Load(zoom, size_t(size), [&](size_t offset, size_t bytes, auto& out) {
        return Files::FilePool::ReadAt(path, int64_t(offset), bytes, out, &error);
    }, error);
    if (!loaded) {
        // Discard a previous zoom too: its bytes must never light this view.
        Zoom = 0; Floor.clear(); Walls[0].clear(); Walls[1].clear(); ++PublishedGeneration;
        std::fprintf(stderr, "[lighting] authored masks unavailable zoom=%d: %s\n", zoom,
            error.empty() ? "GameData/lightMap.dat not found" : error.c_str());
    }
    return loaded;
}

void TS1LightMap::Reset() {
    Zoom = AttemptedZoom = 0; Attempted = false; AttemptedBase.clear(); ++PublishedGeneration;
    std::vector<uint8_t>().swap(Floor);
    for (auto& wall : Walls) std::vector<uint8_t>().swap(wall);
}
TS1LightMask TS1LightMap::FloorMask() const {
    return Ready() ? TS1LightMask{Floor.data(),224 << (Zoom - 1),112 << (Zoom - 1)} : TS1LightMask{};
}
TS1LightMask TS1LightMap::WallMask(int type, int dx, int dy) const {
    if (!Ready() || (type != 1 && type != 2) || dx < -4 || dx > 4 || dy < -4 || dy > 4 ||
        (type == 1 ? dx : dy) > 0) return {};
    const int depth = -(type == 1 ? dx : dy), lateral = type == 1 ? -dy : dx;
    if (depth > 3 || lateral < -Radii[depth] || lateral > Radii[depth]) return {};
    const int width = 16 << (Zoom - 1), height = (58 << (Zoom - 1)) + (8 << Zoom);
    const size_t bytes = size_t(width) * height;
    size_t offset = 0;
    for (int d = 0; d < depth; ++d) offset += size_t(2 * Radii[d] + 1) * bytes;
    offset += size_t(lateral + Radii[depth]) * bytes;
    return {Walls[type - 1].data() + offset,width,height};
}
void TS1LightMap::RotatePair(int& dx, int& dy, int rotation) {
    const int x = dx, y = dy;
    switch (rotation & 3) {
        case 1: dx = -y; dy = x; break;
        case 2: dx = -x; dy = -y; break;
        case 3: dx = y; dy = -x; break;
    }
}
bool TS1LightMap::FloorSample(int dx, int dy, int u, int v, uint8_t& value) const {
    if (!Ready() || dx < -3 || dx > 3 || dy < -3 || dy > 3) return false;
    const int step = 32 << (Zoom - 1);
    return FloorMask().Read((6 + dx - dy) * (step / 2) + u,
        (6 + dx + dy) * (step / 4) + v, value);
}
}}}
