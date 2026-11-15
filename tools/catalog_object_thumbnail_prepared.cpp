// SPDX-License-Identifier: MPL-2.0
// Current-provider sprite entry only: no legacy renderer/index implementation.
#include "catalog_object_thumbnail_core.hpp"
#include <limits>
namespace odh_host {
CatalogObjectThumbnailResult CatalogObjectThumbnail::compose_prepared(
        const std::vector<CatalogObjectThumbnailSprite>& sprites,std::uint8_t rotation){
    return compose_prepared(sprites,rotation,3);
}
CatalogObjectThumbnailResult CatalogObjectThumbnail::compose_prepared(
        const std::vector<CatalogObjectThumbnailSprite>& sprites,std::uint8_t rotation,std::uint8_t zoom){
    CatalogObjectThumbnailResult result;
    try {
        if(rotation>3||zoom<1||zoom>3||sprites.size()>4096)throw odh::FormatError("catalog thumbnail prepared input budget exceeded");
        constexpr std::uint32_t side=1024;
        odh::WorldProjection camera;camera.width=camera.height=side;camera.rotation=rotation;camera.zoom=zoom;
        odh::RasterFrame base{side,side,std::vector<std::array<std::uint8_t,4>>(
            side*side,{0,0,0,255}),std::vector<float>(side*side,std::numeric_limits<float>::infinity())};
        std::vector<odh::SpriteLayer> layers;
        std::uint64_t pixels=0;
        for(const auto& item:sprites){
            if(!item.frame||!item.owner_id||item.owner_id>64)throw odh::FormatError("catalog thumbnail unauthenticated layer");
            const auto& frame=*item.frame;
            pixels+=frame.pixels.size();
            if(pixels>4*1048576)throw odh::FormatError("catalog thumbnail prepared pixel budget exceeded");
            const auto pos=odh::place_sprite(camera,item.sprite,frame,item.tile_position);
            if(pos.x<0||pos.y<0||std::int64_t(pos.x)+frame.width>side||std::int64_t(pos.y)+frame.height>side)
                throw odh::FormatError("catalog thumbnail artwork exceeds isolated camera budget");
            layers.push_back({&frame,pos.x,pos.y,pos.back_depth,pos.front_depth_delta,pos.flip,item.owner_id});
        }
        odh::CompositorLimits limits;limits.pixels=4*1048576;
        result=catalog_thumbnail_detail::crop_thumbnail(odh::composite_sprites(base,layers,limits,true,true));
    }catch(const odh::FormatError& error){result.status=CatalogObjectThumbnailStatus::failed;result.detail=error.what();}
    return result;
}
}
