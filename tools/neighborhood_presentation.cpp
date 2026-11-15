#include "original_win_button.hpp"
// SPDX-License-Identifier: MPL-2.0
// Presentation moved from neighborhood_screen.cpp; no LinuxWindow or player store.
#include "neighborhood_presentation.hpp"
#include "original_ui_art.hpp"
#include "linux_text_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
namespace odh_host {
void ui_image(odh::RasterFrame& frame,const OriginalUiArt* art,const char* key,odh::UiBitmapRect box,unsigned states,unsigned state){
    const auto* image=art?art->find(key):nullptr;
    if(!image)return;
    if(!states||state>=states||image->width%states)throw odh::FormatError("invalid original neighborhood button strip");
    if(states==4&&original_win_button_key(key)&&draw_original_win_button(frame,*image,box,state))return;
    const int span=static_cast<int>(image->width/states);
    odh::blit_color_keyed_bitmap(frame,*image,box,odh::UiBitmapRect{static_cast<int>(state)*span,0,span,
        static_cast<int>(image->height)},{255,0,255});
}
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
void ui_caption(odh::RasterFrame& frame,odh::UiBitmapRect box,const std::string& label,std::uint8_t language){
    TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
    style.font_size_px=13;style.padding=1;style.centered=true;style.background={0,0,0,0};
    (void)draw_text_overlay(frame,label,language,style);
}
#endif
// Optional artwork is checked per control: a partial archive may contain the
// banner while missing an individual button. Always paint its real hit area.
void ui_button(odh::RasterFrame& frame,const OriginalUiArt* art,const char* key,odh::UiBitmapRect box,
        unsigned state,const std::string& caption,std::uint8_t language,bool always_caption){
    const bool installed=art&&art->find(key);
    if(installed)ui_image(frame,art,key,box,4,state);
    else{
        const std::array<std::uint8_t,4> fill=state==3?std::array<std::uint8_t,4>{48,48,63,255}:
            std::array<std::uint8_t,4>{20,48,96,255};
        for(int y=box.y;y<box.y+box.height;++y)for(int x=box.x;x<box.x+box.width;++x){
            const bool edge=x==box.x||x==box.x+box.width-1||y==box.y||y==box.y+box.height-1;
            frame.pixels[std::size_t(y)*frame.width+x]=edge?std::array<std::uint8_t,4>{135,150,167,255}:fill;
        }
    }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    if(!installed||always_caption)ui_caption(frame,box,caption,language);
#else
    (void)caption;(void)language;(void)always_caption;
#endif
}
void prepare_neighborhood_presentation(PreparedNeighborhoodMap& map,const odh::BitmapImage& image){
    std::stable_sort(map.markers.begin(),map.markers.end(),[](const auto& a,const auto& b){return a.location.y<b.location.y;});
    const auto depth=[&](uint16_t house){auto marker=std::find_if(map.markers.begin(),map.markers.end(),[&](const auto& m){return m.location.house==house;});return marker==map.markers.end()?0:marker->location.y;};
    std::stable_sort(map.houses.begin(),map.houses.end(),[&](const auto& a,const auto& b){return depth(a.house)<depth(b.house);});
    std::vector<odh::NeighborhoodMarker> fallback;
    for(const auto& marker:map.markers)if(marker.file_exists&&std::none_of(map.houses.begin(),map.houses.end(),[&](const auto& house){return house.house==marker.location.house;}))
        fallback.push_back(marker); // An installed lot without usable artwork stays accessible.
    map.frame=odh::draw_neighborhood_map(image,fallback,map.marker_scale);
    map.background=map.frame.pixels;
    draw_map_houses(map.frame,map,std::nullopt);
}
namespace {
void fade_keyed(odh::RasterFrame& frame,const odh::BitmapImage& image,odh::UiBitmapRect box,float opacity){
    if(opacity<=0)return;
    if(opacity>=1){odh::blit_color_keyed_bitmap(frame,image,box,std::nullopt,{255,0,255});return;}
    auto faded=image;faded.bit_count=32;const auto alpha=unsigned(std::lround(opacity*255));
    for(auto& pixel:faded.pixels)pixel[3]=uint8_t((unsigned(pixel[3])*alpha+127)/255);
    odh::blit_color_keyed_bitmap(frame,faded,box,std::nullopt,{255,0,255});
}
}
NeighborhoodOverlay::NeighborhoodOverlay(odh::BitmapImage image):image_(std::move(image)){
    // Other images retain the checked general compositor.
    if(image_.bit_count==32 || image_.width>800 || image_.height>600 ||
       !image_.width || !image_.height ||
       image_.pixels.size()!=std::size_t(image_.width)*image_.height)return;
    std::vector<Run> runs;
    std::size_t count=0;
    for(unsigned y=0;y<image_.height;++y){
        const auto* row=image_.pixels.data()+std::size_t(y)*image_.width;
        unsigned start=0;
        for(unsigned x=0;x<=image_.width;++x){
            if(x<image_.width && !(row[x][0]==255 && row[x][1]==0 && row[x][2]==255)){
                if(row[x][3]!=255)return; // Preserve the source's authored fade.
                continue;
            }
            if(x>start){
                if(runs.size()==4096)return;
                runs.push_back({int(start),int(y),int(x-start)});
                count+=x-start;
            }
            start=x+1;
        }
    }
    decltype(image_.pixels) visible;
    visible.reserve(count);
    for(const auto& run:runs){
        const auto first=image_.pixels.begin()+std::size_t(run.y)*image_.width+run.x;
        visible.insert(visible.end(),first,first+run.length);
    }
    runs_=std::move(runs);
    image_.pixels=std::move(visible);
    packed_=true;
}

void NeighborhoodOverlay::draw(odh::RasterFrame& target,int x,int y,float opacity) const{
    if(!packed_){
        fade_keyed(target,image_,{x,y,int(image_.width),int(image_.height)},opacity);
        return;
    }
    if(!target.width || !target.height ||
       std::uint64_t(target.width)*target.height!=target.pixels.size())
        throw odh::FormatError("UI bitmap destination frame is malformed");
    const unsigned alpha=unsigned(std::lround(std::clamp(opacity,0.0f,1.0f)*255));
    if(!alpha)return;
    std::size_t offset=0;
    for(const auto& run:runs_){
        const auto* source=image_.pixels.data()+offset;
        offset+=run.length;
        const std::int64_t left=std::int64_t(x)+run.x, row=std::int64_t(y)+run.y;
        if(row<0 || row>=target.height)continue;
        const auto begin=std::max<std::int64_t>(0,left);
        const auto end=std::min<std::int64_t>(target.width,left+run.length);
        if(end<=begin)continue;
        source+=begin-left;
        auto* dest=target.pixels.data()+std::size_t(row)*target.width+begin;
        if(alpha==255)std::copy_n(source,end-begin,dest);
        else for(auto i=begin;i<end;++i,++source,++dest){
            for(unsigned channel=0;channel<3;++channel)
                (*dest)[channel]=std::uint8_t(((*source)[channel]*alpha+(*dest)[channel]*(255-alpha)+127)/255);
            (*dest)[3]=255;
        }
    }
}
void draw_map_houses(odh::RasterFrame& frame,const PreparedNeighborhoodMap& map,std::optional<std::uint16_t> hovered,
                     const std::map<std::uint16_t,float>* roof_openness){
    for(const auto& house:map.houses){
        float amount=hovered==house.house?1.0f:0.0f;
        if(roof_openness){const auto state=roof_openness->find(house.house);amount=state==roof_openness->end()?0:state->second;}
        if(!house.open)amount=0;
        if(amount>0)odh::blit_color_keyed_bitmap(frame,*house.open,house.open_box,std::nullopt,{255,0,255});
        if(amount>=1)continue;
        if(amount<=0){odh::blit_color_keyed_bitmap(frame,house.closed,house.closed_box,std::nullopt,{255,0,255});continue;}
        fade_keyed(frame,house.closed,house.closed_box,1-amount);
    }
}
void NeighborhoodMapPresentation::advance(const PreparedNeighborhoodMap& map,double elapsed_ms){
    if(!std::isfinite(elapsed_ms)||elapsed_ms<0)return;
    time_ms+=elapsed_ms;
    const auto hovered=pointer&&(*pointer)[1]>=52?pick_map_lot(map,(*pointer)[0],(*pointer)[1]):std::nullopt;
    for(const auto& house:map.houses){
        auto& transition=roofs[house.house];const float target=hovered==house.house&&house.open?1.0f:0.0f;
        if(target!=transition.target){transition.start=transition.value;transition.target=target;transition.elapsed_ms=0;}
        // The fixed Simitone reference tweens AlphaTime for half a second in
        // both directions. Time belongs to the frame producer, never draw calls.
        transition.elapsed_ms=std::min(500.0,transition.elapsed_ms+elapsed_ms);
        transition.value=transition.start+(transition.target-transition.start)*float(transition.elapsed_ms/500.0);
    }
}

// NBRS.Name identifies character files such as "user00001" and may be
// literally "iffname" for player creations. Display only the localized CTSS
// for a unique person definition, never the NBRS string or filename.
void draw_house_bubble(odh::RasterFrame& frame,const OriginalUiArt* art,const odh::LotLocation& spot,
        const NeighborhoodHoverText& text,const odh::FamilyEntry& family,
        const NeighborhoodPortraitProvider& portraits,const std::string& resident_names,std::uint8_t language){
    constexpr int width=200,height=166;
    const int x=std::clamp(spot.x+4,0,int(frame.width)-width);
    const int y=std::clamp(spot.y-height-6,0,int(frame.height)-height);
    const auto* base=art?art->find("Nbhd/HotSpotPopupBkg.bmp"):nullptr;
    const auto* alpha=art?art->find("Nbhd/HotSpotPopupBkgAlpha.bmp"):nullptr;
    const bool masked=base&&alpha&&base->width==width&&base->height==height&&alpha->width==width&&alpha->height==height;
    for(int dy=0;dy<height;++dy)for(int dx=0;dx<width;++dx){
        const auto at=static_cast<std::size_t>(dy*width+dx);
        const auto opacity=masked?alpha->pixels[at][0]:160;
        const auto color=masked?base->pixels[at]:std::array<std::uint8_t,4>{0,0,82,255};
        auto& pixel=frame.pixels[static_cast<std::size_t>(y+dy)*frame.width+x+dx];
        for(unsigned c=0;c<3;++c)pixel[c]=static_cast<std::uint8_t>((color[c]*opacity+pixel[c]*(255-opacity)+127)/255);
    }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;style.x=x+14;style.y=y+4;style.width=width-22;style.height=28;style.font_size_px=14;
    style.padding=0;style.background={0,0,0,0};
    if(!text.address.empty())(void)draw_text_overlay(frame,text.address,language,style);
    style.y=y+40;style.height=68;style.font_size_px=13;style.wrap_lines=true;
    if(!text.family_summary.empty())(void)draw_text_overlay(frame,text.family_summary,language,style);
    if(!portraits&&!resident_names.empty()){
        style.y=y+110;style.height=50;
        (void)draw_text_overlay(frame,resident_names,language,style);
    }
#else
    (void)text;(void)resident_names;(void)language;
#endif
    if(portraits)for(std::size_t i=0;i<family.family.member_guids.size()&&i<8;++i){
        if(const auto* face=portraits(family.family.member_guids[i])){
            const odh::UiBitmapRect box{x+14+int(i%6)*25,y+110+int(i/6)*25,25,25};
            if(face->width==25&&face->height==25){
                // BMP_2004 already contains the authored blue miniature frame.
                // The original rollover uses it at its native 25x25 extent.
                odh::blit_color_keyed_bitmap(frame,*face,box,std::nullopt,{255,0,255});
            }else{
                // Retain the earlier generated-head fallback for characters
                // without an authored miniature; never replace authored art.
                ui_image(frame,art,"CPanel/People/PeopleTemplate.bmp",box,4);
                odh::blit_color_keyed_bitmap(frame,*face,{box.x+1,box.y+1,22,22},std::nullopt,{255,0,255});
            }
        }
    }
}

namespace {
// Complete Collection provides the resizable kHotspotPopupTiler (Res_Nbhd.RT).
// Its observed vacant-house rollover is wider than the occupied portrait plate.
// Preserve the existing FrontUI::NineSlice source mapping and authored alpha.
void draw_vacant_lot_bubble(odh::RasterFrame& frame,const OriginalUiArt* art,
        const odh::LotLocation& spot,const NeighborhoodHoverText& text,std::uint8_t language){
    const auto* tile=art?art->find("Nbhd/HotspotPopupTiler.TGA"):nullptr;
    if(!tile||!tile->width||!tile->height)return;
    // Original 1024x768 frame contains an unscaled 800x600 map.
    // Vacant popup painted bounds are x417..772 (356px); low-alpha
    // authored edges account for the remaining ~4px of its 360px box.
    const int width=std::min(360,int(frame.width));
    int content_height=106;
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    content_height=measure_text_overlay(text.family_summary,language,13,width-40).height;
#endif
    const int height=std::min(std::max(190,content_height+80),int(frame.height)-52);
    if(height<=0)return;
    const int x=std::clamp(spot.x+4,0,int(frame.width)-width);
    const int y=std::clamp(spot.y-height-6,52,int(frame.height)-height);
    const int border=std::min({30,int(tile->width)/2,int(tile->height)/2,width/2,height/2});
    for(int dy=0;dy<height;++dy){
        const int sy=dy<border?dy:dy>=height-border?int(tile->height)-(height-dy):
            border+int(std::int64_t(dy-border)*(int(tile->height)-2*border)/std::max(1,height-2*border));
        for(int dx=0;dx<width;++dx){
            const int sx=dx<border?dx:dx>=width-border?int(tile->width)-(width-dx):
                border+int(std::int64_t(dx-border)*(int(tile->width)-2*border)/std::max(1,width-2*border));
            const auto& source=tile->pixels[std::size_t(sy)*tile->width+sx];
            auto& target=frame.pixels[std::size_t(y+dy)*frame.width+x+dx];
            for(unsigned c=0;c<3;++c)target[c]=std::uint8_t((source[c]*source[3]+target[c]*(255-source[3])+127)/255);
        }
    }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    TextOverlayStyle style;style.x=x+20;style.y=y+20;style.width=width-40;style.height=28;
    style.font_size_px=14;style.padding=0;style.background={0,0,0,0};
    style.foreground={187,198,195,255};
    if(!text.address.empty())(void)draw_text_overlay(frame,text.address,language,style);
    style.y=y+58;style.height=height-76;style.font_size_px=13;style.wrap_lines=true;
    if(!text.family_summary.empty())(void)draw_text_overlay(frame,text.family_summary,language,style);
#else
    (void)text;(void)language;
#endif
}
} // namespace

std::optional<std::uint16_t> pick_map_lot(const PreparedNeighborhoodMap& map,std::int32_t x,std::int32_t y){
    if(map.houses.empty())return odh::pick_neighborhood_lot(map.markers,map.marker_scale,x,y);
    if(x<0||y<0||x>=800||y>=600)return std::nullopt;
    for(auto marker=map.markers.rbegin();marker!=map.markers.rend();++marker){
        if(!marker->file_exists)continue;
        const auto house=std::find_if(map.houses.begin(),map.houses.end(),[&](const auto& value){return value.house==marker->location.house;});
        if(house!=map.houses.end()){
            if(house->hit.contains(x,y))return house->house;
        }else if(std::abs(x-marker->location.x)<=static_cast<int>(6*map.marker_scale)&&
                 std::abs(y-marker->location.y)<=static_cast<int>(6*map.marker_scale))return marker->location.house;
    }
    return std::nullopt;
}

std::optional<std::size_t> FamilyPickerPresentation::target_at(int x,int y)const{
        if(cancel_box.contains(x,y))return 0;
        if(creating&&add_box.contains(x,y))return 1;
        if(creating&&archive_box.contains(x,y))return entry()&&can_archive(*entry())?std::optional<std::size_t>{2}:std::nullopt;
        if(move_box.contains(x,y))return entry()&&can_move(*entry())?std::optional<std::size_t>{3}:std::nullopt;
        if(rename&&entry()&&rename_box.contains(x,y))return 4;
        if(previous_box.contains(x,y))return first?std::optional<std::size_t>{5}:std::nullopt;
        if(next_box.contains(x,y))return first+page_size<choices.size()?std::optional<std::size_t>{6}:std::nullopt;
        if(x>=row_x&&x<row_x+690&&y>=row_y&&y<row_y+int(page_size)*row_height){
            const auto i=first+std::size_t((y-row_y)/row_height);
            if(i<choices.size())return 100+i;
        }
        return {};
}
unsigned FamilyPickerPresentation::state(std::size_t id,bool enabled)const{
        const auto hovered=pointer?target_at((*pointer)[0],(*pointer)[1]):std::nullopt;
        return !enabled?3U:hovered==id&&activation.pressed()==id?1U:hovered==id?2U:0U;
}
odh::RasterFrame FamilyPickerPresentation::draw(const OriginalUiArt* art,std::uint8_t language,const std::string& localized_title,
    const std::string& localized_action,const std::string& localized_archive,const std::string& localized_cancel,
    const std::string& localized_no_family_title,const std::string& localized_no_family_message,
    const NeighborhoodPortraitProvider& portraits)const{
    const auto* background=art?art->find(magic_town?"Nbhd/PickBkgMagic.bmp":"Nbhd/PickBkg.bmp"):nullptr;
    if(!background&&art)background=art->find("Nbhd/PickBkg.bmp");
    const auto* rows=art?art->find("Nbhd/FamilyPickItemBkg.bmp"):nullptr;
        odh::RasterFrame frame{};frame.width=800;frame.height=600;
        if(background)frame.pixels=background->pixels;
        else frame.pixels.assign(800*600,{0,0,82,255});
        frame.depth.assign(frame.pixels.size(),std::numeric_limits<float>::infinity());
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        TextOverlayStyle text;text.background={0,0,0,0};text.padding=1;text.centered=true;
        text.x=0;text.y=4;text.width=800;text.height=35;text.font_size_px=21;
        (void)draw_text_overlay(frame,localized_title,language,text);
#endif
        for(std::size_t r=0;r<page_size;++r){
            const auto i=first+r;const int y=row_y+int(r)*row_height;
            const bool active=selected==i;
            const bool hovered=pointer&&target_at((*pointer)[0],(*pointer)[1])==100+i;
            if(rows)odh::blit_color_keyed_bitmap(frame,*rows,{row_x,y,690,68},
                odh::UiBitmapRect{0,active||(hovered&&activation.pressed()==100+i)?136:hovered?68:0,690,68},{255,0,255});
            if(i>=choices.size())continue;
            const auto& family=*choices[i];
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
            text.x=61;text.y=y;text.width=455;text.height=23;text.centered=false;text.font_size_px=16;
            (void)draw_text_overlay(frame,family.display_name.empty()?"Family "+std::to_string(family.family.family_number):family.display_name,language,text);
            text.x=magic_town?530:564;text.y=y+24;text.width=102;text.height=22;text.centered=true;text.font_size_px=13;
            const auto worth=static_cast<std::int64_t>(family.family.value_in_arch)+family.family.budget;
            auto amount=std::to_string(worth);
            const auto sign=worth<0?1U:0U;
            if(language==1||language==2||language==14){
                for(auto at=amount.size();at>sign+3;at-=3)amount.insert(at-3,1,language==14?'.':',');
            }
            (void)draw_text_overlay(frame,"§"+amount,language,text);
            text.x=magic_town?632:648;text.width=50;
            (void)draw_text_overlay(frame,std::to_string(family.family.family_friends),language,text);
            if(magic_town&&magicoins){
                const auto coins=std::to_string(magicoins(family));
                // Keep the authored column centre and the native font size;
                // a nonzero wallet must not inherit a one-digit clip box.
                const auto extent=measure_text_overlay(coins,language,text.font_size_px);
                text.width=std::max(30,extent.width+2*text.padding);
                text.x=697-text.width/2;
                (void)draw_text_overlay(frame,coins,language,text);
            }

#endif
            for(std::size_t member=0;member<family.family.member_guids.size();++member){
                const odh::UiBitmapRect box{150+int(member)*48,y+20,45,45};
                ui_image(frame,art,"CPanel/People/PeopleTemplate.bmp",box,4);
                const auto guid=family.family.member_guids[member];
                const auto* head=scoped_portraits?scoped_portraits(family,guid):portraits?portraits(guid):nullptr;
                if(head){
                    // Family exports contain authored 25x25 thumbnails, while
                    // resident face cells are 35x41. Keep each resource's aspect
                    // and native size instead of stretching an export into a head.
                    const double scale=std::min({1.0,35.0/head->width,41.0/head->height});
                    const int width=std::max(1,int(head->width*scale));
                    const int height=std::max(1,int(head->height*scale));
                    odh::blit_color_keyed_bitmap(frame,*head,{box.x+(45-width)/2,box.y+(45-height)/2,width,height},std::nullopt,{255,0,255});
                }
            }
        }
        ui_button(frame,art,"Nbhd/NbhdTileBtn.bmp",cancel_box,state(0,true),localized_cancel,language,true);
        if(creating)ui_button(frame,art,"Nbhd/PAFAddFamily.bmp",add_box,state(1,true),language==14?"Criar família":"Create family",language);
        if(creating)ui_button(frame,art,"Nbhd/PAFDeleteFamily.bmp",archive_box,state(2,entry()&&can_archive(*entry())),localized_archive,language);
        ui_button(frame,art,"Nbhd/PAFMoveIn.bmp",move_box,state(3,entry()&&can_move(*entry())),localized_action,language);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if(rename&&entry())ui_caption(frame,rename_box,language==14?"Renomear família":"Rename family",language);
        if(rename&&!std::any_of(choices.begin(),choices.end(),[&](auto e){return can_move(*e);})){
            text.x=60;text.y=485;text.width=600;text.height=28;text.font_size_px=13;text.centered=true;
            (void)draw_text_overlay(frame,choices.empty()?localized_no_family_title:localized_no_family_message,language,text);
        }
#endif
        if(choices.size()>page_size){
            ui_button(frame,art,"NghUI/Previous.bmp",previous_box,state(5,first!=0),"‹",language);
            ui_button(frame,art,"NghUI/Next.bmp",next_box,state(6,first+page_size<choices.size()),"›",language);
        }
        return frame;
 }
} // namespace odh_host

