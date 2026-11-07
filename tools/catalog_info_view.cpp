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
}
int extent(std::string_view s,int width,float scale,const CatalogInfoTextMeasure& measure){
    if(s.empty())return 0;
    if(measure){const auto h=measure(s,width);if(h<0||h>1048576)throw odh::FormatError("catalog information text measure invalid");return h;}
    // Font-free consumers cannot infer glyph wrapping from UTF-8 byte counts.
    // Real text hosts supply the same exact shaped metrics used for painting.
    (void)width;return std::max(1,int(std::lround(14*scale)));
}
}
CatalogInfoLayout catalog_info_layout(std::uint32_t w,std::uint32_t h,
        const CatalogInfoContent& c,CatalogInfoTextMeasure measure){
    validate(c);const auto hud=original_control_panel_layout(w,h);const auto s=hud.scale;
    const auto px=[&](int n){return std::max(1,int(std::lround(n*s)));};
    // Observed original 1024x768: right-aligned559px panel immediately above
    // HUD y669. Text starts178px into the panel, thumbnail in left column.
    const int width=std::min(int(w),px(559)),x=int(w)-width;
    const int bottom=hud.base.y,text_width=std::max(1,width-px(186));
    const int description=extent(c.description,text_width,s,measure);
    const int effects=extent(c.effects,text_width,s,measure);
    const int wanted=std::max(px(127),px(42)+description+effects);
    const int height=std::min(std::max(px(80),bottom-px(8)),wanted),y=bottom-height;
    CatalogInfoLayout l;l.panel={x,y,width,height};
    l.name={x+px(178),y+px(12),text_width,px(19)};
    l.price={x+width-px(76),y+px(12),px(67),px(19)};
    const auto remaining=std::max(0,height-px(42));
    const int effects_height=std::min(effects,remaining);
    const int body_height=std::max(0,remaining-effects_height);
    l.description={x+px(178),y+px(32),text_width,body_height};
    l.effects={x+px(178),y+height-px(10)-effects_height,text_width,effects_height};
    l.icon={x+px(12),y+px(12),px(154),std::max(1,height-px(24))};
    l.body={x+px(178),y+px(32),text_width,std::max(0,height-px(42))};
    l.description_height=description;l.effects_height=effects;
    l.max_scroll=std::max(0,description+effects-l.body.height);
    return l;
}
const CatalogInfoLayout& CatalogInfoScrollState::prepare(std::uint32_t w,std::uint32_t h,
        const CatalogInfoContent& c,const CatalogInfoTextMeasure& measure)const{
    if(!valid_||width_!=w||height_!=h||description_!=c.description||effects_!=c.effects){
        const bool different=valid_&&(description_!=c.description||effects_!=c.effects);
        layout_=catalog_info_layout(w,h,c,measure);width_=w;height_=h;
        offset_=different?0:std::min(offset_,layout_.max_scroll);
        description_=c.description;effects_=c.effects;valid_=true;
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
        const CatalogInfoScrollState* scroll,CatalogInfoScrollPainter scroll_text)const{
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
    if(c.icon){
        const auto& icon=*c.icon;
        if(!icon.width||!icon.height||icon.width>1024||icon.height>1024||icon.pixels.size()!=std::size_t(icon.width)*icon.height)
            throw odh::FormatError("catalog information icon invalid");
        // CatalogBMP may contain the normal and selected thumbnails side by
        // side. Use the same original normal-frame rule as the catalog itself.
        const auto width=(!c.icon_rgba&&icon.width>icon.height*1.1f&&icon.width!=127&&icon.width!=128)?icon.width/2:icon.width;
        const auto scale=std::min({1.,double(l.icon.width)/width,double(l.icon.height)/icon.height});
        const int w=std::max(1,int(width*scale)),h=std::max(1,int(icon.height*scale));
        const odh::UiBitmapRect box{l.icon.x+(l.icon.width-w)/2,l.icon.y+(l.icon.height-h)/2,w,h};
        const odh::UiBitmapRect crop{0,0,int(width),int(icon.height)};
        if(c.icon_rgba)odh::blit_rgba_bitmap(frame,icon,box,crop);
        else odh::blit_color_keyed_bitmap(frame,icon,box,crop,{255,0,255});
    }
    if(text){
        text(frame,c.price.empty()?c.name:c.name+"   "+c.price,l.name,false);
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
