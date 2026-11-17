// SPDX-License-Identifier: MPL-2.0
#include "common/TS1/TS1TerrainLighting.h"
#include "common/TS1/TS1TerrainBlades.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
using namespace FSO::Common::TS1;
static void CheckAt(bool passed,int line) {
    if (!passed) { std::fprintf(stderr,"terrain lighting conformance failed at line %d\n",line); std::abort(); }
}
#define Check(condition) CheckAt(bool(condition),__LINE__)
static void CheckCameraBinding() {
    // Actual owned DVD camera receipts at renderer width48, together with the
    // fixed64 coordinate-table producer. These origins cannot all be zero.
    const TerrainPoint origins[4]={{{0,0}},{{16,0}},{{16,16}},{{0,16}}};
    const TerrainPoint worlds[4]={{{2,5}},{{5,45}},{{45,42}},{{42,2}}};
    const TerrainCorners orders[4]={{{1,2,3,4}},{{4,1,2,3}},{{3,4,1,2}},{{2,3,4,1}}};
    const uint8_t wallInputs[4]={2,1,8,4};
    const TerrainCorners baseShade{{3,40,90,4}};
    for(int r=0;r<4;++r) {
        Check(TerrainCameraOrigin(48,r)==origins[r]);
        Check(TerrainWorldTileForCamera({{2,5}},48,r)==worlds[r]);
        Check((TerrainCameraTileForWorld(worlds[r],48,r)==TerrainPoint{{2,5}}));
        Check(TerrainRotateCorners({{1,2,3,4}},r)==orders[r]);
        Check((ApplyTerrainWallShade(baseShade,TerrainWallFlagsForCamera(wallInputs[r],r))==TerrainCorners{{3,104,127,4}}));
        // Every camera-local tile uses exactly the same finite RNG/seed domain.
        for(int x=0;x<48;++x) for(int y=0;y<48;++y) {
            const TerrainPoint local{{x,y}};
            const auto world=TerrainWorldTileForCamera(local,48,r);
            Check(world[0]>=0 && world[0]<48 && world[1]>=0 && world[1]<48);
            Check(TerrainCameraTileForWorld(world,48,r)==local);
        }
    }
    Check(TerrainWallFlagsForCamera(16,1)==32);
    Check(TerrainWallFlagsForCamera(32,3)==16);
    Check(TerrainWallFlagsForCamera(25,1)==35);
    // Full source-produced 64-mask rotation table from the independent dossier.
    uint64_t wallHash=14695981039346656037ull;
    for(int r=0;r<4;++r) for(int flags=0;flags<64;++flags)
        wallHash=(wallHash^TerrainWallFlagsForCamera(uint8_t(flags),r))*1099511628211ull;
    Check(wallHash==0x8fba8a03e6022c25ull);
    // Shipped packed corners may disagree with neighboring B1 values. Preserve
    // their source bytes until a live/preview vertex actually changes.
    const TerrainCorners authored{{11,20,23,30}},baseline{{10,20,21,31}},edited{{10,22,21,31}};
    Check(BindTerrainHeightQuad(authored,baseline,baseline,true)==authored);
    Check(BindTerrainHeightQuad(authored,baseline,edited,true)==edited);
    Check(BindTerrainHeightQuad(authored,baseline,baseline,false)==baseline);
    // An available source halo at vertex48 must survive an edited interior;
    // no nearest-live-vertex substitution is valid for these source values.
    const TerrainCorners edge{{20,20,99,100}},edgeEdited{{20,21,99,100}};
    Check(BindTerrainHeightQuad(edge,edge,edge,true)==edge);
    Check(BindTerrainHeightQuad(edge,edge,edgeEdited,true)==edgeEdited);
}

