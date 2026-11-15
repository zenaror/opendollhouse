// SPDX-License-Identifier: MPL-2.0
// Extracted from show_language_picker: the existing first-run painting now
// serves both its original LinuxWindow wrapper and the native SDL window.
#include "language_picker.hpp"
#include "linux_text_renderer.hpp"
#include <limits>
#include <stdexcept>
namespace odh_host {
odh::RasterFrame LanguagePicker::draw(int width,int height,bool ascii,bool text,std::string_view notice,std::string_view instructions) const {
    if(width<800 || height<600 || width>3840 || height>2160)throw std::invalid_argument("language picker canvas outside supported bounds");
    odh::RasterFrame frame{};frame.width=width;frame.height=height;
    frame.pixels.assign(std::size_t(width)*height,{35,39,48,255});
    frame.depth.assign(frame.pixels.size(),std::numeric_limits<float>::infinity());
    for(int index=0;index<20;++index){
        const auto color=index==selected_?std::array<std::uint8_t,4>{47,93,138,255}:std::array<std::uint8_t,4>{52,57,68,255};
        for(int y=56+index*25;y<56+index*25+22;++y)
            for(int x=24;x<width-24;++x)frame.pixels[std::size_t(y)*width+x]=color;
    }
    if(!text)return frame;
    TextOverlayStyle style;style.x=24;style.width=width-48;style.height=24;style.font_size_px=16;style.padding=4;
    style.foreground={245,245,245,255};style.background={22,25,32,255};style.y=18;
    if(instructions.empty())instructions="Choose game language - Tab: next, Shift+Tab: previous, Enter or click: confirm, Esc: cancel";
    draw_text_overlay(frame,instructions,language(),style);
    for(int index=0;index<20;++index){
        style.y=56+index*25;
        draw_text_overlay(frame,std::string(index==selected_?"> ":"  ")+std::to_string(index+1)+"  "+
            (ascii?game_languages[index].ascii_name:game_languages[index].name),language(),style);
    }
    style.y=height-32;
    draw_text_overlay(frame,notice.empty()?"Missing translations use English. Player names are unchanged.":notice,language(),style);
    return frame;
}
}
