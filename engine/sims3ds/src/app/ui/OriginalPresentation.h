/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/Canvas.h"
#include "odh/raster.hpp"
#include "odh/bitmap.hpp"
#include <algorithm>
#include <cstring>
namespace simapp { namespace original {
namespace detail {
using RasterPixel=std::array<std::uint8_t,4>;
inline void ImportPortable(const std::uint32_t* source,RasterPixel* target,std::size_t count){
    for(std::size_t i=0;i<count;++i){const auto p=source[i];
        target[i]={uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)};}
}
inline void ExportPortable(const RasterPixel* source,std::uint32_t* target,std::size_t count){
    for(std::size_t i=0;i<count;++i){const auto& p=source[i];target[i]=RGBA(p[0],p[1],p[2],p[3]);}
}
inline void Import(const std::uint32_t* source,RasterPixel* target,std::size_t count){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // Canvas R|G<<8|B<<16|A<<24 has exactly the RasterPixel byte layout here.
    // memcpy preserves alpha and avoids aliasing through a different object type.
    static_assert(sizeof(RasterPixel)==sizeof(std::uint32_t),"RGBA pixel size");
    if(count)std::memcpy(target,source,count*sizeof(RasterPixel));
#else
    ImportPortable(source,target,count);
#endif
}
inline void Export(const RasterPixel* source,std::uint32_t* target,std::size_t count){
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if(count)std::memcpy(target,source,count*sizeof(RasterPixel));
#else
    ExportPortable(source,target,count);
#endif
}
}
inline odh::RasterFrame Raster(const Canvas& c) {
    odh::RasterFrame f{}; f.width=c.Width;f.height=c.Height;f.pixels.resize(c.Px.size());
    detail::Import(c.Px.data(),f.pixels.data(),c.Px.size());
    return f;
}
inline void Copy(const odh::RasterFrame& f,Canvas& c){
    detail::Export(f.pixels.data(),c.Px.data(),c.Px.size());
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
    for(int y=r.Y;y<r.Y+r.H;++y){
        const auto i=size_t(y)*c.Width+r.X;
        detail::Import(c.Px.data()+i,f.pixels.data()+i,size_t(r.W));
    }
}
inline void Copy(const odh::RasterFrame& f,Canvas& c,const Rect& region){
    const auto r=Clip(c,region);
    for(int y=r.Y;y<r.Y+r.H;++y){
        const auto i=size_t(y)*c.Width+r.X;
        detail::Export(f.pixels.data()+i,c.Px.data()+i,size_t(r.W));
    }
}
inline odh::BitmapImage Bitmap(const Canvas& c){auto f=Raster(c);return {f.width,f.height,32,true,std::move(f.pixels),0};}
}}
#endif
