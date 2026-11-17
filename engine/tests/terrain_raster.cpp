// SPDX-License-Identifier: MPL-2.0
#include "world/utils/TriangleRaster.h"
#include "common/TS1/TS1TerrainLighting.h"
#include "common/TS1/TS1TerrainBlades.h"
#include <array>
#include <cstdio>
#include <cstdlib>

using namespace FSO::Common::TS1;
using namespace FSO::LotView;
using namespace FSO::LotView::Utils;

static void Check(bool passed, const char* message) {
    if (!passed) {
        std::fprintf(stderr,"Terrain raster conformance: %s\n",message);
        std::abort();
    }
}

using Corners = std::array<RasterVertex,4>;
static const std::array<int,4> heights{{2,3,0,1}};

static Corners MakeCorners(int zoom, const std::array<int,4>& h, int x=8, int y=100) {
    const int n=2<<zoom;
    const int grass[]={12,4,24,20}, shade[]={60,28,12,44};
    const int xs[]={x,x+4*n,x+8*n,x+4*n}, ys[]={y,y-2*n,y,y+2*n};
    Corners corners{};
    for (int i=0; i<4; ++i) {
        corners[i].x=float(xs[i]); corners[i].y=float(ys[i]-n*h[i]);
        corners[i].z=3; corners[i].u=float(grass[i]); corners[i].v=float(shade[i]);
    }
    return corners;
}

static size_t Pixel(const Framebuffer& fb, int x, int y) { return size_t(y)*fb.Width+x; }

// Distinguish every grass row and signed ramp position, including values that
// quantize to the same RGB565 color. The real palette is exercised separately.
static TerrainPalette IndexedPalette() {
    TerrainPalette p;
    for (int g=0; g<32; ++g) for (int s=p.MinShade; s<128; ++s) {
        p.Colors[size_t(g*p.Shades+s-p.MinShade)]=0xff000000u | (unsigned(g)<<16) | unsigned(s-p.MinShade);
    }
    return p;
}

static uint64_t ColorHash(const Framebuffer& fb) {
    uint64_t hash=14695981039346656037ull;
    for (uint32_t pixel:fb.Color) for (unsigned bit=0; bit<32; bit+=8) {
        hash=(hash ^ uint8_t(pixel>>bit))*1099511628211ull;
    }
    return hash;
}

static void ScanConformance(const TerrainPalette& p) {
    // These whole-frame vectors come from a separate transcription of both
    // original strip generators and their kernel. The right half pre-steps.
    const uint64_t hashes[]={0xb82765fc2208db5dull,0xbcf675a7f5c9a429ull,0x42fb8be333b0c619ull};
    const int counts[]={352,1472,6016};
    for (int z=1; z<=3; ++z) {
        Framebuffer fb(160,155);
        RasterStats stats;
        DrawOriginalTerrainQuad(fb,MakeCorners(z,heights),heights,100,z,p,0,&stats);
        Check(ColorHash(fb)==hashes[z-1],"all four-pixel lanes and both halves at each zoom");
        Check(stats.Pixels==counts[z-1] && stats.Triangles==2,"source coverage");
        Check(fb.HasOriginalTerrain,"floor cache records source terrain");
    }
    auto corners=MakeCorners(1,heights);
    Framebuffer fb(160,155);
    DrawOriginalTerrainQuad(fb,corners,heights,100,1,p,0,nullptr);
    Check(fb.Color[Pixel(fb,36,100)]==p.DitherColor(19,16,4),"rightmost first edge pre-step");
    Check(fb.Color[Pixel(fb,15,97)]==p.DitherColor(13,55,3),"left span last pixel");
    const int descending[]={40,120,80,4};
    for (int i=0;i<4;++i) corners[i].v=float(descending[i]);
    DrawOriginalTerrainQuad(fb,corners,heights,100,1,p,0,nullptr);
    Check(fb.Color[Pixel(fb,20,90)]==p.DitherColor(9,71,4),"negative row step uses signed division toward zero");
}

static void ClipConformance(const TerrainPalette& p) {
    const auto corners=MakeCorners(1,heights);
    Framebuffer whole(160,155), repaint(160,155), clipped(160,155);
    DrawOriginalTerrainQuad(whole,corners,heights,100,1,p,0,nullptr);
    const st::Rectangle rect(10,90,29,4);
    repaint.SetClip(rect.Left(),rect.Top(),rect.Right(),rect.Bottom());
    DrawOriginalTerrainQuad(repaint,corners,heights,100,1,p,0,nullptr);
    DrawOriginalTerrainQuad(clipped,corners,heights,100,1,p,0,nullptr,&rect);
    for (int y=0;y<whole.Height;++y) for (int x=0;x<whole.Width;++x) {
        const auto i=Pixel(whole,x,y);
        Check(repaint.Color[i]==(rect.Contains(x,y)?whole.Color[i]:0u),"dirty scissor preserves every visible lane");
    }
    Check(clipped.Color[Pixel(clipped,13,91)]==p.DitherColor(10,52,5),"source clip recomputes parity");
    Check(repaint.Color[Pixel(repaint,13,91)]==p.DitherColor(10,52,1),"dirty clip preserves full-frame parity");
    Check(clipped.Color[Pixel(clipped,10,92)]==0 && repaint.Color[Pixel(repaint,10,92)]!=0,
          "source clip omits partial horizontal strips");
    // Odd translation of the entire viewport within the overscan buffer must
    // leave the displayed source raster unchanged, including the clipped edges.
    const st::Rectangle movedRect(rect.X+5,rect.Y+3,rect.Width,rect.Height);
    Framebuffer moved(160,155);
    DrawOriginalTerrainQuad(moved,MakeCorners(1,heights,13,103),heights,103,1,p,0,nullptr,&movedRect);
    for (int y=rect.Top();y<rect.Bottom();++y) for (int x=rect.Left();x<rect.Right();++x)
        Check(clipped.Color[Pixel(clipped,x,y)]==moved.Color[Pixel(moved,x+5,y+3)],"translated source-viewport phase");
}

