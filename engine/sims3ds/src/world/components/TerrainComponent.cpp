/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.LotView.Components.TerrainComponent — software terrain mesh, see TerrainComponent.h
#include "world/components/TerrainComponent.h"
#include "world/utils/BuildGrid.h"
#include "world/model/Blueprint.h"
#include "world/WorldState.h"
#include "world/Framebuffer.h"
#include "common/TS1/TS1TerrainLighting.h"
#include "world/utils/_2DWorldBatch.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace FSO { namespace LotView { namespace Components {

using namespace FSO::LotView::Utils;

// FSO.LotView.Model.LotTypeGrassInfo (FreeSO)
namespace LotTypeGrassInfo {
static const st::Color LightGreen[8] = {
    st::Color(80, 116, 59), st::Color(181, 171, 149), st::Color(126, 96, 70), st::Color(240, 245, 250), st::Color(0, 0, 255),
    st::Color(74, 89, 66), //TS1 Dark Grass
    st::Color(140, 113, 49), //TS1 Autumn Grass
    st::Color(240, 245, 250), //TS1 Cloud
};
static const st::Color LightBrown[8] = {
    st::Color(157, 117, 65), st::Color(196, 185, 162), st::Color(126, 96, 70), st::Color(240, 245, 250), st::Color(0, 0, 255),
    st::Color(90, 69, 41), st::Color(115, 73, 33), st::Color(15, 20, 140),
};
static const st::Color DarkGreen[8] = {
    st::Color(8, 52, 8), st::Color(115, 109, 95), st::Color(107, 77, 57), st::Color(180, 180, 190), st::Color(0, 0, 255),
    st::Color(21, 30, 13), st::Color(109, 63, 35), st::Color(180, 180, 190),
};
static const st::Color DarkBrown[8] = {
    st::Color(81, 60, 18), st::Color(121, 114, 100), st::Color(107, 77, 57), st::Color(180, 180, 190), st::Color(0, 0, 255),
    st::Color(64, 69, 14), st::Color(56, 35, 17), st::Color(15, 20, 140),
};
}

TerrainComponent::TerrainComponent(const st::Rectangle& size) : Area(size) {
    GenerateGrassStates();
    UpdateLotType();
}

void TerrainComponent::SetTS1HouseNumber(int32_t houseNumber) {
    // FreeSO VMTS1Activator.HouseNumToType
    TerrainType ttype = TerrainType::GRASS;
    switch (houseNumber) {
        case 28: case 29: case 46: case 47: case 48: ttype = TerrainType::SAND; break;
        case 40: case 41: case 42: ttype = TerrainType::SNOW; break;
        case 90: case 91: case 92: case 93: case 94: ttype = TerrainType::TS1DarkGrass; break;
        case 95: case 96: ttype = TerrainType::TS1AutumnGrass; break;
        case 99: ttype = TerrainType::TS1Cloud; break;
        default: break;
    }
    LightType = (ttype == TerrainType::SAND) ? TerrainType::GRASS : ttype;
    DarkType = ttype;
    UpdateLotType();
    TerrainDirty = true;
}

void TerrainComponent::UpdateLotType() {
    int index = (int)LightType;
    LightGreen = LotTypeGrassInfo::LightGreen[index];
    DarkGreen = LotTypeGrassInfo::DarkGreen[index];
    if (LightType != DarkType) {
        auto dindex = (int)DarkType;
        LightBrown = LotTypeGrassInfo::LightGreen[dindex];
        DarkBrown = LotTypeGrassInfo::DarkGreen[dindex];
    } else {
        LightBrown = LotTypeGrassInfo::LightBrown[index];
        DarkBrown = LotTypeGrassInfo::DarkBrown[index];
    }
}

void TerrainComponent::SetGrassState(const std::vector<uint8_t>& grass, int32_t width) {
    GrassState = grass;
    GrassWidth = width;
    TerrainDirty = true;
}

void TerrainComponent::SetHeights(const std::vector<float>& heights, int32_t width) {
    Heights = heights;
    HeightWidth = width;
    OriginalShadePeriod = -1;
    OriginalBoundsDirty = true;
    TerrainDirty = true;
}

