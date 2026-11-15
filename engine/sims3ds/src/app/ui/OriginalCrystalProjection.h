/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "odh/projection.hpp"
#include <cmath>
namespace simapp {
struct OriginalCrystalViewport {
    static constexpr int Extent=512;
    int X,Y;
    odh::WorldProjection Camera;
};
// Translate only the viewport. The preserved projection's scale, axes and
// depth basis stay unchanged; the caller binds its actual animated head.
inline OriginalCrystalViewport CrystalViewport(std::array<float,3> head,
        float screenX,float screenY,uint8_t zoom,uint8_t rotation) {
    OriginalCrystalViewport result{int(std::floor(screenX))-256,int(std::floor(screenY))-256,
        {512,512,zoom,rotation,head}};
    result.Camera.center_tile=odh::unproject_tile(result.Camera,
        512.f-(screenX-result.X),512.f-(screenY-result.Y),head[2]);
    return result;
}
}
#endif
