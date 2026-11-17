/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.LotView.Components.TerrainComponent (World/components/TerrainComponent.cs, lot types from FreeSO)
//
// Ordinary TS1 grass uses its original indexed color ramps, camera-local
// corner lighting and deterministic blade samples. Other terrain types and
// neutral thumbnails retain the software mesh/texture path below. Both paths
// share projection, clipping, depth and the separate interactive build grid.
// Lot types (FreeSO VMTS1Activator.HouseNumToType + LotTypeGrassInfo): grass, sand (beach lots 28/29/46-48: grass
// fading to sand), snow (Vacation 40-42), Magic Town dark grass (90-94) and autumn grass (95/96), cloud (99).
// Depth: the terrain is written slightly behind the floor plane so floor sprites always win on equal depth.
#pragma once
#include <cstdint>
#include <vector>
#include "core/math.h"
#include "world/components/WorldComponent.h"
#include "world/utils/TriangleRaster.h"
#include "common/TS1/TS1TerrainBlades.h"

namespace FSO { namespace LotView {
class Framebuffer;
namespace Model { class Blueprint; }
namespace Components {

/// FSO.Content.Model.TerrainType (values as in FreeSO).
enum class TerrainType : int { GRASS = 0, SAND = 1, ROCK = 2, SNOW = 3, WATER = 4, TS1DarkGrass = 5, TS1AutumnGrass = 6, TS1Cloud = 7 };

class TerrainComponent : public WorldComponent {
public:
    explicit TerrainComponent(const st::Rectangle& size);

    st::Rectangle Area;       // C# private Size
    bool TerrainDirty = false;
    bool DepthMode = false;
    bool DrawGrid = false;

    TerrainType LightType = TerrainType::GRASS;
    TerrainType DarkType = TerrainType::GRASS;

    float PreferredDrawOrder() override { return 0.0f; }

    /// FreeSO VMTS1Activator: lot type from the TS1 house number.
    void SetTS1HouseNumber(int32_t houseNumber);
    /// FreeSO UpdateTerrain: light/dark types (UpdateLotType picks the colours).
    void UpdateLotType();
    /// Lawn state per tile, row major blueprint-sized (0 = green, 255 = brown; TS1 ARRY 6 as (byte)(127 - x)).
    void SetGrassState(const std::vector<uint8_t>& grass, int32_t width);
    /// Vertex altitude in tiles (width x width vertices, same layout as GrassState). Empty = flat.
    void SetHeights(const std::vector<float>& heights, int32_t width);
    // Original row-major ARRY0 L/T/R/B bytes and signed ARRY6 bits. Storage
    // width is independent of the cropped live lot/renderer width.
    void SetOriginalSource(const std::vector<uint8_t>& heightQuads,
                           const std::vector<uint8_t>& grassBytes,int32_t storageWidth);
    std::array<int,4> OriginalTerrainShadeCorners(int worldX,int worldY) const;
    float GetElevationPoint(int x, int y) const;

    void RegenTerrain(WorldState& world, FSO::LotView::Model::Blueprint* blueprint);
    /// Draws the terrain mesh into target (the floor layer; scroll = world._2D->GetScroll()).
    void DrawInto(WorldState& world, Framebuffer& target);
    void Draw(WorldState& world) override { (void)world; }

    Utils::RasterStats LastStats;
    int MaxOriginalStripHeight() const { return LastOriginalStripHeight; }

private:
    st::Color LightGreen = st::Color(80, 116, 59);
    st::Color LightBrown = st::Color(157, 117, 65);
    st::Color DarkGreen = st::Color(8, 52, 8);
    st::Color DarkBrown = st::Color(81, 60, 18);
    std::vector<uint8_t> GrassState; //0 = green, 255 = brown. to start with, should be randomly distriuted in range 0-128.
    int32_t GrassWidth = 0;
    std::vector<float> Heights;
    int32_t HeightWidth = 0;
    FSO::LotView::Model::Blueprint* Blueprint = nullptr;
    Utils::RasterTexture Blades; // 128x128 tileable luminance (x200 = 1.0)

    // Original terrain caches indices, never RGB from a room/outside tint.
    std::vector<uint8_t> OriginalShades; // camera-local vertices, row major
    std::vector<uint8_t> OriginalSourceHeights, OriginalSourceGrass;
    int OriginalStorageWidth = 0;
    int LastOriginalStripHeight = 0;
    std::array<int,4> OriginalStripHeights{};
    bool OriginalBoundsDirty = true;
    FSO::Common::TS1::TerrainBladePool OriginalBladePool;
    std::vector<uint16_t> OriginalBladeSeeds;
    int OriginalBladeSeedWidth = 0;
    int OriginalShadePeriod = -1;
    int OriginalShadeRotation = -1;
    int OriginalShadeSourceWidth = 0;
    bool BuildOriginalShades(int period, int rotation, int sourceWidth);
    std::array<int,4> OriginalHeightQuad(int worldX,int worldY) const;
    int OriginalGrassSample(int x, int y) const;
    int OriginalGrassAt(int x, int y) const;

    void GenerateGrassStates();
    void GenerateBlades();
    float StateAt(int x, int y) const;
};

}}}