static void OcclusionAndLight() {
    const TerrainPalette p;
    const std::array<int,4> flat{};
    auto corners=MakeCorners(1,flat);
    for (auto& v:corners) { v.u=16; v.v=13; v.z=2+v.x*0.125f+v.y*0.0625f; }
    Framebuffer fb(160,155);
    fb.EnableRoom(true);
    fb.CacheOutsideLight=st::Color(0,0,0,255);
    const st::Color black(0,0,0,255);
    fb.CacheLight=&black; fb.DeferredOutsideTint=true;
    const auto hit=Pixel(fb,8,100), blocked=Pixel(fb,9,100);
    fb.Room[hit]=Framebuffer::OUTSIDE_LIGHT_ROOM; fb.MarkPrelit(hit);
    fb.Color[blocked]=0x44332211; fb.Depth[blocked]=-5; fb.Room[blocked]=42;
    DrawOriginalTerrainQuad(fb,corners,flat,100,1,p,0.25f,nullptr);
    Check(fb.Color[hit]==0xff396d6bu && fb.Room[hit]==65535 && !fb.IsPrelit(hit),"opaque source RGB565 passes through deferred light");
    Check(fb.Depth[hit]==2.25f+8.5f*0.125f+100.5f*0.0625f,"native depth plane with pixel-center bias");
    Check(fb.Color[blocked]==0x44332211 && fb.Room[blocked]==42 && fb.Depth[blocked]==-5,"nearer existing scene surface survives");
    fb.MarkPrelit(hit); fb.ResolvePrelit=true; fb.Color[hit]=0;
    DrawOriginalTerrainQuad(fb,corners,flat,100,1,p,0.25f,nullptr);
    Check(fb.Color[hit]==0xff396d6bu && fb.IsPrelit(hit),"replay mask stays intact and RGB remains fully lit");
    Framebuffer gated(160,155);
    const std::array<int,4> steep{{0,0,0,6}};
    DrawOriginalTerrainQuad(gated,MakeCorners(1,steep),steep,100,1,p,0,nullptr);
    Check(!gated.HasOriginalTerrain,"source skips back-facing steep tile");
    DrawOriginalTerrainQuad(gated,corners,flat,100,0,p,0,nullptr);
    Check(!gated.HasOriginalTerrain,"invalid zoom has no source raster");
}

static void BladeStrokes(const TerrainPalette& p) {
    const std::array<int,4> flat{};
    auto corners=MakeCorners(3,flat);
    for (auto& v:corners) { v.u=16; v.v=110; }
    TerrainBladePool pool;
    const TerrainBladeSample sample{0,4,2,0.0f,60};
    // The source predecessor interval excludes the sentinel record.
    for (int band : {0,31}) pool.Bands[band]={sample,{10,4,0,0.0f,0}};
    TerrainBladeSelection selected{&pool,0,11,1,false};
    Framebuffer fb(160,155);
    DrawOriginalTerrainQuad(fb,corners,flat,100,3,p,0,nullptr,nullptr,&selected);
    const uint32_t blade=p.Color(16,127);
    for (int x : {10,134}) {
        for (int y=97;y<=100;++y) Check(fb.Color[Pixel(fb,x,y)]==blade,"both outer blade bands grow upward after ground with clamped shade");
        Check(fb.Color[Pixel(fb,x,96)]==0,"blade length");
    }
    selected.Shorten=true;
    const st::Rectangle crop(0,0,160,100); // Root is below the view, upper part remains visible.
    Framebuffer clipped(160,155);
    clipped.SetClip(9,97,11,100);
    DrawOriginalTerrainQuad(clipped,corners,flat,100,3,p,0,nullptr,&crop,&selected);
    Check(clipped.Color[Pixel(clipped,10,97)]==0 && clipped.Color[Pixel(clipped,10,98)]==blade &&
          clipped.Color[Pixel(clipped,10,99)]==blade && clipped.Color[Pixel(clipped,10,100)]==0,
          "shortened strokes clip independently of their ground row");
}

int main() {
    const auto p=IndexedPalette();
    ScanConformance(p); ClipConformance(p); OcclusionAndLight(); BladeStrokes(p);
}