static void CheckCameraShadeFixture() {
    // Noncommercial synthetic heights, evaluated by the separate source oracle.
    // Each orientation consumes the full camera-local48-square RNG sequence.
    const TerrainCorners expected[4]={{{71,76,73,74}},{{75,100,114,33}},
        {{68,77,60,61}},{{70,85,71,66}}};
    const auto vertex=[](int x,int y) {return (3*x+5*y+(x*y)%7)%11;};
    for(int r=0;r<4;++r) {
        const TerrainNormals normals(0);
        TerrainRandom random;
        std::array<int,48*48> grid{};
        const auto H=[&](int x,int y) {
            const auto w=TerrainWorldTileForCamera({{x,y}},48,r);
            return TerrainRotateCorners({{vertex(w[0],w[1]+1),vertex(w[0],w[1]),
                vertex(w[0]+1,w[1]),vertex(w[0]+1,w[1]+1)}},r);
        };
        for(int x=1;x<48;++x) for(int y=1;y<48;++y)
            grid[std::size_t(y)*48+x]=normals.Vertex(H(x,y),H(x-1,y),H(x-1,y-1),H(x,y-1),&random);
        const auto q=TerrainCameraTileForWorld({{2,5}},48,r);
        const TerrainCorners sample{{grid[std::size_t(q[1]+1)*48+q[0]],grid[std::size_t(q[1])*48+q[0]],
            grid[std::size_t(q[1])*48+q[0]+1],grid[std::size_t(q[1]+1)*48+q[0]+1]}};
        Check(ApplyTerrainWallShade(sample,TerrainWallFlagsForCamera(1,r))==expected[r]);
        // The original camera-coordinate G getter gives the same signed four
        // samples as the corresponding physical vertex, in a different order.
        const auto G=[](int x,int y) {return (x*37+y*19)%256-128;};
        const auto cameraG=[&](int x,int y) {const auto w=TerrainWorldTileForCamera({{x,y}},48,r);return G(w[0],w[1]);};
        const int qx[4]={q[0],q[0],q[0]+1,q[0]+1},qy[4]={q[1]+1,q[1],q[1],q[1]+1};
        const TerrainPoint physical[4]={{{2,6}},{{2,5}},{{3,5}},{{3,6}}};
        for(int i=0;i<4;++i) {
            const auto v=physical[(i-r)&3]; const int x=qx[i],y=qy[i];
            Check(TerrainGrassLevel({{cameraG(x,y),cameraG(x-1,y),cameraG(x-1,y-1),cameraG(x,y-1)}})==
                TerrainGrassLevel({{G(v[0],v[1]),G(v[0]-1,v[1]),G(v[0]-1,v[1]-1),G(v[0],v[1]-1)}}));
        }
    }
}

