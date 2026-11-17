// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace FSO { namespace Common { namespace TS1 {

// DVD terrain has its own 32 lawn-state by 128 shade ramp. This is not
// the sprite ambient LUT and must not inherit the world's RGB outside tint.
// The two truncating lerps correspond to DVD routines 0049ca4a/0049e846;
// normal lookup and six-plane shading follow 004c496a/004a499b. Transcendental
// library implementations need not reproduce every original x87 slope bit.
struct TerrainRGB { int R, G, B; };
inline TerrainRGB TerrainLerp(TerrainRGB a, TerrainRGB b, float t) {
    return {std::clamp(int(double(a.R) + double(b.R-a.R)*t),0,255),
        std::clamp(int(double(a.G) + double(b.G-a.G)*t),0,255),
        std::clamp(int(double(a.B) + double(b.B-a.B)*t),0,255)};
}
inline uint16_t TerrainRamp565(int grass, int shade) {
    const float g = std::clamp(grass,0,31) / 31.0f;
    const auto lit = TerrainLerp({157,117,65},{80,116,59},g);
    const auto dark = TerrainLerp({81,60,18},{14,53,8},g);
    const auto c = TerrainLerp(lit,dark,shade/128.0f);
    return uint16_t(((c.R>>3)<<11)|((c.G>>2)<<5)|(c.B>>3));
}
inline uint32_t TerrainPresent565(uint16_t c) {
    const unsigned r=(c>>11)&31,g=(c>>5)&63,b=c&31;
    return ((r<<3)|(r>>2)) | (((g<<2)|(g>>4))<<8) |
        (((b<<3)|(b>>2))<<16) | 0xff000000u;
}
// The source subtracts dither offsets after clamping the interpolated shade.
// Negative ramp positions extrapolate; RGB is clamped only after interpolation.
struct TerrainPalette {
    static constexpr int MinShade=-8, Shades=136;
    std::array<uint32_t,32*Shades> Colors{};
    TerrainPalette() {
        for(int g=0;g<32;++g) for(int s=MinShade;s<128;++s)
            Colors[std::size_t(g*Shades+s-MinShade)]=TerrainPresent565(TerrainRamp565(g,s));
    }
    uint32_t DitherColor(int grass,int sourceShade,int lane) const {
        constexpr int offsets[8]={0,8,0,6,6,0,8,0};
        return Color(grass,std::clamp(sourceShade,0,127)-offsets[unsigned(lane)&7]);
    }
    uint32_t Color(int grass,int shade) const {
        return Colors[std::size_t(std::clamp(grass,0,31)*Shades+
            std::clamp(shade,MinShade,127)-MinShade)];
    }
};
inline int TerrainGrassLevel(const std::array<int,4>& signedSamples) {
    const int average=(signedSamples[0]+signedSamples[1]+signedSamples[2]+signedSamples[3])/4;
    return std::clamp((average+128)*32/256,0,31);
}

