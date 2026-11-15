#include "original_win_button.hpp"
// SPDX-License-Identifier: MPL-2.0
#include "original_name_prompt.hpp"
#include "original_ui_art.hpp"
#include "linux_cas_layout.hpp"
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
#include "linux_text_renderer.hpp"
#endif
#include <algorithm>
#include <stdexcept>
namespace odh_host {
namespace {
using Color=std::array<std::uint8_t,4>;
void fill(odh::RasterFrame& frame,odh::UiBitmapRect box,Color color){
    const auto x0=std::clamp(box.x,0,int(frame.width)),y0=std::clamp(box.y,0,int(frame.height));
    const auto x1=std::clamp(box.x+box.width,0,int(frame.width)),y1=std::clamp(box.y+box.height,0,int(frame.height));
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)frame.pixels[std::size_t(y)*frame.width+x]=color;
}
void panel(odh::RasterFrame& frame,odh::UiBitmapRect box,const odh::BitmapImage* image){
    if(!image){fill(frame,box,{0,0,74,255});return;}
    if(image->width!=36||image->height!=36)throw odh::FormatError("original popup tile atlas must be 36 by 36");
    constexpr int side=12;
    const int xs[]={box.x,box.x+side,box.x+box.width-side,box.x+box.width};
    const int ys[]={box.y,box.y+side,box.y+box.height-side,box.y+box.height};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)
        for(int y=ys[row];y<ys[row+1];y+=side)for(int x=xs[col];x<xs[col+1];x+=side){
            const int w=std::min(side,xs[col+1]-x),h=std::min(side,ys[row+1]-y);
            odh::blit_color_keyed_bitmap(frame,*image,{x,y,w,h},odh::UiBitmapRect{col*side,row*side,w,h},{255,0,255});
        }
}
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
void text(odh::RasterFrame& frame,std::string_view value,odh::UiBitmapRect box,std::uint8_t language,
        bool centered=false,Color color={255,250,231,255},int size=15,bool wrap=false){
    // An empty edit draft is valid presentation state. The text renderer only
    // accepts nonempty strings; the field backing and caret are painted below.
    if(value.empty())return;
    TextOverlayStyle s;s.x=box.x;s.y=box.y;s.width=box.width;s.height=box.height;s.padding=2;
    s.font_size_px=size;s.background={0,0,0,0};s.foreground=color;s.centered=centered;s.wrap_lines=wrap;
    (void)draw_text_overlay(frame,value,language,s);
}
#endif
}
OriginalNamePromptLayout original_name_prompt_layout(std::uint32_t width,std::uint32_t height){
    if(width<480||height<280||width>8192||height>8192)throw odh::FormatError("name prompt frame outside supported bounds");
    const int w=440,h=222,x=(int(width)-w)/2,y=(int(height)-h)/2;
    return {{x,y,w,h},{x+18,y+14,w-36,29},{x+20,y+48,w-40,54},
        {x+(w-255)/2,y+108,255,25},{{{x+72,y+167,112,33},{x+w-184,y+167,112,33}}}};
}
std::optional<std::size_t> original_name_prompt_target(const OriginalNamePromptLayout& l,int x,int y){
    if(l.field.contains(x,y))return 0;
    for(std::size_t i=0;i<l.buttons.size();++i)if(l.buttons[i].contains(x,y))return i+1;
    return {};
}
void draw_original_name_prompt(odh::RasterFrame& frame,const OriginalNamePromptLayout& l,const OriginalUiArt* art,
        std::string_view title,std::string_view message,std::string_view name,
        std::string_view accept,std::string_view cancel,std::uint8_t language,bool field_focused,
        bool select_all,std::optional<std::array<std::int32_t,2>> pointer,
        std::optional<std::size_t> pressed,std::optional<std::size_t> keyboard_button){
    const auto* tiles=art?art->find("CPanel/Backgrounds/PopupInfoTiles.bmp"):nullptr;
    const auto* buttons=art?art->find("Shared/Sys/WinBtn.bmp"):nullptr;
    const auto* field=art?art->find("Nbhd/DsgnFamBkg.bmp"):nullptr;
    panel(frame,l.panel,tiles);
    if(field){
        if(field->width!=800||field->height!=600)throw odh::FormatError("invalid original family editor background");
        // Exact name-entry backing from cWinDesignFamily's (274,50,255,25).
        odh::blit_color_keyed_bitmap(frame,*field,l.field,odh::UiBitmapRect{274,50,255,25},{255,0,255});
    }else fill(frame,l.field,{27,31,35,255});
    const auto hover=pointer?original_name_prompt_target(l,(*pointer)[0],(*pointer)[1]):std::nullopt;
    for(std::size_t i=0;i<l.buttons.size();++i){
        const unsigned state=hover==i+1&&pressed==i+1?1:hover==i+1?2:0;
        if(buttons){
            if(buttons->width!=260||buttons->height!=33)throw odh::FormatError("invalid original WinBtn strip");
            if(!draw_original_win_button(frame,*buttons,l.buttons[i],state))
                odh::blit_color_keyed_bitmap(frame,*buttons,l.buttons[i],odh::UiBitmapRect{int(state)*65,0,65,33},{255,0,255});
        }else fill(frame,l.buttons[i],state==1?Color{20,90,150,255}:Color{10,60,120,255});
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
        text(frame,i?cancel:accept,l.buttons[i],language,true,keyboard_button==i?Color{255,255,0,255}:Color{255,250,231,255},14);
#endif
    }
#ifdef ODH_HAVE_LINUX_TEXT_RENDERER
    text(frame,title,l.title,language,true);
    text(frame,message,l.message,language,true,{255,250,231,255},14,true);
    std::string_view shown=name;
    // Keep the insertion point visible without splitting a UTF-8 character.
    while(!select_all&&!shown.empty()&&measure_text_overlay(shown,language,15).width>l.field.width-12){
        std::size_t next=1;while(next<shown.size()&&(static_cast<unsigned char>(shown[next])&0xc0U)==0x80U)++next;
        shown.remove_prefix(next);
    }
    if(field_focused&&select_all&&!shown.empty())fill(frame,{l.field.x+4,l.field.y+3,
        std::min(l.field.width-8,measure_text_overlay(shown,language,15).width+2),l.field.height-6},{31,83,151,255});
    text(frame,shown,{l.field.x+3,l.field.y,l.field.width-6,l.field.height},language,false,cas_input_color);
    if(field_focused&&!select_all){
        const auto caret=cas_edit_caret(l.field,shown.empty()?0:measure_text_overlay(shown,language,15).width,5);
        fill(frame,caret,{255,255,255,255});
    }
#else
    (void)title;(void)message;(void)name;(void)accept;(void)cancel;(void)language;
    (void)field_focused;(void)select_all;(void)keyboard_button;
#endif
}
}