void TerrainComponent::SetOriginalSource(const std::vector<uint8_t>& heightQuads,
        const std::vector<uint8_t>& grassBytes,int32_t storageWidth) {
    const size_t cells=storageWidth>0 && storageWidth<=64 ? size_t(storageWidth)*storageWidth : 0;
    OriginalStorageWidth=cells && heightQuads.size()==cells*4 && grassBytes.size()==cells ? storageWidth : 0;
    if(OriginalStorageWidth) { OriginalSourceHeights=heightQuads; OriginalSourceGrass=grassBytes; }
    else { OriginalSourceHeights.clear(); OriginalSourceGrass.clear(); }
    OriginalShadePeriod=-1; OriginalBoundsDirty=true; TerrainDirty=true;
    if(Blueprint) Blueprint->SignalFloorChange();
}

float TerrainComponent::GetElevationPoint(int x, int y) const {
    if (Heights.empty() || HeightWidth <= 0) return 0.0f;
    x = std::max<int32_t>(0, std::min<int32_t>(HeightWidth - 1, x));
    y = std::max<int32_t>(0, std::min<int32_t>(HeightWidth - 1, y));
    size_t i = (size_t)(y * HeightWidth + x);
    return (i < Heights.size()) ? Heights[i] : 0.0f;
}

void TerrainComponent::GenerateGrassStates() { //generates a set of grass states for a lot.
    //right now only works for square lots, but that's all tso has!
    // FSO_CPP: C# uses an unseeded System.Random; a fixed-seed LCG keeps renders reproducible. Only used when the
    // house file has no lawn state (TS1 houses always do).
    uint32_t seed = 0x5EED1234u;
    auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
    int width = std::max<int32_t>(1, Area.Width + 2);
    std::vector<float> result((size_t)(width * width), 0.0f);
    int initial = width / 4; //divide by more for less noisyness!
    if (initial >= 2) {
        float factor = 0.5f / ((int)std::log2((double)initial));
        while (initial > 0) {
            auto squared = initial * initial;
            std::vector<float> noise((size_t)squared);
            for (int i = 0; i < squared; i++) noise[(size_t)i] = (float)next() * factor;
            int offset = 0;
            for (int x = 0; x < width; x++) {
                double xInt = (x / (double)(width - 1)) * (initial - 1);
                for (int y = 0; y < width; y++) {
                    double yInt = (y / (double)(width - 1)) * (initial - 1);
                    float tl = noise[(size_t)(std::floor(yInt) * initial + std::floor(xInt))];
                    float tr = noise[(size_t)(std::floor(yInt) * initial + std::ceil(xInt))];
                    float bl = noise[(size_t)(std::ceil(yInt) * initial + std::floor(xInt))];
                    float br = noise[(size_t)(std::ceil(yInt) * initial + std::ceil(xInt))];
                    float p = (float)std::fmod(xInt, 1.0);
                    float q = (float)std::fmod(yInt, 1.0);
                    result[(size_t)offset++] += (tl * (1 - p) + tr * (p)) * (1 - q) + (bl * (1 - p) + br * (p)) * q; //don't you love 2 dimensional linear interpolation?? ;)
                }
            }
            initial /= 2;
        }
    }
    GrassState.resize(result.size());
    for (size_t i = 0; i < result.size(); i++) GrassState[i] = (uint8_t)std::min(255.0f, result[i] * 255.0f);
    GrassWidth = width;
}

