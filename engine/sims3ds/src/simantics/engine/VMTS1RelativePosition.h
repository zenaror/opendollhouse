// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/_files_contract.h"
#include "core/math.h"
#include <cmath>
#include <vector>
#include <stdexcept>

namespace FSO { namespace SimAntics { namespace Engine {
using Files::Formats::IFF::Chunks::SLOTItem;

inline bool ValidTS1RelativeSlot(const SLOTItem& s) {
    const auto flags = uint32_t(s.Rsflags), regions = flags & 255;
    const bool known = regions == 0 || regions == 1 || regions == 5 || regions == 4 ||
        regions == 20 || regions == 16 || regions == 80 || regions == 64 || regions == 65 || regions == 85;
    const auto face = int(s.Facing);
    return known && (flags & ~UINT32_C(0xc055)) == 0 && s.Type == 3 &&
        s.Offset == st::Vector3() && s.Standing == 1 && s.Sitting == 0 && s.Ground == 0 &&
        s.SnapTargetSlot == -1 && s.MinProximity == 16 && s.OptimalProximity == 16 &&
        s.MaxProximity == (regions == 85 ? 32 : 16) && s.Resolution == 16 &&
        s.Gradient == 3.0f / 16.0f && s.Height == 0 &&
        (face == -3 || face == -2 || (face >= 0 && face < 8));
}

inline uint8_t TS1RotateMask(uint32_t value, unsigned heading) {
    heading &= 7;
    const auto shifted = value << ((8 - heading) & 7);
    return uint8_t((shifted & 255) | ((shifted >> 8) & 255));
}

inline uint8_t TS1ObjectWallMask(int16_t value, unsigned heading) {
    const uint32_t shifted = uint32_t(int32_t(value)) << (heading & 31);
    return uint8_t((shifted & 255) | ((shifted >> 8) & 255));
}

inline st::Point TS1RotateRelativePoint(int x, int y, unsigned heading) {
    switch (heading & 7) {
    case 0: return {x,y};
    case 2: return {-y,x};
    case 4: return {-x,-y};
    case 6: return {y,-x};
    default: {
        // OP27's fixed 16-unit lattice and 32-unit maximum have a closed
        // numeric domain. These are the PC float-matrix/coordinate-conversion
        // results for that domain, avoiding host-dependent trig or x87 modes.
        const auto diagonal=[](int value) {
            const int v=std::abs(value),sign=value<0?-1:1;
            if (!v) return 0;
            if (v==16) return sign*11;
            if (v==32) return sign*23;
            throw std::logic_error("Relative-position offset outside the original lattice");
        };
        const int rx=diagonal(x-y),ry=diagonal(x+y);
        return TS1RotateRelativePoint(rx,ry,heading-1);
    }
    }
}

struct TS1RelativeCandidate {
    st::Point Position;
    uint8_t Region = 0;
    uint8_t Heading = 0;
    int Score = 0;
};

inline std::vector<TS1RelativeCandidate> TS1RelativeCandidates(
        const SLOTItem& slot, st::Point center, unsigned heading, uint8_t wallMask,
        st::Point preferred = st::Point(0,0)) {
    std::vector<TS1RelativeCandidate> result;
    if (!ValidTS1RelativeSlot(slot)) return result;
    const unsigned mask = unsigned(slot.Rsflags) & 255;
    if (mask == 0) { result.push_back({center,0,uint8_t(heading&7),100}); return result; }
    unsigned nearWall = 0;
    for (unsigned k=0;k<8;++k) if (wallMask & (1u<<k))
        nearWall |= (1u<<k) | (1u<<((k+7)&7)) | (1u<<((k+1)&7));
    result.reserve(12);
    for (unsigned k=0;k<8;++k) {
        const unsigned region = 1u<<k;
        if (!(mask&region) || (wallMask&region)) continue;
        for (int radial=-slot.MinProximity/16;radial>=-slot.MaxProximity/16;--radial) {
            const int end = -radial - int(bool(mask & (1u<<((k+2)&7))));
            for (int tangent=radial;tangent<=end;++tangent) {
                const int x=tangent*16,y=radial*16,r2=x*x+y*y;
                if (r2<slot.MinProximity*slot.MinProximity || r2>slot.MaxProximity*slot.MaxProximity) continue;
                const auto offset=TS1RotateRelativePoint(x,y,(heading+k)&7);
                const st::Point position(center.X+offset.X,center.Y+offset.Y);
                const int penalty=int(std::abs(int(std::sqrt(double(r2)))-slot.OptimalProximity)*slot.Gradient);
                const int score=110-penalty+((nearWall&region)?0:25)+(position==preferred?20:0);
                result.push_back({position,uint8_t(region),uint8_t((heading+k)&7),score});
            }
        }
    }
    return result;
}
}}}
