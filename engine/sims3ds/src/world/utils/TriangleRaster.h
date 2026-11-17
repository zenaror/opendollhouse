/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims: textured, shaded, depth-tested triangle rasterizer for the parts of the lot FreeSims draws in 3D with
// GrassEffect (roofs, terrain). Writes the same world depth as _2DWorldBatch (WorldCamera::Depth, smaller = nearer,
// LessEqual test) into a Framebuffer, so the triangles sort against the 2D sprites exactly like the GPU z-buffer did.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "world/Framebuffer.h"
#include "core/math.h"

namespace FSO { namespace Common { namespace TS1 {
struct TerrainPalette;
struct TerrainBladeSelection;
}}}
namespace FSO { namespace LotView { class WorldState; } }

namespace FSO { namespace LotView { namespace Utils {

/// Screen-space vertex: x,y = target pixels, z = world depth, u,v = texels (wrapped), r,g,b = light (1 = unlit).
struct RasterVertex {
    float x = 0, y = 0, z = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1;
};

enum class RasterFilter : uint8_t { Point, Linear };
struct RasterSampling {
    RasterFilter Filter=RasterFilter::Point;
    bool operator==(const RasterSampling& other) const {
        return Filter==other.Filter;
    }
    bool operator!=(const RasterSampling& other) const {return !(*this==other);}
};

/// RGBA texture. Pixels packed like Framebuffer (R | G<<8 | B<<16 | A<<24).
struct RasterTexture {
    int32_t Width = 0, Height = 0;
    std::vector<uint32_t> Pixels;
    /// Average colour (used for edges / flat fills).
    uint32_t Average = 0xFF808080u;
    bool Valid() const { return Width > 0 && Height > 0 && (int32_t)Pixels.size() == Width * Height; }
    /// Box-filtered half size copy (mip level).
    RasterTexture Half() const;
    /// Texel coordinates: texel(x,y) has center(x+.5,y+.5). Sampling occurs
    /// before material modulation; the triangle's depth/coverage are unchanged.
    uint32_t Sample(float u,float v,const RasterSampling& sampling) const;
};

struct RasterStats { int32_t Triangles = 0, Pixels = 0; };

/// Projection of tile space onto the current 2D batch target: a point P (tile x, y, height z in tiles) lands at
/// pixel WorldSpace.GetScreenFromTile(P) + TileProjectionOrigin(world), exactly where the floor/wall sprites put it
/// (the floor diamond's top vertex is GetScreenFromTile(tile) + (CadgeWidth/2, CadgeBaseLine - TilePxHeight/2), and
/// which tile corner that vertex is depends on the rotation). Includes the batch scroll (world._2D->GetScroll()).
st::Vector2 TileProjectionOrigin(WorldState& world);

/// Draws one triangle. tex == nullptr: flat colour (flatColor) * light. depthBias is added to every depth value.
void DrawTriangle(Framebuffer& fb, const RasterVertex& a, const RasterVertex& b, const RasterVertex& c,
                  const RasterTexture* tex, uint32_t flatColor = 0xFF808080u, float depthBias = 0.0f,
                  RasterStats* stats = nullptr, uint16_t room = Framebuffer::OUTSIDE_LIGHT_ROOM,
                  const RasterSampling* sampling = nullptr);

/// Original terrain scan in left, top, right, bottom order. Vertex u/v carry
/// grass/shade indices. The source clip defines the DVD sampling frame in
/// target coordinates; the framebuffer clip independently limits writes.
void DrawOriginalTerrainQuad(Framebuffer& fb, const std::array<RasterVertex,4>& corners,
                             const std::array<int,4>& sourceHeights, int baseYAtZero, int zoom,
                             const FSO::Common::TS1::TerrainPalette& palette, float depthBias,
                             RasterStats* stats, const st::Rectangle* sourceClip = nullptr,
                             const FSO::Common::TS1::TerrainBladeSelection* blades = nullptr);

}}}