// Tileable grass grain (FreeSO draws blades with GrassShader; this is the flat-texture equivalent): value noise
// in two octaves plus per-texel speckles, mostly darker blades. 200 = unmodulated.
void TerrainComponent::GenerateBlades() {
    if (Blades.Valid()) return;
    const int N = 128;
    Blades.Width = Blades.Height = N;
    Blades.Pixels.resize((size_t)(N * N));
    auto hash = [](uint32_t x, uint32_t y, uint32_t s) {
        uint32_t h = x * 374761393u + y * 668265263u + s * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
    };
    auto value = [&](float x, float y, int cell, uint32_t s) {
        int c = N / cell;
        float fx = x / cell, fy = y / cell;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
        float tx = fx - x0, ty = fy - y0;
        tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        auto h = [&](int a, int b) { return hash((uint32_t)(((a % c) + c) % c), (uint32_t)(((b % c) + c) % c), s); };
        float a = h(x0, y0) * (1 - tx) + h(x0 + 1, y0) * tx;
        float b = h(x0, y0 + 1) * (1 - tx) + h(x0 + 1, y0 + 1) * tx;
        return a * (1 - ty) + b * ty;
    };
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float l = 1.0f + 0.16f * (value((float)x, (float)y, 32, 1) - 0.5f) + 0.10f * (value((float)x, (float)y, 8, 2) - 0.5f);
        float r = hash((uint32_t)x, (uint32_t)y, 3);
        if (r < 0.22f) l *= 0.78f + r;          // dark blade
        else if (r > 0.94f) l *= 1.12f;          // lit blade tip
        else l *= 0.96f + 0.08f * hash((uint32_t)x, (uint32_t)y, 4);
        int g = std::max(0, std::min(255, (int)(l * 200)));
        Blades.Pixels[(size_t)(y * N + x)] = Framebuffer::Pack((uint8_t)g, (uint8_t)g, (uint8_t)g, 255);
    }
}

void TerrainComponent::RegenTerrain(WorldState& world, FSO::LotView::Model::Blueprint* blueprint) {
    (void)world;
    Blueprint = blueprint;
    TerrainDirty = false;
}

float TerrainComponent::StateAt(int x, int y) const {
    if (GrassState.empty() || GrassWidth <= 0) return 0.0f;
    x = std::max<int32_t>(0, std::min<int32_t>(GrassWidth - 1, x));
    y = std::max<int32_t>(0, std::min<int32_t>(GrassWidth - 1, y));
    size_t i = (size_t)(y * GrassWidth + x);
    return (i < GrassState.size()) ? GrassState[i] / 255.0f : 0.0f;
}

int TerrainComponent::OriginalGrassSample(int x,int y) const {
    if(x>=0 && y>=0 && x<GrassWidth && y<GrassWidth) {
        const size_t index=size_t(y)*GrassWidth+x;
        if(index<GrassState.size()) return 127-int(GrassState[index]);
    }
    if(x>=0 && y>=0 && x<OriginalStorageWidth && y<OriginalStorageWidth) {
        const int value=OriginalSourceGrass[size_t(y)*OriginalStorageWidth+x];
        return value<128 ? value : value-256;
    }
    if(GrassWidth<=0 || GrassState.empty()) return 0;
    x=std::clamp(x,0,GrassWidth-1); y=std::clamp(y,0,GrassWidth-1);
    const size_t index=size_t(y)*GrassWidth+x;
    return index<GrassState.size() ? 127-int(GrassState[index]) : 0;
}

std::array<int,4> TerrainComponent::OriginalHeightQuad(int x,int y) const {
    using namespace FSO::Common::TS1;
    const bool haveSource=x>=0 && y>=0 && x<OriginalStorageWidth && y<OriginalStorageWidth;
    TerrainCorners authored{},baseline{},current{};
    const int xs[4]={x,x,x+1,x+1},ys[4]={y+1,y,y,y+1};
    for(int i=0;i<4;++i) {
        const int vx=xs[i],vy=ys[i];
        const bool live=vx>=0 && vy>=0 && vx<HeightWidth && vy<HeightWidth;
        const int liveValue=Blueprint && Blueprint->TerrainFactor>0 ?
            int(std::lround(double(GetElevationPoint(vx,vy))/(double(Blueprint->TerrainFactor)*10)+
                double(Blueprint->BaseAlt)/10)) : 0;
        authored[size_t(i)]=haveSource ? OriginalSourceHeights[(size_t(y)*OriginalStorageWidth+x)*4+i] : liveValue;
        // The full saved B1 grid supplies vertices outside the live crop.
        // Never substitute the nearest live boundary for an available source.
        const bool saved=vx>=0 && vy>=0 && vx<OriginalStorageWidth && vy<OriginalStorageWidth;
        baseline[size_t(i)]=saved ? OriginalSourceHeights[(size_t(vy)*OriginalStorageWidth+vx)*4+1] : authored[size_t(i)];
        current[size_t(i)]=live ? liveValue : baseline[size_t(i)];
    }
    auto result=BindTerrainHeightQuad(authored,baseline,current,haveSource);
    if(Blueprint) for(auto& value:result) value=int(std::lround(double(value)-double(Blueprint->BaseAlt)/10));
    return result;
}