namespace odh_host {
int neighborhood_arrow_at(std::int32_t x,std::int32_t y) noexcept {
    if(previous_neighborhood.contains(x,y))return -1;
    if(next_neighborhood.contains(x,y))return 1;
    return 0;
}
unsigned neighborhood_mode_button_at(std::int32_t x, std::int32_t y) noexcept {
    for(std::size_t i=0;i<travel_modes.size();++i)if(region_button(i).contains(x,y))return travel_modes[i];
    return 0;
}

namespace {
struct MapControl {size_t target;odh::UiBitmapRect box;const char* art;std::string caption;bool enabled;bool selected=false;};
std::vector<MapControl> map_controls(const PreparedNeighborhoodMap& map,const NeighborhoodMapPresentation& view,
                                     const std::string& cancel,std::uint8_t language){
    const bool normal=!view.managing&&!view.rezoning;
    std::vector<MapControl> controls;
    if(map.mode==1||map.mode==4||map.mode==7)
        controls.push_back({8,family_button,"NghUI/MoveIn.bmp",language==14?"Famílias":"Families",normal&&view.allow_create_family});
    if(map.mode==1||map.mode==4){
        controls.push_back({11,previous_neighborhood,"NghUI/Previous.bmp","‹",normal&&view.previous_available});
        controls.push_back({12,next_neighborhood,"NghUI/Next.bmp","›",normal&&view.next_available});
        controls.push_back({13,import_button,"NghUI/Import.bmp",language==14?"Importar":"Import",normal&&view.allow_import});
        controls.push_back({10,manage_button,"NghUI/Bulldoze.bmp",language==14?"Despejar":"Evict",view.can_manage&&!view.rezoning,view.managing});
        controls.push_back({15,rezone_button,"NghUI/Rezone.bmp",language==14?"Zona":"Zone",view.can_rezone&&!view.managing,view.rezoning});
        const std::array<const char*,4> pt{"Centro","Férias","Estúdio","Magia"},en{"Downtown","Vacation","Studio","Magic"};
        for(size_t i=0;i<travel_modes.size();++i){const auto mode=travel_modes[i];
            controls.push_back({mode,region_button(i),region_icons[i],language==14?pt[i]:en[i],normal&&map.region_available[mode]});}
        controls.push_back({9,exit_button,"NghUI/Exit.bmp",cancel,normal});
    }else{
        // Complete Collection destination maps have their own compact banner:
        // residents at 200, neighbourhood at 325, bulldoze at 375, lot import at 425.
        const char* home=map.mode==2?"Downtown/Return.bmp":map.mode==5?"Studiotown/Return.bmp":map.mode==7?"Magicland/Return.bmp":"NghUI/Return.bmp";
        const char* bulldoze=map.mode==2?"Downtown/Bulldoze.bmp":map.mode==5?"Studiotown/Bulldoze.bmp":map.mode==7?"Magicland/Bulldoze.bmp":"NghUI/Bulldoze.bmp";
        const char* import=map.mode==2?"Downtown/Import.bmp":map.mode==3?"VIsland/Import.bmp":map.mode==5?"Studiotown/import.bmp":"Magicland/Import.bmp";
        controls.push_back({1,{325,0,50,52},home,language==14?"Voltar":"Back",normal});
        controls.push_back({10,{375,0,50,52},bulldoze,language==14?"Demolir":"Bulldoze",view.can_manage&&!view.rezoning,view.managing});
        // This is lot import, distinct from the residential family-import action.
        controls.push_back({0,{425,0,50,52},import,language==14?"Importar lote":"Import lot",false});
    }
    return controls;
}
}
std::optional<std::size_t> NeighborhoodMapPresentation::target_at(const PreparedNeighborhoodMap& map,int x,int y)const{
    if(logo_button.contains(x,y))return managing||rezoning?std::nullopt:std::optional<size_t>{14};
    for(const auto& control:map_controls(map,*this,{},map.language))
        if(control.box.contains(x,y))return control.enabled?std::optional<size_t>{control.target}:std::nullopt;
    if(y<52)return {};
    if(const auto house=pick_map_lot(map,x,y))return 100+*house;
    return {};
}
odh::RasterFrame NeighborhoodMapPresentation::draw(const PreparedNeighborhoodMap& map,const OriginalUiArt* art,
    const std::string& localized_cancel,std::uint8_t language,unsigned neighborhood_number,
    const NeighborhoodHoverTextProvider& hover_text,const NeighborhoodPortraitProvider& portraits,
    const NeighborhoodPortraitProvider& known_portraits,const std::function<std::string(std::uint32_t)>& resident_name){
        auto frame=map.frame;
        const auto hovered_lot=pointer?pick_map_lot(map,(*pointer)[0],(*pointer)[1]):std::nullopt;
        const bool opening=roofs.empty()?hovered_lot.has_value():
            std::any_of(roofs.begin(),roofs.end(),[](const auto& roof){return roof.second.value>0;});
        if(opening&&!map.background.empty()){
            frame.pixels=map.background;
            std::map<std::uint16_t,float> openness;
            for(const auto& entry:roofs)openness.emplace(entry.first,entry.second.value);
            draw_map_houses(frame,map,hovered_lot,roofs.empty()?nullptr:&openness);
        }
        for(const auto& animation:map.animations){
            const auto count=animation.frames.size();if(!count||!animation.frame_ms)continue;
            const auto total=animation.pulsates&&count>1?count*2-2:count;
            const auto cycle=std::fmod(time_ms/double(animation.frame_ms),double(total));
            const auto step=size_t(cycle);
            const auto index=[&](size_t n){n%=total;return n<count?n:total-n;};
            const auto paint=[&](size_t n,float opacity){animation.frames[index(n)].draw(frame,animation.x,animation.y,opacity);};
            paint(step,1);paint(step+1,float(cycle-step));
        }
        for(const auto& image:map.foreground)image.draw(frame,0,0,1);
        if(rezoning)for(const auto& house:map.houses){
            const auto marker=std::find_if(map.markers.begin(),map.markers.end(),[&](const auto& m){return m.location.house==house.house;});
            if(marker==map.markers.end()||!marker->file_exists)continue;
            const auto zone=community.find(house.house);
            const std::array<uint8_t,4> color=marker->occupied?std::array<uint8_t,4>{255,0,0,255}:
                zone!=community.end()&&zone->second?std::array<uint8_t,4>{0,0,255,255}:std::array<uint8_t,4>{0,255,0,255};
            // The saved thumbnail contains the isometric terrain diamond below
            // its building; keep the authored bounds instead of fixed markers.
            const auto& box=house.closed_box;const int w=box.width;
            const int bottom=box.y+box.height-1,cy=bottom-w/4;
            const std::array<std::array<int,2>,4> points{{{box.x,cy},{box.x+w/2,bottom-w/2},{box.x+w-1,cy},{box.x+w/2,bottom}}};
            for(unsigned edge=0;edge<4;++edge){auto a=points[edge],b=points[(edge+1)%4];
                const int dx=std::abs(b[0]-a[0]),dy=-std::abs(b[1]-a[1]);
                const int sx=a[0]<b[0]?1:-1,sy=a[1]<b[1]?1:-1;int error=dx+dy;
                for(;;){if(a[0]>=0&&a[1]>=52&&a[0]<int(frame.width)&&a[1]<int(frame.height))frame.pixels[std::size_t(a[1])*frame.width+a[0]]=color;
                    if(a==b)break;const int twice=2*error;if(twice>=dy){error+=dy;a[0]+=sx;}if(twice<=dx){error+=dx;a[1]+=sy;}}
            }
        }
        ui_image(frame,art,map.mode==1||map.mode==4?"NghUI/banner_neighborhood.bmp":"NghUI/banner_downtown.bmp",{0,0,800,52});
        ui_image(frame,art,"NghUI/TheSimsLogo.bmp",{5,0,70,52});
        if(hovered_lot&&pointer&&(*pointer)[1]>=52){
            const auto found=std::find_if(map.effective_families.begin(),map.effective_families.end(),[&](const auto& f){return f.family.house_number==*hovered_lot;});
            const auto* family=found==map.effective_families.end()?nullptr:&*found;
            const auto marker=std::find_if(map.markers.begin(),map.markers.end(),[&](const auto& m){return m.location.house==*hovered_lot;});
            if(marker!=map.markers.end()&&marker->file_exists){
                auto entry=hover_cache.try_emplace(*hovered_lot);
                if(entry.second){
                    if(hover_text)try{entry.first->second=hover_text(*hovered_lot);}
                    catch(const odh::FormatError& error){
                        std::clog<<"OpenDollhouse: lot "<<*hovered_lot<<" hover text unavailable: "<<error.what()<<'\n';
                    }
                    if(family&&marker->occupied&&entry.first->second.family_summary.empty())entry.first->second.family_summary=
                        family->display_name+"\n§ "+std::to_string(static_cast<std::int64_t>(family->family.budget)+family->family.value_in_arch);
                }
                if(!marker->occupied){
                    if(!entry.first->second.family_summary.empty())
                        draw_vacant_lot_bubble(frame,art,marker->location,entry.first->second,language);
                }else if(family){
                    std::string residents;
                    if(!portraits)for(const auto guid:family->family.member_guids){
                        const auto id=map.unique_neighbors.find(guid);
                        if(id==map.unique_neighbors.end()||!id->second)continue;
                        const auto name=resident_name?resident_name(guid):std::string();
                        if(name.empty())continue;
                        if(!residents.empty())residents+=", ";
                        residents+=name;
                    }
                    draw_house_bubble(frame,art,marker->location,entry.first->second,*family,known_portraits,residents,language);
                }
            }
        }
        for(const auto& control:map_controls(map,*this,localized_cancel,language)){
            auto box=control.box;
            if(box.width==50&&art&&art->find(control.art))box.width=53;
            const bool hover=pointer&&box.contains((*pointer)[0],(*pointer)[1]);
            const bool pressed=activation.pressed()==control.target;
            ui_button(frame,art,control.art,box,!control.enabled?3U:control.selected||(pressed&&hover)?1U:hover?2U:0U,control.caption,language);
        }
        // The far-right original icon is the Internet/update control, not a
        // return/quit action. Keep its authored appearance without routing it
        // to an unrelated local action.
        ui_image(frame,art,"NghUI/InetBtn.bmp",{761,0,39,52},4);
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        if(map.mode==1||map.mode==4)ui_caption(frame,{126,6,20,24},std::to_string(neighborhood_number),language);
#endif
        return frame;
}
NeighborhoodHouseArt prepare_house_presentation(std::uint16_t house,const odh::LotLocation& location,
    odh::UiBitmapRect box,odh::UiBitmapRect open_box,odh::BitmapImage closed,std::optional<odh::BitmapImage> open){
    const auto width=box.width,height=box.height;
    return NeighborhoodHouseArt{house,box,open_box,{location.x-width/2,location.y-width/4,width,height/2},
        std::move(closed),std::move(open)};
}
}
