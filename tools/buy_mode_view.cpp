// SPDX-License-Identifier: MPL-2.0
#include "buy_mode_view.hpp"
#include "resource.hpp"
#include "catalog_object_visuals.hpp"
#include "odh/graphics_state.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <tuple>
namespace odh_host {
namespace {
using namespace odh;
bool inside(const std::filesystem::path& root,const std::filesystem::path& file){const auto rel=file.lexically_relative(root);return !rel.empty()&&!rel.is_absolute()&&std::none_of(rel.begin(),rel.end(),[](const auto& part){return part=="..";});}
struct Source {
    std::ifstream stream;View file;std::map<std::string,FarEntry> entries;
    Source(const std::string& path,bool archive):stream(path,std::ios::binary),file(stream,std::filesystem::file_size(path)){
        if(!stream)throw FormatError("cannot open buy catalog resource");
        if(archive){Limits limits;limits.far_entries=16384;limits.far_name_bytes=1024*1024;
            for(auto& e:far1_index(file,limits))if(!entries.emplace(e.name,e).second)throw FormatError("ambiguous buy catalog archive entry");}
    }
    View asset(const std::string& name){if(name.empty())return file;const auto it=entries.find(name);if(it==entries.end())throw FormatError("exact buy catalog entry missing");return file.subview(it->second.offset,it->second.length);}
};
std::optional<BitmapImage> icon(const BuyCatalogResource& resource){std::optional<BitmapImage> result;
    const auto read=[&](const View& view){result=read_object_catalog_icon(view,resource.strings_id);};
    if(resource.definition.asset.empty())with_iff_file(resource.definition.source,read);else with_far_entry(resource.definition.source,resource.definition.asset,read);
    return result;
}
using original_panel_detail::blit;
bool same_camera(const WorldProjection& a,const WorldProjection& b){return a.width==b.width&&a.height==b.height&&a.zoom==b.zoom&&a.rotation==b.rotation&&a.center_tile==b.center_tile;}
bool same_preview_group(const std::optional<BuyModePreviewGroup>& a,const std::optional<BuyModePreviewGroup>& b){
    if(bool(a)!=bool(b))return false;if(!a)return true;
    if(a->base_index!=b->base_index||a->parts.size()!=b->parts.size()||a->native_head_heading_delta!=b->native_head_heading_delta||a->native_selected_heading!=b->native_selected_heading)return false;
    for(std::size_t n=0;n<a->parts.size();++n){const auto& x=a->parts[n];const auto& y=b->parts[n];
        if(x.guid!=y.guid||x.offset_x!=y.offset_x||x.offset_y!=y.offset_y||x.offset_level!=y.offset_level||
           x.graphic!=y.graphic||x.dynamic_sprite_flags!=y.dynamic_sprite_flags)return false;
        if(bool(x.native)!=bool(y.native))return false;
        if(x.native&&(x.native->authored.x!=y.native->authored.x||x.native->authored.y!=y.native->authored.y||
            x.native->authored.level!=y.native->authored.level||x.native->heading_delta!=y.native->heading_delta))return false;
        if(x.native){if(bool(x.native->current)!=bool(y.native->current))return false;
            if(x.native->current&&(x.native->current->x!=y.native->current->x||x.native->current->y!=y.native->current->y||
                x.native->current->level!=y.native->current->level))return false;}
    }return true;
}
bool same_held(const std::optional<BuyModeHeld>& a,const std::optional<BuyModeHeld>& b){
    if(bool(a)!=bool(b))return false;
    if(!a)return true;
    const bool same_identity=bool(a->existing)==bool(b->existing)&&(!a->existing||
        (a->existing->id==b->existing->id&&a->existing->generation==b->existing->generation));
    return same_identity&&a->guid==b->guid&&a->position==b->position&&a->direction==b->direction&&
        a->graphic==b->graphic&&a->dynamic_sprite_flags==b->dynamic_sprite_flags&&a->validity==b->validity&&
        same_preview_group(a->preview_group,b->preview_group);
}
}
BuyModeCatalog load_buy_mode_catalog(const std::filesystem::path& game_root,const std::vector<DefinitionCandidate>& candidates,
        const DefinitionIndex& index,std::uint8_t language,BuyTextDecoder decode){
    if(candidates.size()>8192||language<1||language>20)throw FormatError("buy catalog resource/language bounds");
    const auto root=std::filesystem::canonical(game_root);BuyModeCatalog result;std::size_t catalog_text=0;
    using AssetGroups=std::map<std::string,std::vector<const DefinitionCandidate*>>;
    std::map<std::string,AssetGroups> groups;std::set<std::uint32_t> ambiguity;
    for(const auto& candidate:candidates){
        // OBJDType.Person=2 is a character resource, never a buy-mode item.
        // Native-created families keep those resources outside the game tree;
        // exclude their authoritative type before any catalog filesystem I/O.
        if(candidate.definition.object_type==2)continue;
        if(!index.unique(candidate.definition.guid)){ambiguity.insert(candidate.definition.guid);continue;}
        if(!inside(root,std::filesystem::canonical(candidate.source)))throw FormatError("buy catalog source is outside selected game root");
        groups[candidate.source][candidate.asset].push_back(&candidate);}
    result.diagnostics.ambiguous=ambiguity.size();
    for(const auto& source:groups){const bool archive=std::any_of(source.second.begin(),source.second.end(),[](const auto& a){return !a.first.empty();});Source file(source.first,archive);
        for(const auto& asset:source.second){++result.diagnostics.assets;const auto view=file.asset(asset.first);
            std::map<std::uint16_t,Chunk> definitions;std::map<std::uint16_t,View> string_payloads;std::map<std::uint16_t,StringTable> strings;std::size_t parsed_text=0;
            iff_chunks(view,[&](const Chunk& c){if(c.type=="OBJD"){if(!definitions.emplace(c.id,c).second)throw FormatError("duplicate catalog OBJD");}
                else if(c.type=="CTSS"){if(!string_payloads.emplace(c.id,c.payload).second)throw FormatError("duplicate catalog CTSS");}});
            for(const auto* candidate:asset.second){const auto found=definitions.find(candidate->chunk_id);if(found==definitions.end())throw FormatError("catalog indexed OBJD absent");
                const auto actual=read_object_definition(found->second.payload);if(actual.guid!=candidate->definition.guid)throw FormatError("catalog indexed definition changed");
                const auto words=read_object_definition_words(found->second.payload);if(words.size()<=69)throw FormatError("catalog OBJD fields incomplete");
                const auto word=[&](std::size_t n){return static_cast<std::uint16_t>(words.at(n));};
                if(!(word(40)||word(69))||word(16)||(word(4)==0&&!actual.is_multitile)||(actual.master_id&&actual.sub_index!=-1))continue;
                if(!word(40)){++result.diagnostics.build_mode;continue;}
                std::uint8_t category=0;for(auto mask=word(40);mask>1;mask=static_cast<std::uint16_t>(mask>>1))++category;
                if(category>7){++result.diagnostics.build_mode;continue;}
                BuyCatalogItem item{actual.guid,word(18),category,static_cast<std::uint8_t>(word(39)),found->second.label,{}};
                if(decode)item.name=decode(item.name,1);
                const auto payload=string_payloads.find(word(41));
                if(payload!=string_payloads.end()&&!strings.count(word(41))){AnimationLimits limits;limits.text_bytes=1024*1024;limits.strings=4096;
                    auto table=read_strings(payload->second,limits);for(const auto& entry:table.entries){const auto size=entry.value.size()+entry.comment.size();
                        if(size>2*1024*1024-parsed_text)throw FormatError("catalog asset text exceeds 2Mi bytes");parsed_text+=size;}
                    strings.emplace(word(41),std::move(table));}
                const auto table=strings.find(word(41));
                if(table!=strings.end())for(std::size_t text=0;text<2;++text)if(const auto* value=string_in_language_or_english(table->second,language,text)){
                    auto label=decode?decode(value->value,value->language):value->value;if(text==0)item.name=std::move(label);else item.description=std::move(label);}
                const auto text_size=item.name.size()+item.description.size();
                if(item.name.size()>4096||item.description.size()>16384||text_size>4*1024*1024-catalog_text)throw FormatError("catalog display text exceeds bounded storage");
                catalog_text+=text_size;
                if(item.name.empty())++result.diagnostics.missing_names;
                BuyCatalogResource resource{*candidate,word(41)};
                // FreeSO field-list indices exclude the uint32 version header.
                // In the native raw word bank it occupies two words, so signed
                // ratings are82..88 and skill flags89. Never pad absent fields.
                if(words.size()>=89){
                    std::array<std::int16_t,7> ratings;
                    std::copy_n(words.begin()+82,ratings.size(),ratings.begin());
                    resource.ratings=ratings;
                }
                if(words.size()>=90)resource.skill_flags=word(89);
                if(!result.resources.emplace(item.guid,std::move(resource)).second)throw FormatError("duplicate catalog GUID");
                result.items.push_back(std::move(item));++result.diagnostics.listed;
            }
        }
    }
    return result;
}
BuyModeLayout buy_mode_layout(std::uint32_t width,std::uint32_t height){
    const auto common=original_control_panel_layout(width,height);const auto s=common.scale;const auto baseline=static_cast<float>(height)-600*s;
    const auto rect=[&](float x,float y,float w,float h){return BuyUiRect{static_cast<std::int32_t>(std::lround(x*s)),static_cast<std::int32_t>(std::lround(baseline+y*s)),static_cast<std::int32_t>(std::lround(w*s)),static_cast<std::int32_t>(std::lround(h*s))};};
    BuyModeLayout l;l.viewport_width=width;l.viewport_height=height;l.panel=common.base;l.toggle=common.modes[1];
    for(unsigned n=0;n<8;++n)l.categories[n]=rect(230+static_cast<float>(n%4)*31,504+static_cast<float>(n/4)*43,30,38);
    // Keep catalog artwork in authored45px frames. Wider desktops reveal more
    // columns; they do not enlarge buttons, portraits or thumbnails.
    const auto columns=std::clamp<int>((static_cast<int>(width/s)-390)/45,1,14);
    for(unsigned row=0;row<2;++row)for(int col=0;col<columns;++col)
        l.items.push_back(rect(380+static_cast<float>(col)*45,503+static_cast<float>(row)*48,45,45));
    l.previous=rect(366,525,9,49);l.next=rect(380+static_cast<float>(columns)*45,525,9,49);
    // Original holder shortcuts remain handled by the host (Escape, rotate,
    // Delete). No provisional text toolbar floats over the lot.
    return l;
}
BuyModeView::BuyModeView(std::filesystem::path root,BuyModeCatalog catalog,const DefinitionIndex& definitions,BuyModeText text,std::shared_ptr<const OriginalControlPanelArt> shared_panel)
    :root_(std::filesystem::canonical(root)),catalog_(std::move(catalog)),text_(std::move(text)),panel_art_(shared_panel?std::move(shared_panel):std::make_shared<const OriginalControlPanelArt>(root_)),info_view_(root_),pickup_view_(text_.pickup_text),definitions_(definitions),info_thumbnail_(definitions),preview_renderer_(definitions){}
void BuyModeView::prepare_page(const BuyModeController& controller){
    std::vector<std::uint32_t> ids;for(const auto* item:controller.visible())ids.push_back(item->guid);if(ids==prepared_page_)return;
    std::map<std::uint32_t,BitmapImage> page;std::size_t pixels=0;
    for(const auto* item:controller.visible()){const auto old=icons_.find(item->guid);if(old!=icons_.end()){pixels+=old->second.pixels.size();page.emplace(item->guid,old->second);continue;}
        const auto resource=catalog_.resources.find(item->guid);if(resource==catalog_.resources.end())continue;
        std::optional<BitmapImage> image;try{image=icon(resource->second);}catch(const FormatError&){++catalog_.diagnostics.missing_icons;continue;}
        if(!image){++catalog_.diagnostics.missing_icons;continue;}
        if(image->pixels.size()>2*1048576-pixels)throw FormatError("buy page icon cache exceeds 2Mi pixels");pixels+=image->pixels.size();page.emplace(item->guid,std::move(*image));}
    icons_.swap(page);prepared_page_=std::move(ids);
}
BuyModePreviewResult BuyModeView::prepare_preview(const BuyModeController& controller,const WorldProjection& camera,
        std::optional<std::array<float,3>> actual_tile_position,
        std::function<std::array<float,3>(const EntityPosition&)> part_graphics){
    if(preview_key_valid_&&preview_camera_&&same_camera(camera,*preview_camera_)&&
        same_held(controller.held(),preview_selection_)&&actual_tile_position==preview_tile_position_)return preview_result_;
    preview_key_valid_=true;preview_.reset();preview_selection_=controller.held();preview_camera_=camera;
    preview_tile_position_=actual_tile_position;preview_result_={};
    if(!controller.held()||!controller.held()->position)return preview_result_;
    if(preview_preparations_!=std::numeric_limits<std::uint64_t>::max())++preview_preparations_;
    const auto& held=*controller.held();const auto& p=*held.position;
    try{RasterFrame empty;empty.width=camera.width;empty.height=camera.height;
        if(!empty.width||!empty.height||static_cast<std::uint64_t>(empty.width)*empty.height>1048576)throw FormatError("buy preview viewport exceeds raster budget");
        empty.pixels.assign(static_cast<std::size_t>(empty.width)*empty.height,{0,0,0,255});empty.depth.assign(empty.pixels.size(),std::numeric_limits<float>::infinity());
        std::vector<SavedObjectVisual> visuals;
        const BuyModePreviewGroup* group=held.preview_group?&*held.preview_group:nullptr;
        if(!held.existing){
            if(preview_group_guid_!=held.guid){
                preview_group_guid_=held.guid;preview_authored_group_.reset();preview_group_error_.clear();
                try{const auto* requested=definitions_.unique(held.guid);
                    if(!requested)throw FormatError("buy preview definition is absent or ambiguous");
                    preview_authored_group_=prepare_catalog_buy_group(*requested,definitions_);
                }catch(const FormatError& error){preview_group_error_=error.what();}
            }
            if(!preview_authored_group_)throw FormatError(preview_group_error_);
            group=&*preview_authored_group_;
        }
        if(group){
            auto anchor=p;anchor.direction=held.direction;const auto positions=odh::buy_mode_group_positions(*group,anchor);
            for(std::size_t n=0;n<group->parts.size();++n){const auto& part=group->parts[n];
                auto tile=odh::free_graphics_position(odh::EntityKind::object,positions[n]);
                if(actual_tile_position&&positions[n]==anchor)tile=*actual_tile_position;
                else if(part_graphics)tile=part_graphics(positions[n]);
                else if(actual_tile_position){tile=*actual_tile_position;
                    tile[0]+=(positions[n].x-p.x)/16.0f;tile[1]+=(positions[n].y-p.y)/16.0f;
                    tile[2]+=(positions[n].level-p.level)*2.95f;}
                visuals.push_back({static_cast<std::int16_t>(32767-n),part.guid,part.graphic,positions[n].direction,tile,part.dynamic_sprite_flags});
            }
        }else visuals.push_back({32767,held.guid,held.graphic.value_or(0),held.direction,
            actual_tile_position.value_or(odh::free_graphics_position(odh::EntityKind::object,p)),held.dynamic_sprite_flags});
        auto rendered=preview_renderer_.render(visuals,camera,empty,true);
        if(rendered.layers){
            preview_=std::move(rendered.composite);
            preview_result_.sprite=true;
        }else if(!rendered.records.empty()&&rendered.records.front().no_graphics){
            preview_result_.diagnostic="Selected object has no artwork for its current graphic";
        }
    }catch(const std::exception& error){preview_result_={false,error.what()};}
    return preview_result_;
}
void BuyModeView::prepare_existing_info(const BuyModeExistingInfo& info){
    info_image_guid_.reset();info_thumbnail_.clear();info_image_diagnostic_.clear();
    pickup_content_=info.content;pickup_content_->image=nullptr;pickup_content_->image_rgba=false;
    const auto& result=info_thumbnail_.select(info.visuals);
    if(result.status==CatalogObjectThumbnailStatus::ready){
        pickup_content_->image=&result.image;pickup_content_->image_rgba=true;
    }else info_image_diagnostic_=result.detail;
}
std::optional<ObjectPickupContent> BuyModeView::pickup_content(const BuyModeController& controller)const{
    if(!controller.active()||!controller.held()||!controller.held()->existing)return {};
    const auto target=*controller.held()->existing;
    if(pickup_content_&&pickup_content_->target.id==target.id&&pickup_content_->target.generation==target.generation)
        return pickup_content_;
    ObjectPickupContent unknown;unknown.target=target;return unknown;
}
void BuyModeView::prepare_info(const BuyModeController& controller){
    if(!info_visible(controller))return;
    const auto guid=controller.held()->guid;
    if(info_image_guid_==guid)return;
    info_image_guid_=guid;info_image_diagnostic_.clear();info_thumbnail_.clear();info_scroll_.reset();
    if(info_image_preparations_!=std::numeric_limits<std::uint64_t>::max())++info_image_preparations_;
    try{
        const auto* candidate=definitions_.unique(guid);
        const auto resource=catalog_.resources.find(guid);
        if(!candidate||resource==catalog_.resources.end()||candidate->source!=resource->second.definition.source||
           candidate->asset!=resource->second.definition.asset||candidate->chunk_id!=resource->second.definition.chunk_id)
            throw FormatError("catalog query definition is no longer the exact selected resource");
        const auto parts=prepare_catalog_object_visuals(*candidate,definitions_);
        const auto& result=info_thumbnail_.select(parts);
        if(result.status!=CatalogObjectThumbnailStatus::ready)
            info_image_diagnostic_=result.detail.empty()?"authored query artwork unavailable":result.detail;
    }catch(const FormatError& error){info_image_diagnostic_=error.what();}
}
bool BuyModeView::info_visible(const BuyModeController& controller)const{
    return info_visible_&&controller.active()&&controller.phase()==BuyModePhase::holding_purchase&&controller.held()&&
        controller.find(controller.held()->guid);
}
std::optional<CatalogInfoContent> BuyModeView::info_content(const BuyModeController& controller)const{
    if(!info_visible(controller))return std::nullopt;
    const auto* item=controller.find(controller.held()->guid);
    CatalogInfoContent content;content.name=item->name;content.description=item->description;
    content.price="§"+std::to_string(item->price);
    const auto cached=icons_.find(item->guid);
    content.icon=cached==icons_.end()?&panel_art_->at("cpanel\\catalogunknown.bmp"):&cached->second;
    if(info_image_guid_==item->guid&&info_thumbnail_.result().status==CatalogObjectThumbnailStatus::ready){
        content.icon=&info_thumbnail_.result().image;content.icon_rgba=true;
    }
    const auto resource=catalog_.resources.find(item->guid);
    if(resource!=catalog_.resources.end()){
        const auto line=[&](std::string value){
            if(value.empty())return;
            if(!content.effects.empty())content.effects+='\n';
            content.effects+=value;
        };
        if(resource->second.ratings)for(std::size_t i=0;i<7;++i){
            const auto value=(*resource->second.ratings)[i];
            if(!value||text_.query_labels[i].empty())continue;
            auto label=text_.query_labels[i];bool replaced=false;
            for(const std::string_view marker:{std::string_view("%d"),std::string_view("%i"),std::string_view("%s"),std::string_view("{0}")}){
                const auto found=label.find(marker);
                if(found!=std::string::npos){label.replace(found,marker.size(),std::to_string(value));replaced=true;break;}
            }
            if(!replaced)label+=' '+std::to_string(value);
            line(std::move(label));
        }
        if(resource->second.skill_flags)for(std::size_t i=0;i<7;++i)
            if(*resource->second.skill_flags&(1u<<i))line(text_.query_labels[7+i]);
    }
    return content;
}
void BuyModeView::prepare_info_layout(const BuyModeController& controller,const WorldProjection& camera)const{
    if(const auto content=info_content(controller))
        (void)info_scroll_.prepare(camera.width,camera.height,*content,text_.info_measure);
}
bool BuyModeView::scroll_info(const BuyModeController& controller,CatalogInfoScrollInput input,
        const WorldProjection& camera,int x,int y){
    const auto content=info_content(controller);if(!content)return false;
    return info_scroll_.consume(input,info_scroll_.prepare(camera.width,camera.height,*content,text_.info_measure),x,y);
}
bool BuyModeView::info_contains(const BuyModeController& controller,const WorldProjection& camera,
        std::int32_t x,std::int32_t y)const{
    const auto content=info_content(controller);
    return content&&info_scroll_.prepare(camera.width,camera.height,*content,text_.info_measure).panel.contains(x,y);
}
void BuyModeView::pointer_motion(const BuyModeLayout& layout,std::int32_t x,std::int32_t y){
    if(!layout.panel.contains(x,y))hide_info();
}
const BuyCatalogItem* BuyModeView::hover_item(const BuyModeController& controller,
        const BuyModeLayout& layout,std::int32_t x,std::int32_t y)const{
    if(!controller.active()||pickup_content(controller)||!layout.panel.contains(x,y))return nullptr;
    const auto items=controller.visible();
    for(std::size_t n=0;n<std::min(items.size(),layout.items.size());++n)
        if(layout.items[n].contains(x,y))return items[n];
    return nullptr;
}
bool BuyModeView::click(BuyModeController& controller,const BuyModeLayout& layout,std::int32_t x,std::int32_t y){
    if(layout.toggle.contains(x,y)){hide_info();if(controller.active())controller.close();else controller.open(controller.budget());return true;}
    if(!controller.active())return false;
    if(layout.cancel.contains(x,y)){hide_info();controller.cancel();return true;}
    if(layout.rotate.contains(x,y)){controller.rotate(1);return true;}
    if(layout.sell.contains(x,y)){hide_info();controller.sell();return true;}
    if(!layout.panel.contains(x,y))return false;
    if(controller.phase()==BuyModePhase::awaiting_command)return true;
    if(const auto pickup=pickup_content(controller)){
        if(layout.viewport_width&&layout.viewport_height&&object_pickup_sell_hit(object_pickup_layout(layout.viewport_width,layout.viewport_height),*pickup,x,y))
            controller.sell();
        return true; // Opaque pickup panel never selects an invisible category.
    }
    // Changing a catalog choice only discards its UI holder. It never creates
    // or removes a World object and cannot publish a purchase or sale.
    const auto browsing=[&]{hide_info();if(controller.held())controller.cancel();};
    if(layout.previous.contains(x,y)){browsing();controller.page(-1);return true;}
    if(layout.next.contains(x,y)){browsing();controller.page(1);return true;}
    for(std::size_t n=0;n<layout.categories.size();++n)if(layout.categories[n].contains(x,y)){
        browsing();controller.category(static_cast<std::uint8_t>(n));return true;
    }
    for(std::size_t n=0;n<layout.items.size();++n)if(layout.items[n].contains(x,y)){
        browsing();info_visible_=controller.select(n);return true;
    }
    return true;
}
void BuyModeView::draw(RasterFrame& frame,const BuyModeController& controller,const WorldProjection& camera,BuyModeTextPainter text,OriginalPanelState panel_state)const{
    if(frame.width!=camera.width||frame.height!=camera.height||frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height||(!frame.depth.empty()&&frame.depth.size()!=frame.pixels.size()))throw FormatError("buy frame/camera mismatch");
    const auto l=buy_mode_layout(frame.width,frame.height);const auto common=original_control_panel_layout(frame.width,frame.height);
    if(controller.active()&&controller.held()&&preview_selection_&&preview_&&preview_camera_&&same_camera(camera,*preview_camera_)&&
       same_held(controller.held(),preview_selection_)){
        const auto color=controller.held()->validity==BuyPreviewValidity::blocked?std::array<unsigned,3>{255,70,70}:std::array<unsigned,3>{230,220,100};
        for(std::size_t i=0;i<frame.pixels.size();++i)if(i<preview_->owner_ids.size()&&preview_->owner_ids[i]&&
            (frame.depth.empty()||preview_->image.depth[i]<frame.depth[i]))for(unsigned c=0;c<3;++c)
                frame.pixels[i][c]=static_cast<std::uint8_t>((frame.pixels[i][c]*2+preview_->image.pixels[i][c]*2+color[c])/5);
    }
    if(controller.active()){
        panel_state.paused=true; // Buy mode suspends simulation irrespective of pre-buy speed.
        panel_art_->draw(frame,OriginalPanelMode::buy,panel_state);
        if(const auto pickup=pickup_content(controller)){
            const auto painter=text_.info_painter?text_.info_painter:text;
            pickup_view_.draw(frame,*pickup,[&](auto& image,std::string_view value,const auto& box){
                if(painter)painter(image,value,box,false);
            });
            if(text&&panel_state.show_family_funds)text(frame,controller.budget()?"§"+std::to_string(*controller.budget()):"—",common.budget,true);
            return;
        }
        for(std::size_t n=0;n<8;++n){const auto& image=panel_art_->at(original_panel_category_art[n]);const auto span=static_cast<std::int32_t>(image.width/4);
            blit(frame,image,l.categories[n],{controller.category()==n?span:0,0,span,static_cast<std::int32_t>(image.height)});}
        const auto items=controller.visible();
        for(std::size_t n=0;n<std::min(items.size(),l.items.size());++n){const auto found=icons_.find(items[n]->guid);
            if(panel_art_->find("cpanel\\catalog\\thumbtemplate.bmp"))
                panel_art_->draw_button(frame,"cpanel\\catalog\\thumbtemplate.bmp",l.items[n],
                    controller.held()&&controller.held()->guid==items[n]->guid?1:0);
            const auto& image=found==icons_.end()?panel_art_->at("cpanel\\catalogunknown.bmp"):found->second;
            const auto crop_width=(image.width>image.height*1.1f&&image.width!=127&&image.width!=128)?image.width/2:image.width;
            // A single uniform scale preserves the authored proportions. Small
            // artwork stays at its original pixel size on desktop; its former
            // price row is part of the thumbnail cell again.
            const auto scale=std::min({common.scale,static_cast<float>(l.items[n].width-4)/crop_width,
                static_cast<float>(l.items[n].height-4)/image.height});
            const auto icon_width=std::max(1,static_cast<std::int32_t>(std::lround(crop_width*scale)));
            const auto icon_height=std::max(1,static_cast<std::int32_t>(std::lround(image.height*scale)));
            BuyUiRect icon_box{l.items[n].x+(l.items[n].width-icon_width)/2,
                l.items[n].y+(l.items[n].height-icon_height)/2,icon_width,icon_height};
            blit(frame,image,icon_box,{0,0,static_cast<std::int32_t>(crop_width),static_cast<std::int32_t>(image.height)});
        }
        for(const auto& p:std::array<std::pair<const char*,BuyUiRect>,2>{{{"cpanel\\buttons\\scrollleft.bmp",l.previous},{"cpanel\\buttons\\scrollright.bmp",l.next}}}){
            const auto& image=panel_art_->at(p.first);blit(frame,image,p.second,{0,0,static_cast<std::int32_t>(image.width/4),static_cast<std::int32_t>(image.height)});}
        if(text){
            const auto money=controller.budget()?"§"+std::to_string(*controller.budget()):std::string("—");
            if(panel_state.show_family_funds)text(frame,money,common.budget,true);
            std::string message=controller.phase()==BuyModePhase::awaiting_command?text_.pending:
                controller.issue()==BuyModeIssue::insufficient_funds?text_.insufficient_funds:
                controller.issue()==BuyModeIssue::operation_failed?text_.cannot_place:std::string{};
            if(!message.empty())text(frame,message,{static_cast<std::int32_t>(380*common.scale),
                l.panel.y+static_cast<std::int32_t>(79*common.scale),
                static_cast<std::int32_t>(350*common.scale),static_cast<std::int32_t>(20*common.scale)},true);}
        if(const auto content=info_content(controller))info_view_.draw(frame,*content,text_.info_painter?text_.info_painter:text,text_.info_measure,&info_scroll_,text_.info_scroll_painter);
    }

}
}