std::array<int,4> TerrainComponent::OriginalTerrainShadeCorners(int x,int y) const {
    using namespace FSO::Common::TS1;
    if(!Blueprint || OriginalShadePeriod<0 || OriginalShadeSourceWidth<=0) return {};
    const int width=OriginalShadeSourceWidth,rotation=OriginalShadeRotation;
    const auto q=TerrainCameraTileForWorld({{x,y}},width,rotation);
    const int xs[4]={q[0],q[0],q[0]+1,q[0]+1},ys[4]={q[1]+1,q[1],q[1],q[1]+1};
    TerrainCorners result{};
    for(int i=0;i<4;++i) {
        if(xs[i]<0 || ys[i]<0 || xs[i]>=width || ys[i]>=width) continue;
        result[size_t(i)]=OriginalShades[size_t(ys[i])*width+xs[i]];
    }
    uint8_t flags=0;
    if(Blueprint->Walls && !Blueprint->Walls->empty() && x>=0 && y>=0 && x<Blueprint->Width && y<Blueprint->Height) {
        const size_t tile=size_t(y)*Blueprint->Width+x;
        if(tile<(*Blueprint->Walls)[0].size()) flags=uint8_t((*Blueprint->Walls)[0][tile].Segments);
    }
    return ApplyTerrainWallShade(result,TerrainWallFlagsForCamera(flags,rotation));
}

int TerrainComponent::OriginalGrassAt(int x,int y) const {
    return FSO::Common::TS1::TerrainGrassLevel({{OriginalGrassSample(x,y),OriginalGrassSample(x-1,y),
        OriginalGrassSample(x-1,y-1),OriginalGrassSample(x,y-1)}});
}

bool TerrainComponent::BuildOriginalShades(int period,int rotation,int sourceWidth) {
    // The original shader owns a camera-local grid, including its RNG order.
    if (period<0 || period>3 || rotation<0 || rotation>3 || sourceWidth<HeightWidth || sourceWidth>64 ||
        HeightWidth<=0 || Heights.size()!=size_t(HeightWidth)*HeightWidth ||
        Blueprint==nullptr || Blueprint->TerrainFactor<=0) return false;
    if (OriginalShadePeriod==period && OriginalShadeRotation==rotation &&
        OriginalShadeSourceWidth==sourceWidth) return true;
    using namespace FSO::Common::TS1;
    const TerrainNormals normals(period);
    TerrainRandom random;
    OriginalShades.assign(size_t(sourceWidth)*sourceWidth,0);
    const auto corners = [&](int x,int y) {
        const auto world=TerrainWorldTileForCamera({{x,y}},sourceWidth,rotation);
        return TerrainRotateCorners(OriginalHeightQuad(world[0],world[1]),rotation);
    };
    // Consume the renderer grid, rather than the ARRY storage stride. The
    // original rebuilds in x-major/y-inner order; retain the rendered crop.
    for(int x=1;x<sourceWidth;++x) for(int y=1;y<sourceWidth;++y) {
        OriginalShades[size_t(y)*sourceWidth+x]=uint8_t(normals.Vertex(corners(x,y),
            corners(x-1,y),corners(x-1,y-1),corners(x,y-1),&random));
    }
    if(OriginalShadeRotation!=rotation) OriginalBoundsDirty=true;
    OriginalShadePeriod=period; OriginalShadeRotation=rotation;
    OriginalShadeSourceWidth=sourceWidth;
    if(OriginalBoundsDirty) {
        OriginalStripHeights.fill(0);
        // Source geometry-wide bounds are independent of the current dirty
        // scissor. Retained scrolling needs the old fringe even on small draws.
        const int x0=std::max(Area.Left(),1), y0=std::max(Area.Top(),1);
        const int x1=std::min(Area.Right(),HeightWidth-1), y1=std::min(Area.Bottom(),HeightWidth-1);
        for(int x=x0;x<x1;++x) for(int y=y0;y<y1;++y) {
            const auto h=TerrainRotateCorners(OriginalHeightQuad(x,y),rotation);
            for(int zoom=1;zoom<=3;++zoom) for(int side : {0,2}) {
                const int first=h[size_t(side)]-h[3]+2;
                const int last=first+((2<<zoom)-1)*(h[1]-h[3]+4);
                OriginalStripHeights[size_t(zoom)]=std::max(OriginalStripHeights[size_t(zoom)],
                    std::max(first,last));
            }
        }
        OriginalBoundsDirty=false;
    }
    return true;
}

