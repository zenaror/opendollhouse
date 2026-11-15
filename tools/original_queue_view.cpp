// SPDX-License-Identifier: MPL-2.0
#include "original_queue_view.hpp"
#include "resource.hpp"
#include <algorithm>

namespace odh_host {
namespace {
constexpr auto frame_key="CPanel/Buttons/IconFrame.bmp";
constexpr auto cancel_key="Other/QueueCancel.bmp";
constexpr auto object_template_key="CPanel/Catalog/ThumbTemplate.bmp";
constexpr auto avatar_template_key="CPanel/People/PeopleTemplate.bmp";
constexpr std::array<std::uint8_t,3> blue{0,0,255};
constexpr std::array<std::uint8_t,3> magenta{255,0,255};
}
std::vector<QueueTile> original_queue_layout(const odh::InteractionQueueSnapshot& queue,std::uint32_t width,std::uint32_t height,
        std::optional<std::int16_t> active_priority){
    std::vector<QueueTile> tiles;
    // Original Complete observation: 32px inset, 45px frame, 50px pitch.
    // Both pinned reference queues also use fixed pitch and one horizontal row.
    if(!width||!height)return tiles;
    bool seen_parent=false;
    for(std::size_t n=0;n<queue.actions.size();++n){
        const auto& action=queue.actions[n];
        if(action.mode==odh::InteractionQueueMode::idle||(n&&action.mode==odh::InteractionQueueMode::parent_exit))continue;
        if(action.mode==odh::InteractionQueueMode::parent_idle){
            if(seen_parent)continue;
            seen_parent=true;
        }
        const auto column=tiles.size();
        const auto priority=queue.active&&queue.active->uid==action.uid?active_priority.value_or(action.priority):action.priority;
        tiles.push_back({{queue.actor_id,queue.actor_generation,queue.run_generation,action.uid},
            {32+static_cast<int>(column)*50,32,45,45},n,n==0,action.notify_idle&&priority<=0});
    }
    return tiles;
}
odh::BitmapImage read_queue_portrait(const odh::View& payload){
    odh::BitmapLimits limits;limits.width=384;limits.height=128;limits.pixels=384*128;
    auto strip=odh::read_bmp(payload,limits,odh::BmpExtentPolicy::iff_container);
    if(strip.width%3)throw odh::FormatError("queue BMP2002 must contain three equal frames");
    const auto width=strip.width/3;
    odh::BitmapImage cell{width,strip.height,strip.bit_count,strip.top_down,{},0};
    cell.pixels.reserve(std::size_t(width)*strip.height);
    for(std::uint32_t y=0;y<strip.height;++y){
        const auto begin=strip.pixels.begin()+std::size_t(y)*strip.width;
        cell.pixels.insert(cell.pixels.end(),begin,begin+width);
    }
    return cell;
}
const QueueTile* queue_tile_at(const std::vector<QueueTile>& tiles,std::int32_t x,std::int32_t y){
    const auto found=std::find_if(tiles.begin(),tiles.end(),[&](const auto& tile){return tile.box.contains(x,y);});
    return found==tiles.end()?nullptr:&*found;
}
OriginalQueueView::OriginalQueueView(const std::filesystem::path& game_root):art_(game_root,{frame_key,cancel_key,object_template_key,avatar_template_key}){
    const auto& frame=art_.at(frame_key);const auto& cross=art_.at(cancel_key);
    const auto& objects=art_.at(object_template_key);const auto& people=art_.at(avatar_template_key);
    if(frame.width!=90||frame.height!=45||cross.width!=45||cross.height!=45||
       objects.width!=180||objects.height!=45||people.width!=180||people.height!=45)
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
void OriginalQueueView::draw_tile(odh::RasterFrame& image,const QueueTile& tile,const odh::BitmapImage* thumbnail,QueueIconKind kind) const{
    if(!thumbnail){
        // No original no-BMP object observation exists. Keep the existing
        // missing-image presentation rather than fabricate a catalog image.
        odh::blit_color_keyed_bitmap(image,art_.at(frame_key),tile.box,
            odh::UiBitmapRect{tile.active?45:0,0,45,45},magenta);
        if(tile.cancelled)odh::blit_color_keyed_bitmap(image,art_.at(cancel_key),tile.box,std::nullopt,magenta);
        return;
    }
    odh::RasterFrame cell;cell.width=cell.height=45;
    cell.pixels.assign(45*45,{255,0,255,255});
    // Original cCatalog::GetIcon004ccc2f starts with ThumbTemplate frame0;
    // person004e71be starts with PeopleTemplate frame0 (BMP2002 at5,2).
    // These authored backplates supply their own bevel/background colors.
    odh::blit_color_keyed_bitmap(cell,art_.at(kind==QueueIconKind::object?object_template_key:avatar_template_key),
        {0,0,45,45},odh::UiBitmapRect{0,0,45,45},magenta);
    if(kind==QueueIconKind::authored_avatar){
        const auto scale=std::min({1.0,41.0/thumbnail->width,41.0/thumbnail->height});
        const int w=std::max(1,int(thumbnail->width*scale)),h=std::max(1,int(thumbnail->height*scale));
        odh::blit_color_keyed_bitmap(cell,*thumbnail,{(45-w)/2,(45-h)/2,w,h},std::nullopt,magenta);
    }else if(kind==QueueIconKind::avatar_thumbnail){
        // Keep the independently decoded2004/effective-appearance fallback's
        // existing fitting policy; the new resource choice applies to2002.
        draw_object_icon(cell,{0,0,45,45},*thumbnail);
    }else{
        // Original004ccc2f selects the normal half when width>45, centers at
        // native source scale, then clips to the45px intermediate buffer.
        const int width=int(thumbnail->width),height=int(thumbnail->height);
        const int crop=width>45?width/2:width;
        odh::blit_color_keyed_bitmap(cell,*thumbnail,{(45-crop)/2,(45-height)/2,crop,height},
            odh::UiBitmapRect{0,0,crop,height},magenta);
    }
    // Original cActionIcon004cad42 overlays4200 with BLUE as its key. Its
    // magenta corners replace the prepared plate, and the completed45px
    // buffer is then presented with MAGENTA transparency. Applying both
    // keys to the final canvas would retain opaque plate corners.
    odh::blit_color_keyed_bitmap(cell,art_.at(frame_key),{0,0,45,45},
        odh::UiBitmapRect{tile.active?45:0,0,45,45},blue);
    if(tile.cancelled)odh::blit_color_keyed_bitmap(cell,art_.at(cancel_key),{0,0,45,45},std::nullopt,magenta);
    const odh::BitmapImage composed{45,45,24,true,std::move(cell.pixels),0};
    odh::blit_color_keyed_bitmap(image,composed,tile.box,std::nullopt,magenta);
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
