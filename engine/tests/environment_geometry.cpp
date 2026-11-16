// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1EnvironmentGeometry.h"
#include <cstdio>
#include <cstdlib>
#include <set>

using namespace FSO::Common::TS1;
static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static std::vector<EnvironmentGeometryTile> Enclosure(uint16_t style) {
    std::vector<EnvironmentGeometryTile> tiles(49);
    for (auto& tile : tiles) {
        tile.TopLeftStyle = tile.TopRightStyle = style;
        tile.TopLeftPattern = tile.TopRightPattern = 1;
        tile.BottomLeftPattern = tile.BottomRightPattern = 1;
    }
    for (int x = 2; x <= 4; ++x) {
        tiles[2 * 7 + x].Segments |= 2; tiles[1 * 7 + x].Segments |= 8;
        tiles[4 * 7 + x].Segments |= 8; tiles[5 * 7 + x].Segments |= 2;
    }
    for (int y = 2; y <= 4; ++y) {
        tiles[y * 7 + 2].Segments |= 1; tiles[y * 7 + 1].Segments |= 4;
        tiles[y * 7 + 4].Segments |= 4; tiles[y * 7 + 5].Segments |= 1;
        for (int x = 2; x <= 4; ++x) tiles[y * 7 + x].Floor = true;
    }
    return tiles;
}

int main() {
    // The original pending queue produces repeated visits even on an empty
    // story. Area/objects must retain those visits, rather than using unique area.
    std::vector<EnvironmentGeometryTile> empty(48 * 48);
    const auto upper = BuildEnvironmentGeometry(empty, 48, 48, 2);
    Require(upper.Rooms.size() == 1 && upper.Rooms[0].Outside, "Empty story is exterior");
    Require(upper.Rooms[0].Visits.size() == 4515, "PC empty-story visit sequence");
    Require(upper.Rooms[0].Inputs.HalfTiles == 9030, "Repeated visits contribute to half-area");
    Require(std::set<int>(upper.Rooms[0].Visits.begin(), upper.Rooms[0].Visits.end()).size() == 2304,
        "Repeated visits still cover every unique tile");

    for (uint16_t style : {1, 2, 3, 4, 12, 13, 14, 22, 23}) {
        const auto tiles = Enclosure(style);
        const auto result = BuildEnvironmentGeometry(tiles, 7, 7, 1);
        Require(result.Rooms.size() == 2, "Fence and wall segments divide logical rooms");
        const auto interior = uint16_t(result.Map[3 * 7 + 3]);
        const auto& room = result.Rooms[interior];
        const bool pass = style == 2 || style == 12 || style == 13 || style == 14;
        Require(room.Outside == pass, "Fence styles pass exterior connectivity");
        if (!pass) {
            Require(room.Inputs.CoveredHalfTiles == room.Inputs.HalfTiles, "Complete floor coverage");
            Require(room.Inputs.CoveredWallCount == room.Inputs.WallCount, "Complete wall covering");
            Require((room.Inputs.WallCount > 0) == (style != 4), "Only original eligible wall styles score");
        }
    }

    auto poolTiles = Enclosure(1);
    poolTiles[3 * 7 + 3].Pool = true;
    const auto pool = BuildEnvironmentGeometry(poolTiles, 7, 7, 1);
    Require(pool.Rooms.size() == 3, "First-floor pool mask divides the room flood");
    const auto poolID = uint16_t(pool.Map[3 * 7 + 3]);
    Require(pool.Rooms[poolID].Inputs.GroundGoodCount == int(pool.Rooms[poolID].Visits.size()),
        "Pool-mask visits supply the original good-count predicate");
    const auto poolUpper = BuildEnvironmentGeometry(poolTiles, 7, 7, 2);
    Require(poolUpper.Rooms.size() == 2, "Upper-story partition ignores the first-floor pool mask");

    // A diagonal inside an open room can be reached from both ends; its two
    // sides then resolve to the same room without leaving a missing half.
    for (uint8_t diagonal : {16, 32}) {
        auto tiles = Enclosure(1);
        tiles[3 * 7 + 3].Segments = diagonal;
        auto result = BuildEnvironmentGeometry(tiles, 7, 7, 1);
        const auto cell = result.Map[3 * 7 + 3];
        Require(uint16_t(cell) == uint16_t(cell >> 16), "Connected diagonal halves collapse");
        Require(!result.Rooms[uint16_t(cell)].Outside, "Diagonal keeps enclosed room classification");
        Require(result.Rooms[uint16_t(cell)].Inputs.CoveredHalfTiles ==
                result.Rooms[uint16_t(cell)].Inputs.HalfTiles, "Diagonal floor coverage is retained");
    }
    // Cutting one corner leaves an exterior and an interior half of the same
    // tile. It must not merge their logical room scores.
    auto cutCorner = Enclosure(1);
    cutCorner[4 * 7 + 4].Segments = 16;
    cutCorner[4 * 7 + 5].Segments &= uint8_t(~1);
    cutCorner[5 * 7 + 4].Segments &= uint8_t(~2);
    auto corner = BuildEnvironmentGeometry(cutCorner, 7, 7, 1);
    const auto mixed = corner.Map[4 * 7 + 4];
    Require(uint16_t(mixed) != uint16_t(mixed >> 16), "Diagonal corner retains two rooms");
    Require(!corner.Rooms[uint16_t(mixed)].Outside && corner.Rooms[uint16_t(mixed >> 16)].Outside,
        "Northwest and southeast diagonal faces keep their own exterior state");

    auto bare = Enclosure(1);
    for (auto& tile : bare) {
        tile.Floor = false;
        tile.TopLeftPattern = tile.TopRightPattern = 0;
        tile.BottomLeftPattern = tile.BottomRightPattern = 0;
    }
    const auto unpainted = BuildEnvironmentGeometry(bare, 7, 7, 1);
    const auto room = uint16_t(unpainted.Map[3 * 7 + 3]);
    Require(unpainted.Rooms[room].Inputs.CoveredHalfTiles == 0 &&
        unpainted.Rooms[room].Inputs.WallCount > 0 &&
        unpainted.Rooms[room].Inputs.CoveredWallCount == 0, "Bare floors and walls have zero coverage");
    bare[3 * 7 + 2].Segments &= uint8_t(~1);
    bare[3 * 7 + 1].Segments &= uint8_t(~4);
    const auto opening = BuildEnvironmentGeometry(bare, 7, 7, 1);
    Require(opening.Rooms.size() == 1 && opening.Rooms[0].Outside,
        "Removing a wall connects the former interior to the exterior");
    std::puts("Original environment geometry checks passed");
}
