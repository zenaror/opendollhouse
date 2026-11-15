/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/Canvas.h"
#include "odh/raster.hpp"
#include <algorithm>
#include <limits>

namespace simapp {
// The full raster's opaque clear pixels have depth +infinity. The existing
// <= depth comparison therefore also paints them where the scene is clear.
inline void ComposeCrystal(Canvas& canvas,const std::vector<float>& depth,const Rect& viewport,
        const odh::RasterFrame& layer,const std::array<uint32_t,2>& origin,float bias){
    const int left=std::max(0,viewport.X),top=std::max(0,viewport.Y);
    const int right=std::min(canvas.Width,viewport.Right()),bottom=std::min(canvas.Height,viewport.Bottom());
    if(left>=right || top>=bottom)return;
    const auto clear=odh::RasterOptions{}.clear;
    const auto clearPixel=RGBA(clear[0],clear[1],clear[2],clear[3]);
    const float clearDepth=std::numeric_limits<float>::infinity()+bias;
    for(int y=top;y<bottom;++y){
        const auto row=size_t(y)*canvas.Width;
        for(int x=left;x<right;++x)
            if(clearDepth<=depth[row+x])canvas.Px[row+x]=clearPixel;
    }
    // All shaded crystal samples are opaque. A finite fragment that passes
    // the original depth test replaces the cleared or unchanged scene pixel.
    const int ox=viewport.X+int(origin[0]),oy=viewport.Y+int(origin[1]);
    const int endX=std::min(right,ox+int(layer.width)),endY=std::min(bottom,oy+int(layer.height));
    for(int y=std::max(top,oy);y<endY;++y)for(int x=std::max(left,ox);x<endX;++x){
        const auto local=size_t(y-oy)*layer.width+size_t(x-ox),target=size_t(y)*canvas.Width+x;
        if(layer.depth[local]+bias<=depth[target]){
            const auto& p=layer.pixels[local];
            canvas.Px[target]=RGBA(p[0],p[1],p[2],p[3]);
        }
    }
}
}
#endif