static void CheckBladeVectors() {
    Check(TerrainPoolBlocksBlades(0));
    Check(TerrainPoolBlocksBlades(15));
    Check(TerrainPoolBlocksBlades(16));
    Check(!TerrainPoolBlocksBlades(17));
    Check(!TerrainPoolBlocksBlades(255));
    // Whole generated streams from the independent finite DVD transcription.
    // Each record hashes little-endian band, ordinal, length, lane, f32 Y, shade.
    const uint64_t hashes[3][3]={
        {0xfcaf543250e0e2d7ull,0xbb23db7596b87bdaull,0x9ebfa0ea6255ab79ull},
        {0xd46883bd01b4375bull,0x8d0f6a1f0ff5483cull,0x6f9e8ed266eca75full},
        {0xdc1de4d4f70fe2dbull,0x6a5c7fb86a0df675ull,0x88a02ff5080ebcf6ull}};
    const uint32_t finalState[3][3]={
        {0xa01368f0u,0xa01368f0u,0x7143adecu},
        {0x217f96c4u,0x217f96c4u,0xa7bf9c44u},
        {0xe94c4484u,0xe94c4484u,0xb041acc4u}};
    TerrainBladePool pool;
    for(int detail=0;detail<3;++detail) for(int zoom=1;zoom<=3;++zoom) {
        pool.Reset(zoom,detail);
        std::vector<std::pair<int,const TerrainBladeSample*>> ordered(std::size_t(pool.Budget));
        for(int band=0;band<32;++band) for(const auto& sample:pool.Bands[std::size_t(band)]) {
            Check(sample.Ordinal>=0 && sample.Ordinal<pool.Budget);
            ordered[std::size_t(sample.Ordinal)]={band,&sample};
        }
        uint64_t hash=14695981039346656037ull;
        const auto word=[&](uint32_t value) {
            for(int i=0;i<4;++i) hash=(hash^uint8_t(value>>(8*i)))*1099511628211ull;
        };
        for(const auto& item:ordered) {
            Check(item.second!=nullptr);
            const auto& sample=*item.second;
            uint32_t fraction;
            std::memcpy(&fraction,&sample.FractionY,sizeof(fraction));
            word(uint32_t(item.first)); word(uint32_t(sample.Ordinal));
            word(uint32_t(sample.Length)); word(uint32_t(sample.Lane));
            word(fraction); word(uint32_t(sample.ShadeDelta));
        }
        Check(hash==hashes[detail][zoom-1]);
        Check(pool.FinalRandomState==finalState[detail][zoom-1]);
    }
    const auto seeds=TerrainBladeTileSeeds(48);
    const uint16_t firstSeeds[8]={5521,16605,33174,28690,20704,3225,49131,65394};
    for(int y=0;y<8;++y) Check(seeds[std::size_t(y)*48]==firstSeeds[y]);
    pool.Reset(2,2);
    const auto selected=SelectTerrainBlades(pool,seeds[0],0);
    Check(selected.Count==823 && selected.StartOrdinal==1544 && selected.EndOrdinal==2367);
    std::size_t count=0;
    int first=pool.Budget;
    for(int band=0;band<32;++band) {
        const auto span=selected.BandSpan(band);
        count+=span.second-span.first;
        if(span.first<span.second) first=std::min(first,pool.Bands[std::size_t(band)][span.first].Ordinal);
    }
    Check(count==823 && first==1407);
    TerrainBladePool synthetic;
    for(int ordinal : {0,7,10,15}) synthetic.Bands[0].push_back({ordinal,1,0,0,30});
    const TerrainBladeSelection window{&synthetic,8,12,4,false};
    const auto span=window.BandSpan(0);
    Check(span.first==1 && span.second==2);
    const auto farDry=TerrainBladeDensity(-73,1,1);
    Check(farDry.first==75 && farDry.second);
    Check(SelectTerrainBlades(TerrainBladePool{},0,0).Pool==nullptr);
}
int main() {
    CheckCameraBinding();
    CheckCameraShadeFixture();
    CheckBladeVectors();
    const TerrainCorners wallBase{{3,40,90,4}};
    Check((ApplyTerrainWallShade(wallBase,2)==TerrainCorners{{3,104,127,4}}));
    Check((wallBase==TerrainCorners{{3,40,90,4}}));
    for(uint8_t flags : {uint8_t(0),uint8_t(1),uint8_t(4),uint8_t(8),uint8_t(0x30)})
        Check(ApplyTerrainWallShade(wallBase,flags)==wallBase);
    const TerrainCorners flat{{0,0,0,0}};
    const int shades[4]={13,44,84,44};
    const uint32_t colors[4]={0xff396d6bu,0xff295d5au,0xff184d42u,0xff295d5au};
    for(int p=0;p<4;++p) {
        const TerrainNormals normals(p);
        Check(normals.Vertex(flat,flat,flat,flat)==shades[p]);
        Check(TerrainPresent565(TerrainRamp565(16,shades[p]))==colors[p]);
    }
    const int slopeShades[4]={33,64,95,64};
    for(int p=0;p<4;++p) {
        const TerrainNormals normals(p);
        Check(normals.Vertex({{0,1,2,3}},{{3,4,5,6}},{{1,2,0,1}},{{0,0,1,1}})==slopeShades[p]);
    }
    // DVD 004c4a98 with packed H=0x04010300 selects row7/column8.
    // Distinct cells isolate axis/sign binding from the normal-light formula.
    TerrainNormals indexed(0);
    indexed.Values.fill(-1.0f);
    indexed.Values[85]=0.625f;
    Check(indexed.Plane({{0,3,1,4}},true)==0.625f);
    Check(indexed.Plane({{0,3,1,4}},false)==-1.0f);
    Check(TerrainGrassLevel({{-128,-128,-128,-128}})==0);
    Check(TerrainGrassLevel({{127,127,127,127}})==31);
    Check(TerrainGrassLevel({{0,0,0,0}})==16);
    // Signed division is truncated before quantizing the averaged lawn byte.
    Check(TerrainGrassLevel({{-1,-1,-1,0}})==16);
    TerrainRandom random;
    const uint64_t first=uint64_t(0x40de9)*0x41c64e6d+0x3039;
    Check(random.Next()==uint32_t(first>>16));
    Check(random.State==uint32_t(first));
    const TerrainPalette palette;
    // Independently read from the owned DVD renderer's active g16/s13 ramp.
    const uint32_t capturedLanes[8]={0xff396d6bu,0xff397173u,0xff396d6bu,0xff397173u,
        0xff397173u,0xff396d6bu,0xff397173u,0xff396d6bu};
    for(int lane=0;lane<8;++lane) Check(palette.DitherColor(16,13,lane)==capturedLanes[lane]);
    Check(palette.DitherColor(16,13,0)==colors[0]);
    Check(palette.DitherColor(16,0,1)==TerrainPresent565(TerrainRamp565(16,-8)));
    Check(palette.DitherColor(16,0,1)!=palette.Color(16,0));
    for(int lane=0;lane<4;++lane)
        Check(palette.DitherColor(16,44,lane)==palette.DitherColor(16,44,7-lane));
    Check((TerrainRotateCorners({{1,2,3,4}},1)==TerrainCorners{{4,1,2,3}}));
    Check((TerrainRotateCorners({{1,2,3,4}},2)==TerrainCorners{{3,4,1,2}}));
    // Golden checksum from the independent bounded source transcription;
    // this covers both truncations, all 32 grass rows and extrapolated dithers.
    uint64_t rampHash=14695981039346656037ull;
    for(int g=0;g<32;++g) for(int s=-8;s<128;++s) {
        const uint16_t c=TerrainRamp565(g,s);
        rampHash=(rampHash^uint8_t(c))*1099511628211ull;
        rampHash=(rampHash^uint8_t(c>>8))*1099511628211ull;
    }
    Check(rampHash==0xf3ee5d0beeeb06f6ull);
    // Authored lit endpoints are independently preserved on the packed grid.
    Check(TerrainRamp565(0,0)==uint16_t((19<<11)|(29<<5)|8));
    Check(TerrainRamp565(31,0)==uint16_t((10<<11)|(29<<5)|7));
}
