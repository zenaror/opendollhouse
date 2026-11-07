// SPDX-License-Identifier: MPL-2.0
#include "original_queue_view.hpp"
#include "resource.hpp"
#include <algorithm>

namespace odh_host {
namespace {
constexpr auto frame_key="CPanel/Buttons/IconFrame.bmp";
constexpr auto cancel_key="Other/QueueCancel.bmp";
constexpr std::array<std::uint8_t,3> magenta{255,0,255};
}
std::vector<QueueTile> original_queue_layout(const odh::InteractionQueueSnapshot& queue,std::uint32_t width,std::uint32_t height,
        std::optional<std::int16_t> active_priority){
    std::vector<QueueTile> tiles;
    if(width<53||height<53)return tiles;
    const auto columns=(width-8)/45;
    const auto rows=(height-8)/45;
    bool seen_parent=false;
    for(std::size_t n=0;n<queue.actions.size();++n){
        const auto& action=queue.actions[n];
        if(action.mode==odh::InteractionQueueMode::idle||(n&&action.mode==odh::InteractionQueueMode::parent_exit))continue;
        if(action.mode==odh::InteractionQueueMode::parent_idle){
            if(seen_parent)continue;
            seen_parent=true;
        }
        const auto column=tiles.size()%columns,row=tiles.size()/columns;
        if(row>=rows)break;
        const auto priority=queue.active&&queue.active->uid==action.uid?active_priority.value_or(action.priority):action.priority;
        tiles.push_back({{queue.actor_id,queue.actor_generation,queue.run_generation,action.uid},
            {8+static_cast<int>(column)*45,8+static_cast<int>(row)*45,45,45},n,n==0,action.notify_idle&&priority<=0});
    }
    return tiles;
}
const QueueTile* queue_tile_at(const std::vector<QueueTile>& tiles,std::int32_t x,std::int32_t y){
    const auto found=std::find_if(tiles.begin(),tiles.end(),[&](const auto& tile){return tile.box.contains(x,y);});
    return found==tiles.end()?nullptr:&*found;
}
OriginalQueueView::OriginalQueueView(const std::filesystem::path& game_root):art_(game_root,{frame_key,cancel_key}){
    const auto& frame=art_.at(frame_key);const auto& cross=art_.at(cancel_key);
    if(frame.width!=90||frame.height!=45||cross.width!=45||cross.height!=45)
        throw odh::FormatError("original action queue artwork has unexpected dimensions");
}
const odh::BitmapImage* OriginalQueueView::object_icon(const odh::DefinitionIndex& index,std::uint32_t guid){
    const auto previous=std::find_if(icons_.begin(),icons_.end(),[&](const auto& entry){return entry.first==guid;});
    if(previous!=icons_.end())return previous->second?&*previous->second:nullptr;
    // Up to 16 thumbnails at 256x256 pixels each, including negative results.
    if(icons_.size()>=16)icons_.pop_front();
    auto& image=icons_.emplace_back(guid,std::nullopt).second;
    const auto* definition=index.unique(guid);
    if(definition&&definition->definition.master_id&&definition->definition.sub_index!=-1)
        definition=index.unique_master(*definition);
    if(!definition)return nullptr;
    const auto read=[&](const odh::View& view){
        std::optional<std::uint16_t> strings;
        odh::iff_chunks(view,[&](const odh::Chunk& chunk){
            if(chunk.type!="OBJD"||chunk.id!=definition->chunk_id)return;
            const auto identity=odh::read_object_definition(chunk.payload);
            if(strings||identity.guid!=definition->definition.guid)throw odh::FormatError("queue icon definition identity changed");
            const auto words=odh::read_object_definition_words(chunk.payload);
            if(words.size()>41)strings=static_cast<std::uint16_t>(words[41]);
        });
        if(strings)image=read_object_catalog_icon(view,*strings);
    };
    try{
        if(definition->asset.empty())with_iff_file(definition->source,read);
        else with_far_entry(definition->source,definition->asset,read);
    }catch(const odh::FormatError&){return nullptr;}
    return image?&*image:nullptr;
}
void OriginalQueueView::draw_tile(odh::RasterFrame& image,const QueueTile& tile,const odh::BitmapImage* thumbnail) const{
    odh::blit_color_keyed_bitmap(image,art_.at(frame_key),tile.box,odh::UiBitmapRect{tile.active?45:0,0,45,45},magenta);
    if(thumbnail)draw_object_icon(image,tile.box,*thumbnail);
    if(tile.cancelled)odh::blit_color_keyed_bitmap(image,art_.at(cancel_key),tile.box,std::nullopt,magenta);
}
void OriginalQueueView::draw_object_icon(odh::RasterFrame& image,odh::UiBitmapRect box,
        const odh::BitmapImage& thumbnail)const{
        const int width=static_cast<int>(thumbnail.width),height=static_cast<int>(thumbnail.height);
        // Match the existing catalog convention, including its full-width
        // 127/128px pictures. This is a compatibility rule, not strip metadata.
        const int crop=width*10>height*11&&width!=127&&width!=128?width/2:width;
        if(crop>0&&height>0){
            const double scale=std::min({1.0,double(box.width)*37/(45*crop),double(box.height)*37/(45*height)});
            const int w=std::max(1,int(crop*scale)),h=std::max(1,int(height*scale));
            odh::blit_color_keyed_bitmap(image,thumbnail,{box.x+(box.width-w)/2,box.y+(box.height-h)/2,w,h},
                odh::UiBitmapRect{0,0,crop,height},magenta);
        }
}
}
