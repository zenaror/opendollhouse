// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "catalog_object_thumbnail.hpp"
#include <algorithm>
#include <cmath>
namespace odh_host {
namespace catalog_thumbnail_detail {
inline CatalogObjectThumbnailResult crop_thumbnail(const odh::CompositeFrame& composite){
    constexpr std::uint32_t side=1024;
    CatalogObjectThumbnailResult result;
    if(composite.owner_ids.size()!=side*side||composite.sprite_rgba.size()!=side*side)
        throw odh::FormatError("catalog thumbnail ownership/alpha unavailable");
    std::uint32_t left=side,top=side,right=0,bottom=0;
    for(std::uint32_t y=0;y<side;++y)for(std::uint32_t x=0;x<side;++x){
        const auto i=std::size_t(y)*side+x;
        if(composite.owner_ids[i]&&std::isfinite(composite.image.depth[i])){
            left=std::min(left,x);top=std::min(top,y);
            right=std::max(right,x+1);bottom=std::max(bottom,y+1);
        }
    }
    if(left>=right||top>=bottom){result.detail="selected graphics contain no visible pixels";return result;}
    odh::BitmapImage image{right-left,bottom-top,32,true,{},0};
    image.pixels.assign(std::size_t(image.width)*image.height,{0,0,0,0});
    for(std::uint32_t y=top;y<bottom;++y)for(std::uint32_t x=left;x<right;++x){
        const auto i=std::size_t(y)*side+x;
        if(!composite.owner_ids[i]||!std::isfinite(composite.image.depth[i]))continue;
        image.pixels[std::size_t(y-top)*image.width+x-left]=composite.sprite_rgba[i];
    }
    result.image=std::move(image);result.status=CatalogObjectThumbnailStatus::ready;
    return result;
}
}

}
