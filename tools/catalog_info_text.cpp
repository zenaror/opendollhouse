// SPDX-License-Identifier: MPL-2.0
#include "catalog_info_text.hpp"
#include "linux_text_renderer.hpp"
#include "buy_mode_view.hpp"
#include <algorithm>
#include <cmath>
namespace odh_host {
namespace {
int font_for(float scale){return std::max(8,int(std::lround(11*scale)));}
}
CatalogInfoTextPainter catalog_info_text_painter(std::uint8_t language){
    return [language](odh::RasterFrame& frame,std::string_view value,const odh::UiBitmapRect& box,bool effect){
        if(value.empty()||box.width<=0||box.height<=0)return;
        TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        style.padding=0;style.font_size_px=font_for(original_control_panel_layout(frame.width,frame.height).scale);
        style.wrap_lines=true;style.background={0,0,0,0};
        style.foreground=effect?std::array<std::uint8_t,4>{0,255,0,255}:std::array<std::uint8_t,4>{245,245,235,255};
        (void)draw_text_overlay(frame,value,language,style);
    };
}
CatalogInfoScrollPainter catalog_info_scroll_painter(std::uint8_t language){
    return [language](odh::RasterFrame& frame,std::string_view value,const odh::UiBitmapRect& box,bool effect,int offset){
        if(value.empty()||box.width<=0||box.height<=0)return;
        TextOverlayStyle style;style.x=box.x;style.y=box.y;style.width=box.width;style.height=box.height;
        style.padding=0;style.font_size_px=font_for(std::min(1.f,float(box.width)/373));
        style.wrap_lines=true;style.pixel_scroll_y=offset;style.background={0,0,0,0};
        style.foreground=effect?std::array<std::uint8_t,4>{0,255,0,255}:std::array<std::uint8_t,4>{245,245,235,255};
        (void)draw_text_overlay(frame,value,language,style);
    };
}
CatalogInfoTextMeasure catalog_info_text_measure(std::uint8_t language){
    return [language](std::string_view value,int width){
        return measure_text_overlay(value,language,font_for(std::min(1.f,float(width)/373)),width).height;
    };
}
}
