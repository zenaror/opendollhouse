// SPDX-License-Identifier: MPL-2.0
#include "character_ui_images.hpp"
#include <algorithm>
#include <sstream>

namespace odh_host {
std::size_t character_relationship_frame(std::optional<std::int16_t> value) noexcept {
    return static_cast<std::size_t>(std::min(4,(std::clamp<int>(value.value_or(0),-100,100)+100)/40));
}
namespace {
void append_image(CharacterUiImages& out,std::array<bool,5>& seen,std::uint64_t& consumed,
                  std::uint16_t id,const odh::View& payload,const CharacterUiImageLimits& limits){
    if(id<2002||id>2006)return;
        const auto index=id-2002;
        if(seen[index])throw odh::FormatError("duplicate character UI image resource");
        seen[index]=true;
        if(payload.size()>limits.bitmap_bytes)
            throw odh::FormatError("character UI bitmap exceeds payload limit");
        const std::uint32_t frames=id==2002?3:id==2003?5:1;
        odh::BitmapLimits bitmap_limits;
        bitmap_limits.width=frames*limits.cell_extent;bitmap_limits.height=limits.cell_extent;
        bitmap_limits.pixels=limits.pixels;bitmap_limits.rle_bytes=static_cast<std::uint32_t>(
            std::min<std::uint64_t>(limits.bitmap_bytes,UINT32_MAX));
        auto image=odh::read_bmp(payload,bitmap_limits,odh::BmpExtentPolicy::iff_container);
        if(!image.width||image.width%frames||!image.height||image.width/frames>limits.cell_extent||
           image.pixels.size()>limits.pixels-consumed)
            throw odh::FormatError("character UI image dimensions or cumulative pixel limit invalid");
        consumed+=image.pixels.size();
        if(frames==1){
            auto& destination=id==2004?out.thumbnail:id==2005?out.speech_medium:out.speech_large;
            destination=std::move(image);return;
        }
        auto& destination=id==2002?out.faces:out.relations;
        const auto width=image.width/frames;
        for(std::uint32_t frame=0;frame<frames;++frame){
            odh::BitmapImage cell{width,image.height,image.bit_count,image.top_down,{},0};
            cell.pixels.reserve(static_cast<std::size_t>(width)*image.height);
            for(std::uint32_t y=0;y<image.height;++y){
                const auto begin=image.pixels.begin()+static_cast<std::size_t>(y)*image.width+frame*width;
                cell.pixels.insert(cell.pixels.end(),begin,begin+width);
            }
            destination.push_back(std::move(cell));
        }
}
}
CharacterUiImages read_character_ui_images(const odh::View& view,CharacterUiImageLimits limits) {
    if(view.size()>limits.iff_bytes||!limits.cell_extent||limits.cell_extent>128)
        throw odh::FormatError("character UI images exceed IFF or cell limits");
    CharacterUiImages out;std::array<bool,5> seen{};std::uint64_t consumed=0;
    odh::Limits iff_limits;iff_limits.iff_chunks=limits.chunks;
    odh::iff_chunks(view,[&](const odh::Chunk& chunk){if(chunk.type=="BMP_")append_image(out,seen,consumed,chunk.id,chunk.payload,limits);},iff_limits);
    return out;
}
CharacterUiImages read_character_ui_images(const std::vector<CharacterUiImageResource>& resources,CharacterUiImageLimits limits){
    if(resources.size()>limits.chunks||!limits.cell_extent||limits.cell_extent>128)
        throw odh::FormatError("character UI resources exceed chunk or cell limits");
    CharacterUiImages out;std::array<bool,5> seen{};std::uint64_t consumed=0,bytes=0;
    for(const auto& resource:resources){
        if(resource.bitmap.size()>limits.iff_bytes-bytes)throw odh::FormatError("character UI resource bytes exceed limit");bytes+=resource.bitmap.size();
        std::string data(reinterpret_cast<const char*>(resource.bitmap.data()),resource.bitmap.size());std::istringstream stream(data,std::ios::binary);
        append_image(out,seen,consumed,resource.id,odh::View(stream,data.size()),limits);
    }
    return out;
}
}