// Local state only: the original saves/restores its global RNG around this
// renderer calculation. Never use VM randomness to rebuild terrain colors.
struct TerrainRandom {
    uint32_t State = 0x40de9;
    uint32_t Next() {
        const uint64_t value=uint64_t(State)*0x41c64e6d+0x3039;
        State=uint32_t(value);
        return uint32_t(value>>16);
    }
    int Triangular(int n) {
        const int a=int(Next()%unsigned(n)), b=int(Next()%unsigned(n));
        const int value=std::min(a,b);
        return (Next()&1) ? -value : value;
    }
};
using TerrainPoint = std::array<int,2>;
// DVD 00407761/00434ffd use a fixed 64-cell coordinate table. The active
// renderer can be smaller, so its rotated minimum is a separate origin.
inline TerrainPoint RotateTerrainSourcePoint(TerrainPoint point,int rotation) {
    switch(rotation&3) {
        case 1:return {{63-point[1],point[0]}};
        case 2:return {{63-point[0],63-point[1]}};
        case 3:return {{point[1],63-point[0]}};
        default:return point;
    }
}
inline TerrainPoint TerrainCameraOrigin(int width,int rotation) {
    TerrainPoint result{{63,63}};
    for(int x : {0,width-1}) for(int y : {0,width-1}) {
        const auto point=RotateTerrainSourcePoint({{x,y}},rotation);
        result[0]=std::min(result[0],point[0]); result[1]=std::min(result[1],point[1]);
    }
    return result;
}
inline TerrainPoint TerrainWorldTileForCamera(TerrainPoint local,int width,int rotation) {
    const auto origin=TerrainCameraOrigin(width,rotation);
    return RotateTerrainSourcePoint({{local[0]+origin[0],local[1]+origin[1]}},(-rotation)&3);
}
inline TerrainPoint TerrainCameraTileForWorld(TerrainPoint world,int width,int rotation) {
    const auto point=RotateTerrainSourcePoint(world,rotation);
    const auto origin=TerrainCameraOrigin(width,rotation);
    return {{point[0]-origin[0],point[1]-origin[1]}};
}
inline uint8_t TerrainWallFlagsForCamera(uint8_t flags,int rotation) {
    const unsigned r=unsigned(rotation)&3, cardinal=flags&15;
    unsigned result=((cardinal<<r)|(cardinal>>(4-r)))&15;
    const unsigned diagonal=flags&48;
    result|=(r&1) ? ((diagonal&16)<<1)|((diagonal&32)>>1) : diagonal;
    return uint8_t(result);
}
using TerrainCorners = std::array<int,4>; // left, top, right, bottom at rotation zero
// Keep shipped packed-corner differences while the live vertex inputs are
// unchanged. An edited/previewed quad follows the current shared vertices.
inline TerrainCorners BindTerrainHeightQuad(const TerrainCorners& authored,
        const TerrainCorners& baseline,const TerrainCorners& current,bool haveAuthored) {
    return haveAuthored && baseline==current ? authored : current;
}
// The camera-rotated source wall byte is a per-tile adjustment. Shared corner
// samples must remain unchanged for neighboring tiles with different flags.
inline TerrainCorners ApplyTerrainWallShade(TerrainCorners shades,uint8_t cameraWallFlags) {
    if(cameraWallFlags&2) {
        shades[1]=std::min(shades[1]+64,127);
        shades[2]=std::min(shades[2]+64,127);
    }
    return shades;
}
inline TerrainCorners TerrainRotateCorners(const TerrainCorners& canonical,int rotation) {
    TerrainCorners result{};
    for(int i=0;i<4;++i) result[std::size_t(i)]=canonical[std::size_t((i-rotation)&3)];
    return result;
}
struct TerrainNormals {
    std::array<float,121> Values{};
    explicit TerrainNormals(int period) {
        const float pi=3.1415927410125732421875f;
        const float theta=float(double(pi)*(period==0?.1:period==2?.3:.2));
        float lx=float(std::sin(double(theta))),lz=float(std::cos(double(theta)));
        const double norm=std::sqrt(double(lx)*lx+double(lz)*lz);
        lx=float(lx/norm); lz=float(lz/norm);
        const float z=float(2*std::sqrt(6.0));
        for(int a=-5;a<=5;++a) for(int b=-5;b<=5;++b) {
            const float dot=float(double(a)*lx+double(z)*lz);
            const double value=double(dot)/std::sqrt(double(a*a+b*b)+double(z)*z);
            Values[std::size_t((a+5)*11+b+5)]=float(value*value);
        }
    }
    float Plane(const TerrainCorners& h,bool second) const {
        const int a=std::clamp(second ? h[1]-h[2] : h[0]-h[3],-5,5);
        const int b=std::clamp(second ? h[3]-h[2] : h[1]-h[0],-5,5);
        return Values[std::size_t((a+5)*11+b+5)];
    }
    int Vertex(const TerrainCorners& current,const TerrainCorners& left,
               const TerrainCorners& upperLeft,const TerrainCorners& upper,
               TerrainRandom* random=nullptr) const {
        float total=Plane(upper,false);
        total=float(double(total)+Plane(upperLeft,true));
        total=float(double(total)+Plane(upperLeft,false));
        total=float(double(total)+Plane(left,true));
        total=float(double(total)+Plane(current,true));
        total=float(double(total)+Plane(current,false));
        const int shade=127+int(-127.0*double(total)*double(1.0f/6.0f));
        return std::clamp(shade+(random?random->Triangular(3):0),0,127);
    }
};

}}}
