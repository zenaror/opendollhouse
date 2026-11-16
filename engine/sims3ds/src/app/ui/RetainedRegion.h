// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "app/Canvas.h"
#include <algorithm>
#include <cstring>

namespace simapp { namespace original {
// For small UI components whose exact drawing bounds and visual inputs are
// known. Compare the underlying scene so antialiasing also stays exact.
class RetainedRegion {
    Rect area{}, priorBounds{};
    std::vector<std::uint32_t> source, result;
public:
    template<class Paint>
    bool draw(Canvas& c, Rect bounds, bool sameContent, Paint paint) {
        const int x=std::clamp(bounds.X,0,c.Width),y=std::clamp(bounds.Y,0,c.Height);
        const int width=std::clamp(bounds.Right(),x,c.Width)-x;
        const int height=std::clamp(bounds.Bottom(),y,c.Height)-y;
        if(!width||!height){area={};return false;}
        // Unexpected or modded text can always use the direct renderer.
        if(size_t(width)*height>8192){area={};paint();return false;}
        auto row=[&](int n){return c.Px.data()+size_t(y+n)*c.Width+x;};
        const size_t bytes=size_t(width)*sizeof(std::uint32_t);
        bool hit=sameContent && bounds.X==priorBounds.X && bounds.Y==priorBounds.Y &&
            bounds.W==priorBounds.W && bounds.H==priorBounds.H && area.X==x && area.Y==y && area.W==width && area.H==height;
        for(int n=0;hit && n<height;++n)
            hit=std::memcmp(row(n),source.data()+size_t(n)*width,bytes)==0;
        if(hit) {
            for(int n=0;n<height;++n)
                std::memcpy(row(n),result.data()+size_t(n)*width,bytes);
            return true;
        }
        area={x,y,width,height};priorBounds=bounds;
        source.resize(size_t(width)*height);
        result.resize(source.size());
        for(int n=0;n<height;++n)
            std::memcpy(source.data()+size_t(n)*width,row(n),bytes);
        paint();
        for(int n=0;n<height;++n)
            std::memcpy(result.data()+size_t(n)*width,row(n),bytes);
        return false;
    }
};
}}
