/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "world/Framebuffer.h"
#include <cmath>

namespace FSO { namespace LotView { namespace Utils {
// Ground-stage presentation: later floor/object passes cover these dots using
// their normal depth. Scroll/zoom/rotation/altitude come from the world vertices.
inline void DrawBuildGridEdge(Framebuffer& target,float ax,float ay,float az,float bx,float by,float bz) {
    const int steps=std::max(1,int(std::ceil(std::max(std::abs(bx-ax),std::abs(by-ay)))));
    for(int i=0;i<=steps;++i) {
        if(i&1)continue;
        const float t=float(i)/steps;
        const int x=int(std::lround(ax+(bx-ax)*t)),y=int(std::lround(ay+(by-ay)*t));
        if(x<0 || y<0 || x>=target.Width || y>=target.Height || x<target.ClipX0 ||
           y<target.ClipY0 || x>=target.ClipX1 || y>=target.ClipY1)continue;
        const size_t p=size_t(y)*target.Width+x;
        if(az+(bz-az)*t<=target.Depth[p]+.1f)target.Color[p]=Framebuffer::Pack(0,0,0,255);
    }
}
}}}
