// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>
#include <cstdint>

namespace FSO { namespace SimAntics { namespace Engine {

// DVD476f33 consumes the numeric rectangle, not registered occupancy. Bounds
// use native x1,y1,x2,y2; the original stores y2,x2,y1,x1. Wall styles are the
// six logical WORDs returned by479737, indexed by their segment bit.
inline bool TS1FootprintCrossesWall(const std::array<int32_t,4>& bounds,
    int tileX, int tileY, uint8_t bits, const std::array<uint16_t,6>& styles) {
    bits&=0x3f;
    if (!bits) return false;
    unsigned bit=1, index=0;
    while (!(bits&bit)) { bit<<=1; ++index; }
    for (;;) {
        const auto style=styles[index];
        if (style!=3 && style!=5 && style!=6 && style!=15 && style!=23) {
            int64_t low=0,high=0,line=0;
            switch (bit) {
            case 1: low=bounds[0];high=bounds[2];line=int64_t(tileX)*16;break;
            case 2: low=bounds[1];high=bounds[3];line=int64_t(tileY)*16;break;
            case 16:
                low=int64_t(bounds[0])+bounds[1];
                high=int64_t(bounds[2])+bounds[3];
                line=(int64_t(tileX)+tileY+1)*16;break;
            case 32:
                low=int64_t(bounds[1])-bounds[2];
                high=int64_t(bounds[3])-bounds[0];
                line=(int64_t(tileY)-tileX)*16;break;
            default: break; // Neighbor-owned bits4/8 have no line test here.
            }
            if ((low<line && high>line) || (low>line && high<line)) return true;
        }
        // Preserve477077..477094: bit32 is tested when it was the initial
        // lowest bit, but is the terminating sentinel after a lower bit.
        do { bit<<=1; ++index; } while (bit<32 && !(bits&bit));
        if (bit>=32) return false;
    }
}

}}}
