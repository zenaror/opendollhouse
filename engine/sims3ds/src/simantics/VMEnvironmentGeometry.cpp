// SPDX-License-Identifier: MPL-2.0
#include "simantics/VMContext.h"
#include "simantics/VM.h"
#include "common/TS1/TS1EnvironmentGeometry.h"
#include <algorithm>

namespace FSO { namespace SimAntics {
void VMContext::RebuildEnvironmentGeometry() {
    using namespace FSO::Common::TS1;
    EnvironmentRooms.clear();
    EnvironmentMaps.clear();
    EnvironmentTileRooms.clear();
    EnvironmentRooms.emplace_back(); // zero remains an absent room
    const auto* arch = Architecture();
    if (!arch || !VM || !VM->TS1) return;
    EnvironmentMaps.resize(size_t(arch->Stories));
    EnvironmentTileRooms.resize(size_t(arch->Stories));
    std::vector<EnvironmentGeometryTile> tiles(size_t(arch->Width) * arch->Height);
    // The PC game has two logical stories. Extra renderer/routing placeholders
    // must not multiply exterior pool/object contributions.
    const int stories = std::min(2, arch->Stories);
    for (int level = 0; level < stories; ++level) {
        const auto& walls = arch->Walls[level];
        const auto& floors = arch->Floors[level];
        const auto& floorPresent = arch->TS1FloorPresent[level];
        for (size_t i = 0; i < tiles.size(); ++i) {
            const auto& wall = walls[i];
            auto& tile = tiles[i];
            tile.Segments = uint8_t(wall.Segments);
            tile.TopLeftStyle = arch->TS1WallStyleTopLeft[level].size() == tiles.size() ?
                arch->TS1WallStyleTopLeft[level][i] : wall.TopLeftStyle;
            tile.TopRightStyle = arch->TS1WallStyleTopRight[level].size() == tiles.size() ?
                arch->TS1WallStyleTopRight[level][i] : wall.TopRightStyle;
            tile.TopLeftPattern = wall.TopLeftPattern;
            tile.TopRightPattern = wall.TopRightPattern;
            tile.BottomLeftPattern = wall.BottomLeftPattern;
            tile.BottomRightPattern = wall.BottomRightPattern;
            tile.Floor = floorPresent.size() == tiles.size() ? floorPresent[i] != 0 :
                floors[i].Pattern != 0 && floors[i].Pattern < 65534;
            tile.Pool = arch->TS1PoolTiles.size() == tiles.size() ? arch->TS1PoolTiles[i] != 0 :
                arch->Floors[0][i].Pattern == 65535;
        }
        auto geometry = BuildEnvironmentGeometry(tiles, arch->Width, arch->Height, level + 1);
        const uint32_t base = uint32_t(EnvironmentRooms.size());
        auto& map = EnvironmentMaps[level];
        map.resize(geometry.Map.size());
        for (size_t i = 0; i < map.size(); ++i) {
            const auto cell = geometry.Map[i];
            const uint32_t low = uint16_t(cell), high = uint16_t(cell >> 16);
            map[i] = (low < geometry.Rooms.size() ? low + base : 0) |
                ((high < geometry.Rooms.size() ? high + base : 0) << 16);
        }
        for (auto& source : geometry.Rooms) {
            const auto id = uint16_t(EnvironmentRooms.size());
            EnvironmentRooms.emplace_back();
            auto& target = EnvironmentRooms.back();
            target.Environment = source.Inputs;
            target.Outside = source.Outside;
            for (const auto tile : source.Visits) ++target.EnvironmentTileVisits[tile];
            for (const auto& entry : target.EnvironmentTileVisits)
                EnvironmentTileRooms[level][entry.first].emplace_back(id, entry.second);
        }
    }
    EnvironmentObjectsDirty = true;
    EnvironmentPeriod = -1;
}
}}
