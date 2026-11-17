// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "common/TS1/TS1TerrainLighting.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace FSO { namespace Common { namespace TS1 {

// The original pool admission includes dry rim 16 as well as water 0..15.
// This is deliberately distinct from the strict water classification.
inline bool TerrainPoolBlocksBlades(uint8_t sourceByte) { return sourceByte<=16; }

struct TerrainBladeSample {
    int Ordinal=0, Length=1, Lane=0;
    float FractionY=0;
    int ShadeDelta=0;
};
struct TerrainBladePool {
    std::array<std::vector<TerrainBladeSample>,32> Bands;
    int Detail=-1, Zoom=-1, Budget=0;
    uint32_t FinalRandomState=0;
    void Reset(int zoom,int detail) {
        for(auto& band:Bands) band.clear();
        Zoom=zoom; Detail=detail; Budget=0;
        if(zoom<1 || zoom>3 || detail<0 || detail>2) return;
        constexpr int maximum[3]={0,800,1200};
        Budget=std::max(4*maximum[detail],500);
        const int halfBands=(8<<zoom)/4, halfArea=8*halfBands*halfBands;
        TerrainRandom random; random.State=0x4aa47b44;
        for(int ordinal=0;ordinal<Budget;++ordinal) {
            const unsigned area=random.Next()%unsigned(2*halfArea);
            int residue=int(area%unsigned(halfArea)), band=0, weight=8;
            while(residue>0) {
                if(residue<weight) break;
                residue-=weight; ++band; weight+=16;
            }
            if(area>=unsigned(halfArea)) band=2*halfBands-band-1;
            TerrainBladeSample sample;
            sample.Ordinal=ordinal;
            sample.Lane=int(random.Next()&3);
            sample.FractionY=float(double(random.Next()%4001)*(1.0/4001.0));
            int delta=int(random.Next()%31);
            if(random.Next()&1) delta=-delta;
            sample.ShadeDelta=30+delta;
            sample.Length=zoom==3?4:1;
            if(sample.Length>1) sample.Length-=int(random.Next()%unsigned(sample.Length/2));
            Bands[std::size_t(band)].push_back(sample);
        }
        FinalRandomState=random.State;
    }
};
inline std::pair<int,bool> TerrainBladeDensity(int signedGrass,int detail,int zoom) {
    if(detail<0 || detail>2 || zoom<1 || zoom>3) return {0,false};
    constexpr int minimum[3]={0,300,500}, maximum[3]={0,800,1200};
    int count;
    const bool shorten=signedGrass<=-73;
    if(signedGrass>=85) count=maximum[detail];
    else if(shorten) count=minimum[detail];
    else count=minimum[detail]+(maximum[detail]-minimum[detail])*(signedGrass+73)/158;
    return {zoom==1?count/4:count,shorten};
}
// Literal source predecessor search; a lower_bound changes the visible pool.
inline std::size_t TerrainBladePredecessor(const std::vector<TerrainBladeSample>& samples,
                                         int key,std::size_t first=0) {
    std::size_t index=first,step=samples.size()/2;
    while(step) {
        const std::size_t candidate=index+step;
        if(candidate<samples.size() && samples[candidate].Ordinal<key) index=candidate;
        else step/=2;
    }
    return index;
}
struct TerrainBladeSelection {
    const TerrainBladePool* Pool=nullptr;
    int StartOrdinal=0, EndOrdinal=0, Count=0;
    bool Shorten=false;
    std::pair<std::size_t,std::size_t> BandSpan(int band) const {
        if(!Pool || Count<=0 || band<0 || band>=32) return {0,0};
        const auto& samples=Pool->Bands[std::size_t(band)];
        if(samples.empty()) return {0,0};
        const auto first=TerrainBladePredecessor(samples,StartOrdinal);
        return {first,TerrainBladePredecessor(samples,EndOrdinal,first)};
    }
};
inline TerrainBladeSelection SelectTerrainBlades(const TerrainBladePool& pool,
                                               uint16_t tileSeed,int signedGrass) {
    if(pool.Budget<=0) return {};
    const auto density=TerrainBladeDensity(signedGrass,pool.Detail,pool.Zoom);
    if(density.first<=0 || density.first>=pool.Budget) return {};
    const int start=int(tileSeed)% (pool.Budget-density.first);
    return {&pool,start,start+density.first,density.first,density.second};
}
inline std::vector<uint16_t> TerrainBladeTileSeeds(int width) {
    std::vector<uint16_t> result(std::size_t(width)*width);
    TerrainRandom random; random.State=0x4aa47b44;
    for(int x=0;x<width;++x) for(int y=0;y<width;++y)
        result[std::size_t(y)*width+x]=uint16_t(random.Next()%65535);
    return result;
}

}}}
