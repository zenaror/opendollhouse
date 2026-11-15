// SPDX-License-Identifier: MPL-2.0
#include "catalog_info_text.hpp"
#include "linux_text_renderer.hpp"
#include "buy_mode_view.hpp"
#include <algorithm>
#include <cmath>
namespace odh_host {
namespace {
int font_for(float scale,bool live=false){return std::max(8,int(std::lround((live?13:11)*scale)));}
struct LiveMetrics {int font,advance,height;std::vector<TextOverlayLineRange> ranges;bool compact=false;};
LiveMetrics live_metrics(std::string_view value,std::uint8_t language,int width,float requested_scale=0.f){
    const auto scale=requested_scale>0?requested_scale:std::min(1.f,float(width)/530);const int font=font_for(scale,true);
    LiveMetrics m{font,std::max(1,int(std::lround(17*scale))),measure_text_overlay(value,language,font,width).height,
        text_overlay_line_ranges(value,language,font,width),false};
    if(!m.ranges.empty()){
        const int line=m.ranges.front().bottom-m.ranges.front().top;
        m.compact=line>0;
        int previous=0;
        for(const auto& range:m.ranges){
            if(range.top<previous||range.bottom-range.top!=line)m.compact=false;
            previous=range.bottom;
        }
        if(m.compact)m.height=int(m.ranges.size()-1)*m.advance+line;
    }
    return m;
}
void paint_live(odh::RasterFrame& frame,std::string_view value,std::uint8_t language,
                const odh::UiBitmapRect& box,int scroll,float requested_scale){
    if(value.empty()||box.width<=0||box.height<=0)return;
    const auto metrics=live_metrics(value,language,box.width,requested_scale);
    // Uniform observed Latin lines use the original17px placement. Mixed
    // fallback heights, or an alternate backend without range support, retain
    // the complete natural13px document instead of inventing line boundaries.
    if(!metrics.compact){
        TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        style.padding=0;style.font_size_px=metrics.font;style.wrap_lines=true;
        if(scroll)style.pixel_scroll_y=scroll;
        style.background={0,0,0,0};style.foreground={195,205,205,255};
        (void)draw_text_overlay(frame,value,language,style);return;
    }
    for(std::size_t row=0;row<metrics.ranges.size();++row){
        const auto& range=metrics.ranges[row];const int line=range.bottom-range.top;
        const int top=box.y+int(row)*metrics.advance-scroll;
        if(top>=box.y+box.height)break;
        const int clipped_top=std::max(top,box.y),bottom=std::min(top+line,box.y+box.height);
        if(bottom<=clipped_top)continue;
        TextOverlayStyle style;style.x=box.x;style.y=clipped_top;style.width=box.width;style.height=bottom-clipped_top;
        style.padding=0;style.font_size_px=metrics.font;style.wrap_lines=true;
        style.pixel_scroll_y=range.top+clipped_top-top;
        style.background={0,0,0,0};style.foreground={195,205,205,255};
        (void)draw_text_overlay(frame,value,language,style);
    }
}
}
CatalogInfoTextPainter catalog_info_text_painter(std::uint8_t language,bool text_only_live,float live_scale){
    return [language,text_only_live,live_scale](odh::RasterFrame& frame,std::string_view value,const odh::UiBitmapRect& box,bool effect){
        if(value.empty()||box.width<=0||box.height<=0)return;
        if(text_only_live){paint_live(frame,value,language,box,0,live_scale);return;}
        TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        style.padding=0;style.font_size_px=font_for(original_control_panel_layout(frame.width,frame.height).scale);
        style.wrap_lines=true;style.background={0,0,0,0};
        style.foreground=effect?std::array<std::uint8_t,4>{0,255,0,255}:std::array<std::uint8_t,4>{245,245,235,255};
        (void)draw_text_overlay(frame,value,language,style);
    };
}
CatalogInfoHeadingPainter catalog_info_heading_painter(std::uint8_t language,bool text_only_live,float live_scale){
    return [language,text_only_live,live_scale](odh::RasterFrame& frame,std::string_view name,std::string_view price,
            const odh::UiBitmapRect& name_cell,const odh::UiBitmapRect& price_cell){
        const auto scale=text_only_live&&live_scale>0?live_scale:original_control_panel_layout(frame.width,frame.height).scale;
        const int font=font_for(scale,text_only_live),right=price_cell.x+price_cell.width;
        const int available=std::max(0,right-name_cell.x);
        const int price_width=price.empty()?0:std::clamp(measure_text_overlay(price,language,font).width,0,available);
        const int gap=price.empty()?0:std::clamp(measure_text_overlay("   ",language,font).width,0,available-price_width);
        auto title=name_cell;
        title.width=std::clamp(measure_text_overlay(name,language,font).width,0,available-price_width-gap);
        const auto paint=[&](std::string_view value,const odh::UiBitmapRect& box,bool right_aligned){
            if(value.empty()||box.width<=0||box.height<=0)return;
            TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
            style.padding=0;style.font_size_px=font;style.wrap_lines=false;style.right_aligned=right_aligned;
            style.background={0,0,0,0};style.foreground=text_only_live?std::array<std::uint8_t,4>{195,205,205,255}:std::array<std::uint8_t,4>{245,245,235,255};
            (void)draw_text_overlay(frame,value,language,style);
        };
        paint(name,title,false);
        // Original header puts the price after the measured title plus three
        // spaces. Only an overflowing name consumes the reserved title cell.
        paint(price,{title.x+title.width+gap,price_cell.y,price_width,price_cell.height},false);
    };
}
CatalogInfoScrollPainter catalog_info_scroll_painter(std::uint8_t language,bool text_only_live,float live_scale){
    return [language,text_only_live,live_scale](odh::RasterFrame& frame,std::string_view value,const odh::UiBitmapRect& box,bool effect,int offset){
        if(value.empty()||box.width<=0||box.height<=0)return;
        if(text_only_live){paint_live(frame,value,language,box,offset,live_scale);return;}
        TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        style.padding=0;style.font_size_px=font_for(std::min(1.f,float(box.width)/373));
        style.wrap_lines=true;style.pixel_scroll_y=offset;style.background={0,0,0,0};
        style.foreground=effect?std::array<std::uint8_t,4>{0,255,0,255}:std::array<std::uint8_t,4>{245,245,235,255};
        (void)draw_text_overlay(frame,value,language,style);
    };
}
CatalogInfoTextMeasure catalog_info_text_measure(std::uint8_t language,bool text_only_live,float live_scale){
    return [language,text_only_live,live_scale](std::string_view value,int width){
        if(text_only_live){const auto m=live_metrics(value,language,width,live_scale);return m.height;}
        return measure_text_overlay(value,language,font_for(std::min(1.f,float(width)/373)),width).height;
    };
}
}
