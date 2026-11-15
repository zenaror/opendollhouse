// SPDX-License-Identifier: MPL-2.0
#include "original_tooltip_view.hpp"
#include "linux_text_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace odh_host {
namespace {
int font_size(float scale){return std::max(1,int(std::lround(10*scale)));}
int padding(float scale){return std::max(1,int(std::lround(5*scale)));}
int border(float scale){return std::max(1,int(std::lround(scale)));}
}
std::optional<odh::UiBitmapRect> original_tooltip_layout(std::uint32_t width,std::uint32_t height,
        std::string_view name,std::uint8_t language,std::array<std::int32_t,2> pointer,float scale){
    if(!width||!height||width>8192||height>8192||
       !std::isfinite(scale)||scale<=0||scale>4||language<1||language>20)
        throw std::invalid_argument("original tooltip viewport/scale/language invalid");
    if(name.empty()||name.size()>4096||name.find_first_of("\r\n")!=std::string_view::npos||name.find('\0')!=std::string_view::npos)return {};
    const auto pad=padding(scale),edge=border(scale);
    const auto maximum=std::min(int(width),300);
    if(maximum<=pad*2||int(height)<=edge*2)return {};
    const auto natural=measure_text_overlay(name,language,font_size(scale));
    const auto box_width=std::min(maximum,natural.width+pad*2);
    const auto extent=measure_text_overlay(name,language,font_size(scale),box_width-pad*2);
    const auto box_height=extent.height+edge*2;
    if(box_height>int(height)||box_width-pad*2<2||extent.height<2)return {};
    return position_original_tooltip(width,height,box_width,box_height,pointer,scale);
}
odh::UiBitmapRect position_original_tooltip(std::uint32_t width,std::uint32_t height,
        int box_width,int box_height,std::array<std::int32_t,2> pointer,float scale){
    if(box_width<1||box_height<1||std::uint32_t(box_width)>width||std::uint32_t(box_height)>height||
       width>8192||height>8192||!std::isfinite(scale)||scale<=0||scale>4)
        throw std::invalid_argument("original tooltip box outside viewport");
    const auto gap=std::max(1,int(std::lround(14*scale)));
    const auto x=std::clamp<std::int64_t>(pointer[0],0,std::int64_t(width)-box_width);
    const auto y=std::clamp<std::int64_t>(std::int64_t(pointer[1])-gap-box_height,0,std::int64_t(height)-box_height);
    return odh::UiBitmapRect{int(x),int(y),box_width,box_height};
}
std::optional<odh::UiBitmapRect> draw_original_tooltip(odh::RasterFrame& frame,std::string_view name,
        std::uint8_t language,std::array<std::int32_t,2> pointer,float scale,std::array<std::uint8_t,4> foreground){
    if(frame.pixels.size()!=std::uint64_t(frame.width)*frame.height)
        throw std::invalid_argument("original tooltip destination pixels invalid");
    const auto box=original_tooltip_layout(frame.width,frame.height,name,language,pointer,scale);
    if(!box)return {};
    const auto edge=border(scale),pad=padding(scale);
    for(int y=0;y<box->height;++y)for(int x=0;x<box->width;++x){
        const bool outline=x<edge||y<edge||x>=box->width-edge||y>=box->height-edge;
        frame.pixels[std::size_t(box->y+y)*frame.width+box->x+x]=outline?
            std::array<std::uint8_t,4>{0,0,0,255}:std::array<std::uint8_t,4>{255,255,230,255};
    }
    TextOverlayStyle style;style.x=box->x+pad;style.y=box->y+edge;
    style.width=box->width-pad*2;style.height=box->height-edge*2;style.padding=0;
    style.font_size_px=font_size(scale);style.wrap_lines=true;
    style.foreground=foreground;style.background={0,0,0,0};
    (void)draw_text_overlay(frame,name,language,style);
    return box;
}
}
