/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/Canvas.h"
#include "odh/raster.hpp"
#include "odh/bitmap.hpp"
#include <algorithm>
namespace simapp { namespace original {
inline odh::RasterFrame Raster(const Canvas& c) {
    odh::RasterFrame f{}; f.width=c.Width;f.height=c.Height;f.pixels.reserve(c.Px.size());
    for(auto p:c.Px)f.pixels.push_back({uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)});
    return f;
}
inline void Copy(const odh::RasterFrame& f,Canvas& c){
    for(size_t i=0;i<c.Px.size();++i){const auto& p=f.pixels[i];c.Px[i]=RGBA(p[0],p[1],p[2],p[3]);}
}
inline Rect Clip(const Canvas& c,const Rect& region){
    const auto x=std::clamp(region.X,0,c.Width),y=std::clamp(region.Y,0,c.Height);
    const auto right=std::clamp<std::int64_t>(std::int64_t(region.X)+std::max(0,region.W),x,c.Width);
    const auto bottom=std::clamp<std::int64_t>(std::int64_t(region.Y)+std::max(0,region.H),y,c.Height);
    return {x,y,int(right-x),int(bottom-y)};
}
// Keep full-display coordinates while synchronizing only the pixels touched by
// this draw. The scratch frame can retain its allocation between UI frames.
inline void Raster(const Canvas& c,odh::RasterFrame& f,const Rect& region){
    f.width=static_cast<std::uint32_t>(c.Width);f.height=static_cast<std::uint32_t>(c.Height);
    f.pixels.resize(c.Px.size());
    const auto r=Clip(c,region);
    for(int y=r.Y;y<r.Y+r.H;++y)for(int x=r.X;x<r.X+r.W;++x){
        const auto i=size_t(y)*c.Width+x;const auto p=c.Px[i];
        f.pixels[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};
    }
}
inline void Copy(const odh::RasterFrame& f,Canvas& c,const Rect& region){
    const auto r=Clip(c,region);
    for(int y=r.Y;y<r.Y+r.H;++y)for(int x=r.X;x<r.X+r.W;++x){
        const auto i=size_t(y)*c.Width+x;const auto& p=f.pixels[i];
        c.Px[i]=RGBA(p[0],p[1],p[2],p[3]);
    }
}
inline odh::BitmapImage Bitmap(const Canvas& c){auto f=Raster(c);return {f.width,f.height,32,true,std::move(f.pixels),0};}
}}
#endif