void TerrainComponent::DrawInto(WorldState& world, Framebuffer& target) {
    LastStats = RasterStats();
    LastOriginalStripHeight = 0;
    if (Blueprint == nullptr) return;
    // Ordinary DVD grass uses its own indexed source ramp in every camera.
    // Other lot types and neutral thumbnails retain their established path.
    const bool original = !world.RenderingThumbnail &&
        LightType==TerrainType::GRASS && DarkType==TerrainType::GRASS &&
        GrassWidth==HeightWidth && GrassState.size()==size_t(GrassWidth)*GrassWidth &&
        BuildOriginalShades(world.TerrainPeriod,int(world.Rotation()),Blueprint->Width);
    const bool grain = !world.SimpleTerrain || world.RenderingThumbnail;
    if (grain && !original) GenerateBlades();
    auto& ws = world.WorldSpace;
    auto K = TileProjectionOrigin(world);
    auto cam = world.Camera();
    const int zoom = int(world.Zoom());
    LastOriginalStripHeight=original && zoom>=1 && zoom<=3 ? OriginalStripHeights[size_t(zoom)] : 0;
    const int detail = world.SimpleTerrain ? 0 : world.TerrainDetail;
    const bool originalBlades = original && detail>0 && detail<=2 && zoom>=1 && zoom<=3;
    if(originalBlades) {
        if(OriginalBladePool.Detail!=detail || OriginalBladePool.Zoom!=zoom)
            OriginalBladePool.Reset(zoom,detail);
        if(OriginalBladeSeedWidth!=Blueprint->Width) {
            OriginalBladeSeeds=FSO::Common::TS1::TerrainBladeTileSeeds(Blueprint->Width);
            OriginalBladeSeedWidth=Blueprint->Width;
        }
    }

    st::Color out = world.OutsideColor;
    if (out.A == 0 || !target.Room.empty() || target.DeferredOutsideTint) out = st::Color::White(); // tint after raster clamp
    // blade texture: 200 = 1.0
    float lr = out.R / (255.0f * 200.0f), lg = out.G / (255.0f * 200.0f), lb = out.B / (255.0f * 200.0f);
    // texels per tile: ~2 screen pixels per texel at every zoom
    float tpt = ws.TilePxWidth / 2.0f;

    // vertex grid over the terrain area (one more vertex than tiles on each axis)
    int x0 = Area.Left(), y0 = Area.Top(), x1 = std::min(Area.Right(), Blueprint->Width), y1 = std::min(Area.Bottom(), Blueprint->Height);
    if (world.RenderingThumbnail && world.ThumbnailGroundBounds.Width > 0) {
        x0 = std::max<int32_t>(x0, world.ThumbnailGroundBounds.Left());
        y0 = std::max<int32_t>(y0, world.ThumbnailGroundBounds.Top());
        x1 = std::min<int32_t>(x1, world.ThumbnailGroundBounds.Right());
        y1 = std::min<int32_t>(y1, world.ThumbnailGroundBounds.Bottom());
    }
    if (x1 <= x0 || y1 <= y0) return;
    int vw = x1 - x0 + 1;
    std::vector<RasterVertex> verts((size_t)(vw * (y1 - y0 + 1)));
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        // the colour of a vertex blends the lawn state of the tiles around it (C# GrassState per vertex)
        float state = (StateAt(x - 1, y - 1) + StateAt(x, y - 1) + StateAt(x - 1, y) + StateAt(x, y)) * 0.25f;
        st::Vector3 p((float)x, (float)y, GetElevationPoint(x, y));
        auto s = ws.GetScreenFromTile(p);
        auto& v = verts[(size_t)((y - y0) * vw + (x - x0))];
        v.x = s.X + K.X; v.y = s.Y + K.Y;
        v.z = cam->Depth(WorldSpace::GetWorldFromTile(p));
        if (original) {
            v.u=float(OriginalGrassAt(x,y));
            // Shade is assigned per camera-local quad, after wall flags.
            v.v=0;
            continue;
        }
        v.u = x * tpt; v.v = y * tpt;
        auto c = st::Color::Lerp(LightGreen, LightBrown, state);
        auto d = st::Color::Lerp(DarkGreen, DarkBrown, state);
        // FreeSO mixes in the dark colour at the blade roots; a quarter of it on the surface
        float r = c.R * 0.8f + d.R * 0.2f, g = c.G * 0.8f + d.G * 0.2f, b = c.B * 0.8f + d.B * 0.2f;
        // TS1(sims): hills are shaded (FreeSO's grass shader lights the mesh normal): a fixed sun from the lot's
        // top-left, slopes facing it brighter, the others darker (heights in tile units)
        float shade = 1.0f;
        // The neutral save capture, like pinned GetLotThumb/ClearLighting,
        // does not bake the software world's heuristic slope illumination.
        if (grain && !world.RenderingThumbnail && !Heights.empty()) {
            float dx = GetElevationPoint(x + 1, y) - GetElevationPoint(x - 1, y);
            float dy = GetElevationPoint(x, y + 1) - GetElevationPoint(x, y - 1);
            shade = std::max(0.55f, std::min(1.35f, 1.0f + 0.9f * (dx + dy)));
        }
        v.r = r * lr * shade; v.g = g * lg * shade; v.b = b * lb * shade;
    }
    // FSO_CPP: behind the floor plane, so floor sprites always cover the grass: the generated floor z-buffers step
    // 1.525 per row (exact would be 1.594) and are quantized to bytes, so floors are up to ~0.2 world units behind
    // the exact plane.
    const float bias = 0.4f;
    float cx0 = (float)std::max<int32_t>(0, target.ClipX0), cy0 = (float)std::max<int32_t>(0, target.ClipY0);
    float cx1 = (float)std::min(target.Width, target.ClipX1), cy1 = (float)std::min(target.Height, target.ClipY1);
    const auto* grassTexture = grain ? &Blades : nullptr;
    const FSO::Common::TS1::TerrainPalette* originalPalette = nullptr;
    if (original) {
        static const FSO::Common::TS1::TerrainPalette palette;
        originalPalette = &palette;
    }
    // The vertex colors are normalized for the grass texture's neutral value (200).
    constexpr uint32_t flatGrass = 0xffc8c8c8u;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        auto& a = verts[(size_t)((y - y0) * vw + (x - x0))];
        auto& b = verts[(size_t)((y - y0) * vw + (x + 1 - x0))];
        auto& c = verts[(size_t)((y + 1 - y0) * vw + (x + 1 - x0))];
        auto& d = verts[(size_t)((y + 1 - y0) * vw + (x - x0))];
        float minX = std::min(std::min(a.x, b.x), std::min(c.x, d.x)), maxX = std::max(std::max(a.x, b.x), std::max(c.x, d.x));
        float minY = std::min(std::min(a.y, b.y), std::min(c.y, d.y)), maxY = std::max(std::max(a.y, b.y), std::max(c.y, d.y));
        if (original) {
            // Source H order is left, top, right, bottom. The native depth
            // corners remain available; source heights drive strip geometry.
            const int rotation=int(world.Rotation());
            const auto heights=FSO::Common::TS1::TerrainRotateCorners(OriginalHeightQuad(x,y),rotation);
            const std::array<RasterVertex,4> worldCorners{{d,a,b,c}};
            const int cornerX[4]={x,x,x+1,x+1},cornerY[4]={y+1,y,y,y+1};
            std::array<RasterVertex,4> corners;
            const auto shades=OriginalTerrainShadeCorners(x,y);
            for(int i=0;i<4;++i) {
                corners[size_t(i)]=worldCorners[size_t((i-rotation)&3)];
                corners[size_t(i)].v=float(shades[size_t(i)]);
            }
            // Source projection differs from the inherited mesh on slopes.
            // Cull using the exact source bounds, not that legacy mesh's Y.
            const int left=(-rotation)&3;
            const int baseY=int(std::lround(ws.GetScreenFromTile(st::Vector3(float(cornerX[left]),float(cornerY[left]),0)).Y+K.Y));
            const int n=2<<zoom;
            int sourceTop=std::numeric_limits<int>::max();
            int sourceBottom=std::numeric_limits<int>::min();
            for(int side : {0,2}) for(int k : {0,n-1}) {
                const int firstY=baseY-n*heights[size_t(side)]-
                    k*(heights[1]-heights[size_t(side)]+2);
                const int span=heights[size_t(side)]-heights[3]+2+
                    k*(heights[1]-heights[3]+4);
                sourceTop=std::min(sourceTop,firstY);
                sourceBottom=std::max(sourceBottom,firstY+span);
            }
            const int bladeMargin=originalBlades && zoom==3 ? 3 : 0;
            if(maxX<cx0 || sourceBottom<cy0 || minX>=cx1 || sourceTop-bladeMargin>=cy1) continue;
            const auto* sourceClip=world.TerrainSourceClip.Width>0 ? &world.TerrainSourceClip : nullptr;
            FSO::Common::TS1::TerrainBladeSelection blades;
            const size_t tile=size_t(y)*Blueprint->Width+x;
            // Ordinary no-floor grass is the bounded source admission here.
            // Split floors and alternate environment selectors stay separate.
            const bool noFloor=Blueprint->Floors && !Blueprint->Floors->empty() &&
                tile<(*Blueprint->Floors)[0].size() && (*Blueprint->Floors)[0][tile].Pattern==0;
            const bool poolClear=tile<Blueprint->TerrainPoolBlocked.size() &&
                Blueprint->TerrainPoolBlocked[tile]==0;
            const auto q=FSO::Common::TS1::TerrainCameraTileForWorld({{x,y}},Blueprint->Width,rotation);
            const size_t seed=size_t(q[1])*OriginalBladeSeedWidth+q[0];
            if(originalBlades && noFloor && poolClear && q[0]>=0 && q[1]>=0 &&
                q[0]<OriginalBladeSeedWidth && q[1]<OriginalBladeSeedWidth && seed<OriginalBladeSeeds.size())
                blades=FSO::Common::TS1::SelectTerrainBlades(OriginalBladePool,
                    OriginalBladeSeeds[seed],OriginalGrassSample(x,y));
            DrawOriginalTerrainQuad(target,corners,heights,baseY,zoom,*originalPalette,bias,&LastStats,
                sourceClip,blades.Pool ? &blades : nullptr);
        } else {
            if (maxX < cx0 || maxY < cy0 || minX >= cx1 || minY >= cy1) continue;
            DrawTriangle(target, a, b, c, grassTexture, flatGrass, bias, &LastStats);
            DrawTriangle(target, a, c, d, grassTexture, flatGrass, bias, &LastStats);
        }
    }
    if (!world.RenderingThumbnail && (DrawGrid || world.BuildMode()>0)) {
        const auto eligible=[&](int x,int y) -> bool {
            if(x<x0 || y<y0 || x>=x1 || y>=y1)return false;
            const size_t index=size_t(y)*Blueprint->Width+x;
            if(Blueprint->FineArea && index<Blueprint->FineArea->size())return (*Blueprint->FineArea)[index];
            return Blueprint->BuildableArea.Contains(x,y);
        };
        const auto line=[&](const RasterVertex& a,const RasterVertex& b) {
            DrawBuildGridEdge(target,a.x,a.y,a.z+bias,b.x,b.y,b.z+bias);
        };
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)if(eligible(x,y)) {
            const auto& a=verts[size_t(y-y0)*vw+x-x0];
            const auto& b=verts[size_t(y-y0)*vw+x+1-x0];
            const auto& c=verts[size_t(y+1-y0)*vw+x+1-x0];
            const auto& d=verts[size_t(y+1-y0)*vw+x-x0];
            line(a,b);line(a,d);
            if(!eligible(x+1,y))line(b,c);
            if(!eligible(x,y+1))line(d,c);
        }
    }

}

}}}
