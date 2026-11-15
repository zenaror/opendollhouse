/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "app/Canvas.h"
#include "odh/raster.hpp"
#include "odh/bitmap.hpp"
namespace simapp { namespace original {
inline odh::RasterFrame Raster(const Canvas& c) {
    odh::RasterFrame f{}; f.width=c.Width;f.height=c.Height;f.pixels.reserve(c.Px.size());
    for(auto p:c.Px)f.pixels.push_back({uint8_t(p),uint8_t(p>>8),uint8_t(p>>16),uint8_t(p>>24)});
    return f;
}
inline void Copy(const odh::RasterFrame& f,Canvas& c){
    for(size_t i=0;i<c.Px.size();++i){const auto& p=f.pixels[i];c.Px[i]=RGBA(p[0],p[1],p[2],p[3]);}
}
inline odh::BitmapImage Bitmap(const Canvas& c){auto f=Raster(c);return {f.width,f.height,32,true,std::move(f.pixels),0};}
}}
#endif
