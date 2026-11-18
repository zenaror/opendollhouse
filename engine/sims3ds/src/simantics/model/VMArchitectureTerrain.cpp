/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Model.VMArchitectureTerrain (FreeSO) — see the header.
#include "simantics/model/VMArchitectureTerrain.h"
#include "common/TS1/TS1TerrainLighting.h"

namespace FSO { namespace SimAntics { namespace Model {

VMArchitectureTerrain::VMArchitectureTerrain(int32_t width, int32_t height) : Width(width), Height(height) {
    size_t n = (size_t)(width * height);
    Heights.assign(n, 0);
    GrassState.assign(n, 0);
    Centers.assign(n, 0);
    Sloped.assign(n, false);
    VisHeights.assign(n, 0);
    VisGrass.assign(n, 0);
}

bool VMArchitectureTerrain::SetTS1HeightSource(const std::vector<uint8_t>& raw) {
    constexpr size_t sourceSize = 64u * 64u * 4u;
    if (raw.size() != sourceSize) {
        TS1HeightSource.clear();
        return false;
    }
    TS1HeightSource = raw;
    return true;
}

bool VMArchitectureTerrain::GetTS1HeightQuad(int x, int y, std::array<int, 4>& result) const {
    constexpr int sourceWidth = 64;
    if (TS1HeightSource.size() != size_t(sourceWidth * sourceWidth * 4) ||
        x < 0 || y < 0 || x >= Width || y >= Height || x >= sourceWidth || y >= sourceWidth ||
        Width <= 0 || Height <= 0 || Heights.size() != size_t(Width) * size_t(Height)) return false;

    FSO::Common::TS1::TerrainCorners authored{}, baseline{}, current{};
    const int xs[4] = {x, x, x + 1, x + 1};
    const int ys[4] = {y + 1, y, y, y + 1};
    for (size_t i = 0; i < 4; ++i) {
        authored[i] = TS1HeightSource[(size_t(y) * sourceWidth + size_t(x)) * 4 + i];
        const int vx = xs[i], vy = ys[i];
        const bool saved = vx < sourceWidth && vy < sourceWidth;
        baseline[i] = saved ? TS1HeightSource[(size_t(vy) * sourceWidth + size_t(vx)) * 4 + 1] : authored[i];
        const bool live = vx < Width && vy < Height;
        // Use the same integer conversion as ARRY serialization; preview
        // rendering currently rounds fractional heights instead.
        current[i] = live ? int(Heights[size_t(vy) * size_t(Width) + size_t(vx)]) / 10
                          : baseline[i];
    }
    result = FSO::Common::TS1::BindTerrainHeightQuad(authored, baseline, current, true);
    return true;
}

void VMArchitectureTerrain::EnterVis() {
    OldHeights = Heights;
    OldGrass = GrassState;
    VisHeights = Heights;
    VisGrass = GrassState;
}

void VMArchitectureTerrain::ExitVis() {
    VisHeights = Heights;   // the commands ran on the live arrays: they are the preview
    VisGrass = GrassState;
    Heights.swap(OldHeights);
    GrassState.swap(OldGrass);
    OldHeights.clear(); OldHeights.shrink_to_fit();
    OldGrass.clear(); OldGrass.shrink_to_fit();
    RegenerateCenters();
}

void VMArchitectureTerrain::RegenerateCenters() { //TODO: partial update
    int i = 0;
    for (int y = 0; y < Height; y++) {
        for (int x = 0; x < Width; x++) {
            auto h1 = Heights[(size_t)i];
            auto h2 = Heights[(size_t)(y * Width + ((x + 1) % Width))];
            auto h3 = Heights[(size_t)(((y + 1) % Height) * Width + ((x + 1) % Width))];
            auto h4 = Heights[(size_t)(((y + 1) % Height) * Width + x)];

            Centers[(size_t)i] = (int16_t)((Heights[(size_t)i] + h2 + h3 + h4) / 4);
            Sloped[(size_t)i] = (h1 != h2) || (h1 != h3) || (h1 != h4);
            i++;
        }
    }
}

}}}
