// SPDX-License-Identifier: MPL-2.0
#include "catalog_info_view.hpp"
#include "buy_mode_view.hpp"
#include <algorithm>
#include <cmath>
namespace odh_host {
namespace {
void validate(const CatalogInfoContent& c){
    if(c.name.size()>4096||c.description.size()>16384||c.price.size()>128||c.effects.size()>8192)
        throw odh::FormatError("catalog information text outside bounds");
    if(c.text_only_live&&(c.icon||c.right_icon||!c.price.empty()||!c.effects.empty()))
        throw odh::FormatError("text-only live information has picture or catalog fields");
    if((c.right_icon&&!c.live_help)||(c.live_help&&(!c.price.empty()||!c.effects.empty())))
        throw odh::FormatError("live information has catalog fields or an unowned second picture");
}
int extent(std::string_view s,int width,float scale,const CatalogInfoTextMeasure& measure){
    if(s.empty())return 0;
    if(measure){const auto h=measure(s,width);if(h<0||h>1048576)throw odh::FormatError("catalog information text measure invalid");return h;}
    // Font-free consumers cannot infer glyph wrapping from UTF-8 byte counts.
    // Real text hosts supply the same exact shaped metrics used for painting.
    (void)width;return std::max(1,int(std::lround(14*scale)));
}
}
void draw_catalog_unaffordable_frame(odh::RasterFrame& frame,const odh::UiBitmapRect& panel,float scale){
    if(frame.pixels.size()!=std::size_t(frame.width)*frame.height || !std::isfinite(scale) || scale<=0)
        throw odh::FormatError("catalog refusal frame invalid");
    const auto px=[&](int n){return std::max(1,int(std::lround(n*scale)));};
    // Original800: panel241,313; red box245,317..405,496, 4px thick.
    const int left=panel.x+px(4),top=panel.y+px(4),right=left+px(161),bottom=panel.y+panel.height-px(3);
    const int border=px(4);
    for(int y=std::max(0,top);y<std::min(int(frame.height),bottom);++y)
        for(int x=std::max(0,left);x<std::min(int(frame.width),right);++x)
            if(x<left+border || x>=right-border || y<top+border || y>=bottom-border)
                frame.pixels[std::size_t(y)*frame.width+x]={255,0,0,255};
}

CatalogInfoLayout catalog_info_layout(std::uint32_t w,std::uint32_t h,
        const CatalogInfoContent& c,CatalogInfoTextMeasure measure){
    validate(c);const auto hud=original_control_panel_layout(w,h);const auto s=hud.scale;
    const auto px=[&](int n){return std::max(1,int(std::lround(n*s)));};
    // Observed original 1024x768: right-aligned559px panel immediately above
    // HUD y669. Text starts178px into the panel, thumbnail in left column.
    const int width=std::min(int(w),px(559)),x=int(w)-width;
    const int left=c.live_help?(c.icon?178:15):(c.text_only_live?15:178);
    const int right=c.live_help?(c.right_icon?382:(c.live_language==19?540:545)):(c.text_only_live?545:551);
    const int bottom=hud.base.y+(c.text_only_live?px(3):0);
    const int text_width=c.live_help?std::max(1,px(right-left)):
        std::max(1,width-px(c.text_only_live?29:186));
    const int description=extent(c.description,text_width,s,measure);
    const int effects=extent(c.effects,text_width,s,measure);
    // Observed original live text popup overlaps the HUD by3px; its
    // title/body document adds24px margins and19px heading around the measured text.
    // Catalog retains its existing anchor and42px margin.
    // DVD004cff80: the original adds50px for Japanese/Korean/Chinese/Thai.
    const bool tall_language=c.live_help&&(c.live_language==15||c.live_language==17||
        c.live_language==18||c.live_language==19||c.live_language==20);
    const int wanted=std::max(px(127),px(c.text_only_live?43:42)+description+effects)+(tall_language?px(50):0);
    const int height=std::min(std::max(px(80),bottom-px(8)),wanted),y=bottom-height;
    CatalogInfoLayout l;l.panel={x,y,width,height};
    l.name={x+px(left),y+px(12),text_width,px(19)};
    l.price={x+width-px(76),y+px(12),px(67),px(19)};
    if(c.text_only_live||c.live_help)l.price={x+px(left)+text_width,y+px(12),0,px(19)};
    if(!c.price.empty())l.name.width=std::max(0,l.price.x-l.name.x-px(8));
    const auto remaining=std::max(0,height-px(42));
    const int effects_height=std::min(effects,remaining);
    const int body_height=std::max(0,remaining-effects_height);
    l.description={x+px(left),y+px(32),text_width,body_height};
    l.effects={x+px(left),y+height-px(10)-effects_height,text_width,effects_height};
    l.icon={x+px(12),y+px(12),px(154),std::max(1,height-px(24))};
    if(c.live_help){
        // DVD0050fcbb: picture slots start at0 and372, independently of
        // the text's10px inset. At1024 the left slot excludes the extra50px;
        // the right illustration is centered in the full expanded panel.
        const int left_height=std::max(px(127),height-(w==1024?px(50):0));
        l.icon={x,y+(left_height-px(127))/2,px(168),px(127)};
        const int right_height=std::max(px(127),height-(tall_language?px(50):0));
        l.right_icon={x+px(372),y+(right_height-px(127))/2,px(168),px(127)};
    }
    l.body={x+px(left),y+px(32),text_width,std::max(0,height-px(42))};
    l.description_height=description;l.effects_height=effects;
    l.max_scroll=std::max(0,description+effects-l.body.height);
    return l;
}
const CatalogInfoLayout& CatalogInfoScrollState::prepare(std::uint32_t w,std::uint32_t h,
        const CatalogInfoContent& c,const CatalogInfoTextMeasure& measure)const{
    const unsigned picture_mask=c.live_help?(1U|(c.icon?2U:0U)|(c.right_icon?4U:0U)|(unsigned(c.live_language)<<3)):0;
    if(!valid_||live_picture_mask_!=picture_mask||width_!=w||height_!=h||description_!=c.description||effects_!=c.effects||price_present_!=!c.price.empty()||text_only_live_!=c.text_only_live){
        const bool different=valid_&&(description_!=c.description||effects_!=c.effects);
        layout_=catalog_info_layout(w,h,c,measure);width_=w;height_=h;
        offset_=different?0:std::min(offset_,layout_.max_scroll);
        description_=c.description;effects_=c.effects;price_present_=!c.price.empty();text_only_live_=c.text_only_live;live_picture_mask_=picture_mask;valid_=true;
    }
    return layout_;
}
bool CatalogInfoScrollState::consume(CatalogInfoScrollInput input,const CatalogInfoLayout& layout,int x,int y){
    if(!input.wheel_steps&&!input.page_steps)return false;
    if(!input.page_steps&&!layout.panel.contains(x,y))return false;
    const auto step=std::max(1,layout.body.height-16);
    const auto desired=std::int64_t(offset_)+std::int64_t(input.wheel_steps)*32+
        std::int64_t(input.page_steps)*step;
    offset_=static_cast<int>(std::clamp<std::int64_t>(desired,0,layout.max_scroll));
    return true; // Even a clamped event belongs to the popup, never the camera.
}
CatalogInfoView::CatalogInfoView(const std::filesystem::path& root)
    :art_(root,{"CPanel/Backgrounds/PopupInfo.bmp","CPanel/Backgrounds/PopupInfoTiles.bmp"}){}
void CatalogInfoView::draw(odh::RasterFrame& frame,const CatalogInfoContent& c,
        CatalogInfoTextPainter text,CatalogInfoTextMeasure measure,
        const CatalogInfoScrollState* scroll,CatalogInfoScrollPainter scroll_text,CatalogInfoHeadingPainter heading)const{
    const auto l=scroll?scroll->prepare(frame.width,frame.height,c,measure):catalog_info_layout(frame.width,frame.height,c,measure);
    if(frame.pixels.size()!=std::size_t(frame.width)*frame.height)
        throw odh::FormatError("catalog information frame invalid");
    const auto& tiles=art_.at("CPanel/Backgrounds/PopupInfoTiles.bmp");
    if(tiles.width!=36||tiles.height!=36)throw odh::FormatError("catalog popup tile atlas invalid");
    const auto& background=art_.at("CPanel/Backgrounds/PopupInfo.bmp");
    if(background.width!=559||background.height!=127)
        throw odh::FormatError("catalog popup background invalid");
    const auto hud=original_control_panel_layout(frame.width,frame.height);
    const bool fixed=l.panel.height==int(std::lround(127*hud.scale));
    if(fixed)odh::blit_color_keyed_bitmap(frame,background,l.panel,std::nullopt,{255,0,255});
    const int xs[]={l.panel.x,l.panel.x+12,l.panel.x+l.panel.width-12,l.panel.x+l.panel.width};
    const int ys[]={l.panel.y,l.panel.y+12,l.panel.y+l.panel.height-12,l.panel.y+l.panel.height};
    if(!fixed)for(int r=0;r<3;++r)for(int col=0;col<3;++col)
        for(int y=ys[r];y<ys[r+1];y+=12)for(int x=xs[col];x<xs[col+1];x+=12){
            const int w=std::min(12,xs[col+1]-x),h=std::min(12,ys[r+1]-y);
            odh::blit_color_keyed_bitmap(frame,tiles,{x,y,w,h},odh::UiBitmapRect{col*12,r*12,w,h},{255,0,255});
        }
    const auto picture=[&](const odh::BitmapImage* source,const odh::UiBitmapRect& cell,bool rgba){
        if(!source)return;
        const auto& icon=*source;
        if(!icon.width||!icon.height||icon.width>1024||icon.height>1024||icon.pixels.size()!=std::size_t(icon.width)*icon.height)
            throw odh::FormatError("catalog information icon invalid");
        // CatalogBMP may contain the normal and selected thumbnails side by
        // side. Use the same original normal-frame rule as the catalog itself.
        const auto width=(!rgba&&icon.width>icon.height*1.1f&&icon.width!=127&&icon.width!=128)?icon.width/2:icon.width;
        const auto scale=c.live_help?double(hud.scale):
            std::min({1.,double(cell.width)/width,double(cell.height)/icon.height});
        const int w=std::max(1,int(width*scale)),h=std::max(1,int(icon.height*scale));
        const odh::UiBitmapRect box{cell.x+(cell.width-w)/2,cell.y+(cell.height-h)/2,w,h};
        const odh::UiBitmapRect crop{0,0,int(width),int(icon.height)};
        if(rgba)odh::blit_rgba_bitmap(frame,icon,box,crop);
        else odh::blit_color_keyed_bitmap(frame,icon,box,crop,{255,0,255});
    };
    picture(c.icon,l.icon,c.icon_rgba);
    picture(c.right_icon,l.right_icon,true);
    if(c.unaffordable&&!c.text_only_live)draw_catalog_unaffordable_frame(frame,l.panel,hud.scale);
    if(text){
        if(heading)heading(frame,c.name,c.price,l.name,l.price);
        else {
            text(frame,c.name,l.name,false);
            if(!c.price.empty())text(frame,c.price,l.price,false);
        }
        if(l.max_scroll&&scroll_text){
            const auto offset=std::min(scroll?scroll->offset():0,l.max_scroll);
            const auto section=[&](std::string_view value,int top,int height,bool emphasis){
                const int start=std::max(top,offset),end=std::min(top+height,offset+l.body.height);
                if(start>=end)return;
                const odh::UiBitmapRect clip{l.body.x,l.body.y+start-offset,l.body.width,end-start};
                scroll_text(frame,value,clip,emphasis,start-top);
            };
            section(c.description,0,l.description_height,false);
            section(c.effects,l.description_height,l.effects_height,true);
        }else {
            if(l.description.height)text(frame,c.description,l.description,false);
            if(l.effects.height)text(frame,c.effects,l.effects,true);
        }
    }
}
}
