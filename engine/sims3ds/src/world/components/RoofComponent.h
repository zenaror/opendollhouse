/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.LotView.Components.RoofComponent (World/components/RoofComponent.cs)
//
// Original roofs use a128-half-cell admission grid, topology-selected reusable
// prototypes and factory-scoped material modes. Admission/height inputs come
// from the scene producer; this component keeps classification and CPU geometry
// until those inputs, style or pitch change. Camera placement is cached apart
// from canonical prototype identity and material history.
// The previous rectangle mesh remains for explicitly unbound source contexts.
// Source UVs/normals do not establish original driver raster/filter parity.
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "core/math.h"
#include "world/components/WorldComponent.h"
#include "world/model/LotTilePos.h"
#include "world/utils/TriangleRaster.h"
#include "common/TS1/TS1RoofLighting.h"
#include "common/TS1/TS1RoofGeometry.h"

namespace FSO { namespace LotView {
class Framebuffer;
namespace Model { class Blueprint; }
namespace Components {

class RoofComponent : public WorldComponent {
public:
    FSO::LotView::Model::Blueprint* blueprint = nullptr;

    uint32_t Style = 16;   // C# RoofStyle (WorldRoofProvider id)
    float Pitch = 0.66f;   // C# RoofPitch (TS1 HOUS: 0.4 / 0.6 / 1.0, 0 = flat)
    bool StyleDirty = false;
    bool ShapeDirty = true;

    void SetStylePitch(uint32_t style, float pitch);
    // Full original storage, source stories1/2. Producer owns room/floor/stair
    // admission and publishes only on input changes; no scene inference here.
    bool SetSourceAdmission(const std::vector<FSO::Common::TS1::RoofAdmissionLevel>& levels,
        const std::vector<uint8_t>& heightQuads,int sourceWidth=64);
    bool HasSourceAdmission() const {return SourceWidth==64;}
    const std::vector<FSO::Common::TS1::RoofClassification>& GetSourceClassifications() const {return SourceClassifications;}
    const FSO::Common::TS1::RoofPrototypeFactory& GetSourceFactory() const {return SourceFactory;}
    size_t GetSourceUnsupportedGroundCount(int camera) const;
    const std::vector<FSO::Common::TS1::RoofAdmissionLevel>& GetSourceAdmissions() const {return SourceAdmission;}
    const std::vector<uint8_t>& GetSourceHeightQuads() const {return SourceHeightQuads;}
    uint64_t GetRenderGeneration() const {return RenderGeneration;}
    // Original-source CPU work, independent of retained raster redraw counts.
    uint64_t GetClassificationBuildCount() const {return ClassificationBuildCount;}
    uint64_t GetMeshBuildCount() const {return MeshBuildCount;}

    // Explicit source material snapshot. The world owns cache invalidation when
    // this returns true; geometry rebuilds do not infer or reset mesh modes.
    bool SetLighting(const FSO::Common::TS1::RoofLightingInput& input);
    const FSO::Common::TS1::RoofLightingInput& GetLighting() const { return Lighting; }
    bool SetCharacterDetail(int rawDetail,bool applyExisting);
    bool SetLightingAmbient(const FSO::Common::TS1::RoofRGB& ambient);
    int GetRawCharacterDetail() const { return MaterialState.RawDetail; }
    bool HasMaterials() const { return MaterialState.HasMaterials || !SourceFactory.Prototypes().empty(); }

    class RoofRect {
    public:
        int32_t ExpandDir = 0;
        int32_t x1, x2, y1, y2;
        RoofRect(int32_t x1_, int32_t y1_, int32_t x2_, int32_t y2_);
        int32_t GetByDir(int dir) const;
        void SetByDir(int dir, int32_t value);
        bool Contains(const st::Point& pt) const { return (pt.X >= x1 && pt.X <= x2) && (pt.Y >= y1 && pt.Y <= y2); }
    };

    /// Regenerates the rectangles of every roof level (C# RegenRoof(device)).
    void RegenRoof();
    void RegenRoof(int8_t level);
    /// Rebuilds the meshes (C# RemeshRoof / MeshRects) and reloads the texture.
    void RemeshRoof();

    bool TileIndoors(int x, int y, int level) const;
    bool IndoorsOrFloor(int x, int y, int level) const;
    bool IsRoofable(const FSO::LotView::Model::LotTilePos& pos) const;

    /// Draws the roofs of the levels up to world.Level()+1 into target (with its scroll = world._2D->GetScroll()).
    void DrawInto(WorldState& world, Framebuffer& target);
    void Draw(WorldState& world) override { (void)world; } // drawn by World2D through DrawInto

    float PreferredDrawOrder() override { return 0.0f; }
    /// Statistics of the last DrawInto.
    Utils::RasterStats LastStats;
    size_t RectCount() const;

private:
    struct RoofVertex { st::Vector3 Tile; float U, V; float Shade; };
    struct RoofQuad { RoofVertex V[4]; int Face; bool Fascia; };
    std::vector<std::vector<RoofRect>> RoofRects; // index level-2
    std::vector<std::vector<RoofQuad>> Meshes;    // index level-2
    Utils::RasterTexture SourceTexture; // Actual authored dimensions/base level.
    Utils::RasterTexture Mips[3];                 // 32x32, 16x16, 8x8
    std::string LoadedTexture;
    FSO::Common::TS1::RoofLightingInput Lighting;
    FSO::Common::TS1::RoofUniformMaterialState MaterialState;

    struct SourceTriangle {
        FSO::Common::TS1::RoofPrototypeTriangle Geometry;
        uint16_t Kind;
    };
    struct SourceLevelMeshes {
        std::array<std::vector<SourceTriangle>,4> Camera;
        std::array<bool,4> Valid{{false,false,false,false}};
        std::array<size_t,4> UnsupportedGround{{0,0,0,0}};
    };
    uint64_t RenderGeneration=1,ClassificationBuildCount=0,MeshBuildCount=0;
    int SourceWidth=0;
    std::vector<uint8_t> SourceHeightQuads;
    std::vector<FSO::Common::TS1::RoofAdmissionLevel> SourceAdmission;
    std::vector<FSO::Common::TS1::RoofClassification> SourceClassifications;
    std::vector<SourceLevelMeshes> SourceMeshes;
    FSO::Common::TS1::RoofPrototypeFactory SourceFactory;
    void InvalidateSourceMeshes();
    void BuildSourceMesh(size_t level,int camera);
    void DrawSourceRoof(WorldState& world,Framebuffer& target);

    void MeshRects(int level);
    void PrepTexture();
    void RoofSpread(const FSO::LotView::Model::LotTilePos& start, std::vector<bool>& evaluated, int width, int height,
                    int8_t level, std::vector<RoofRect>& result);
    st::Vector3 ToTilePos(int x, int y, int z, int level, float pitch); // TS1(sims): + altitude (FreeSO), no longer static
};

}}}
