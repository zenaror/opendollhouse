// SPDX-License-Identifier: MPL-2.0
#include "world/utils/TriangleRaster.h"
#include "common/TS1/TS1TerrainBlades.h"
#include "common/TS1/TS1TerrainLighting.h"
#include <algorithm>
#include <cmath>

namespace FSO { namespace LotView { namespace Utils {
namespace {

// Keep native world depth compatible with the floor, walls and sprites. The
// source scan, including its four-pixel stair steps, determines coverage.
struct DepthPlane {
    const RasterVertex& Origin;
    float Dx = 0, Dy = 0;

    DepthPlane(const RasterVertex& side, const RasterVertex& top, const RasterVertex& bottom)
        : Origin(side) {
        const float tx = top.x - side.x, ty = top.y - side.y;
        const float bx = bottom.x - side.x, by = bottom.y - side.y;
        const float tz = top.z - side.z, bz = bottom.z - side.z;
        const float area = tx*by - bx*ty;
        if (std::fabs(area) > 1e-6f) {
            Dx = (tz*by - bz*ty) / area;
            Dy = (tx*bz - bx*tz) / area;
        } else if (tx != 0) {
            // A vertical fold can collapse the native triangle while an
            // original edge strip still covers pixels. Use its mean edge depth.
            Dx = (tz+bz) / (2*tx);
        }
    }
    float At(int x, int y, float bias) const {
        return Origin.z + bias + Dx*(x+0.5f-Origin.x) + Dy*(y+0.5f-Origin.y);
    }
};

struct StripIndex {
    int Top, PerRow;
    int At(int row) const { return (Top + row*PerRow) >> 8; }
};

StripIndex Interpolate(int side, int top, int bottom, int progress, int strips, int height) {
    // Each legal zoom divides 256 exactly. These edge steps therefore match
    // the original arithmetic shift even when the signed difference is negative.
    const int topEdge = side*256 + progress*((top-side)*256/strips);
    const int bottomEdge = side*256 + progress*((bottom-side)*256/strips);
    return {topEdge, (bottomEdge-topEdge)/height}; // x86 IDIV truncates toward zero.
}

} // namespace

void DrawOriginalTerrainQuad(Framebuffer& fb, const std::array<RasterVertex,4>& corners,
                             const std::array<int,4>& h, int baseYAtZero, int zoom,
                             const Common::TS1::TerrainPalette& palette, float depthBias,
                             RasterStats* stats, const st::Rectangle* sourceClip,
                             const Common::TS1::TerrainBladeSelection* blades) {
    if (zoom < 1 || zoom > 3 || int64_t(h[3])-h[1] > 5) return;
    const auto extremes = std::minmax_element(h.begin(), h.end());
    if (int64_t(*extremes.second)-*extremes.first > 255) return; // Four source bytes share a baseline.

    const st::Rectangle view = sourceClip ? *sourceClip : st::Rectangle(0, 0, fb.Width, fb.Height);
    const int x0 = std::max({0, fb.ClipX0, view.Left()});
    const int x1 = std::min({fb.Width, fb.ClipX1, view.Right()});
    const int y0 = std::max({0, fb.ClipY0, view.Top()});
    const int y1 = std::min({fb.Height, fb.ClipY1, view.Bottom()});
    if (x0 >= x1 || y0 >= y1) return;

    const int strips = 2 << zoom;
    const double left = std::round(double(corners[0].x));
    if (!(left < x1 && left+8*strips > x0)) return;
    const int leftX = int(left);
    const int growth = h[1]-h[3]+4;
    const int bladeReach = blades && blades->Pool && blades->Count > 0 ? (zoom == 3 ? 4 : 1) : 0;
    int drawn = 0;
    for (int side : {0,2}) {
        const int topStep = h[side]-h[1]-2;
        const int firstHeight = h[side]-h[3]+2;
        const int64_t sideY = int64_t(baseYAtZero) - int64_t(strips)*h[side];
        const int64_t minimumY = sideY + std::min(0,(strips-1)*topStep);
        const int64_t maximumEnd = sideY + firstHeight + std::max(0,(strips-1)*(topStep+growth));
        if (maximumEnd <= y0 || minimumY >= int64_t(y1)+bladeReach) continue;
        const auto& v = corners[side];
        const DepthPlane plane(v, corners[1], corners[3]);
        const auto store = [&](int x, int y, uint32_t color) {
            const auto pixel = size_t(y)*fb.Width + x;
            const float depth = plane.At(x,y,depthBias);
            if (!(depth <= fb.Depth[pixel])) return;
            fb.Color[pixel] = color;
            fb.Depth[pixel] = depth;
            if (!fb.Room.empty()) {
                fb.ClearPrelit(pixel);
                fb.Room[pixel] = 65535;
            }
            ++drawn;
        };
        bool visible = false;
        for (int k=0; k<strips; ++k) {
            const int band = side == 0 ? k : 2*strips-1-k;
            const int x = leftX+4*band;
            // Original horizontal clipping admits complete four-pixel bands.
            // The framebuffer repaint scissor may mask any subset of their lanes.
            if (x < view.Left() || x+4 > view.Right() || x >= x1 || x+4 <= x0) continue;
            const int height = firstHeight + k*growth;
            if (height <= 0) continue;
            const int64_t firstY = sideY + k*topStep;
            if (firstY+height <= y0 || firstY >= int64_t(y1)+bladeReach) continue;
            const int top = int(firstY);
            const int start = std::max(top, view.Top());
            const int end = std::min(top+height, view.Bottom());
            const int progress = k + (side == 2 ? 1 : 0);
            const auto shade = Interpolate(int(v.v),int(corners[1].v),int(corners[3].v),progress,strips,height);
            const auto grass = Interpolate(int(v.u),int(corners[1].u),int(corners[3].u),progress,strips,height);
            visible = true;
            for (int y = std::max(start,y0); y < std::min(end,y1); ++y) {
                const int row = y-top;
                const int g = grass.At(row), s = shade.At(row);
                const int firstLane = ((end-view.Y + y-start)&1)*4;
                for (int px=std::max(x,x0); px<std::min(x+4,x1); ++px)
                    store(px,y,palette.DitherColor(g,s,firstLane+px-x));
            }
            if (!bladeReach) continue;
            const auto range = blades->BandSpan(band);
            const auto& samples = blades->Pool->Bands[band];
            for (size_t i=range.first; i<range.second; ++i) {
                const auto& sample = samples[i];
                const int px = x+sample.Lane;
                if (px < x0 || px >= x1) continue;
                const int row = int(double(height)*sample.FractionY);
                const int root = top+row;
                const int length = std::max(1, sample.Length - int(blades->Shorten));
                const uint32_t color = palette.Color(grass.At(row),std::clamp(shade.At(row)+sample.ShadeDelta,0,127));
                for (int y=std::min(root,y1-1); y>=std::max(root-length+1,y0); --y)
                    store(px,y,color);
            }
        }
        if (stats && visible) ++stats->Triangles;
    }
    if (drawn) fb.HasOriginalTerrain = true;
    if (stats) stats->Pixels += drawn;
}

}}}
