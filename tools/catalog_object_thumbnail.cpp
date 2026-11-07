// SPDX-License-Identifier: MPL-2.0
#include "catalog_object_thumbnail.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace odh_host {
CatalogObjectThumbnail::CatalogObjectThumbnail(const odh::DefinitionIndex& definitions)
    :definitions_(definitions),renderer_(definitions){}
void CatalogObjectThumbnail::clear(){result_={};}
const CatalogObjectThumbnailResult& CatalogObjectThumbnail::select(
        const std::vector<SavedObjectVisual>& parts,std::uint8_t rotation){
    clear(); // Never display a prior selection after an unsuccessful request.
    try {
        if(parts.size()>64)throw odh::FormatError("catalog thumbnail part budget exceeded");
        if(rotation>3)throw odh::FormatError("catalog thumbnail camera rotation invalid");
        if(parts.empty()){result_.detail="no authenticated visual parts";return result_;}
        // Validate before copying; input IDs are intentionally not presentation
        // identities. Only bounded local tokens enter this isolated compositor.
        std::size_t flags=0;
        for(const auto& part:parts){
            if(part.direction>7)throw odh::FormatError("catalog thumbnail direction invalid");
            for(float value:part.tile_position)if(!std::isfinite(value))
                throw odh::FormatError("catalog thumbnail position nonfinite");
            for(float value:part.linear_light)if(!std::isfinite(value)||value<0||value>1)
                throw odh::FormatError("catalog thumbnail light invalid");
            if(part.dynamic_sprite_flags.size()>65536-flags)
                throw odh::FormatError("catalog thumbnail dynamic flag budget exceeded");
            flags+=part.dynamic_sprite_flags.size();
        }
        for(const auto& part:parts)if(!definitions_.unique(part.guid)){
            result_.status=CatalogObjectThumbnailStatus::unknown_definition;
            result_.detail="object definition absent or ambiguous";return result_;
        }
        auto visuals=parts;
        for(std::size_t n=0;n<visuals.size();++n){
            visuals[n].id=static_cast<std::int16_t>(n+1);
            for(unsigned axis=0;axis<3;++axis){
                const double relative=double(parts[n].tile_position[axis])-parts.front().tile_position[axis];
                if(!std::isfinite(relative)||std::abs(relative)>std::numeric_limits<float>::max())
                    throw odh::FormatError("catalog thumbnail relative position overflow");
                visuals[n].tile_position[axis]=static_cast<float>(relative);
            }
        }
        constexpr std::uint32_t side=1024;
        odh::WorldProjection camera;camera.width=camera.height=side;camera.rotation=rotation;
        odh::RasterFrame base{side,side,std::vector<std::array<std::uint8_t,4>>(
            side*side,{0,0,0,255}),std::vector<float>(side*side,std::numeric_limits<float>::infinity())};
        auto rendered=renderer_.render(visuals,camera,base,true,true);
        if(rendered.offscreen_layers||rendered.clipped_layers)
            throw odh::FormatError("catalog thumbnail artwork exceeds isolated camera budget");
        const auto& composite=rendered.composite;
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
        if(left>=right||top>=bottom){result_.detail="selected graphics contain no visible pixels";return result_;}
        odh::BitmapImage image{right-left,bottom-top,32,true,{},0};
        image.pixels.assign(std::size_t(image.width)*image.height,{0,0,0,0});
        for(std::uint32_t y=top;y<bottom;++y)for(std::uint32_t x=left;x<right;++x){
            const auto i=std::size_t(y)*side+x;
            if(!composite.owner_ids[i]||!std::isfinite(composite.image.depth[i]))continue;
            image.pixels[std::size_t(y-top)*image.width+x-left]=composite.sprite_rgba[i];
        }
        result_.image=std::move(image);result_.status=CatalogObjectThumbnailStatus::ready;
    }catch(const odh::FormatError& error){
        result_.status=CatalogObjectThumbnailStatus::failed;result_.detail=error.what();
    }
    return result_;
}
}
