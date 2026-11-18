/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Model.VMArchitectureTerrain (FreeSO tso.simantics/Model/VMArchitectureTerrain.cs)
// TS1(sims): FreeSims has no terrain model; this is FreeSO's, which reads the TS1 altitude grid (house ARRY 0,
// one byte per vertex, stored here x10 like FreeSO) and the lawn state (ARRY 6). The perlin grass generator and the
// network serialization are not needed (TS1 houses always carry their lawn state).
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace FSO { namespace SimAntics { namespace Model {

class VMArchitectureTerrain {
public:
    int32_t Width;
    int32_t Height;

    std::vector<int16_t> Heights;   // per vertex (x, y) = tile corner, FreeSO units (TS1 altitude x 10)
    std::vector<int16_t> Centers;   // per tile: average of its four corners
    std::vector<bool> Sloped;       // per tile: corners differ
    std::vector<uint8_t> GrassState; // 0 = green, 255 = brown

    std::vector<int16_t> VisHeights;
    std::vector<uint8_t> VisGrass;

    VMArchitectureTerrain(int32_t width, int32_t height);

    /// C# swaps the arrays for the preview copies (SimulateCommands); here the real arrays are saved and restored.
    void EnterVis();
    void ExitVis();
    void RegenerateCenters();

    // ARRY #0 is the authoritative TS1 packed four-corner terrain data.
    // A VM created from a marshal alone has no such source and returns false.
    bool SetTS1HeightSource(const std::vector<uint8_t>& raw);
    bool GetTS1HeightQuad(int x, int y, std::array<int, 4>& result) const;

private:
    std::vector<int16_t> OldHeights;
    std::vector<uint8_t> OldGrass;
    std::vector<uint8_t> TS1HeightSource;
};

}}}
