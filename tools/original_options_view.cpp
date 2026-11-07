// SPDX-License-Identifier: MPL-2.0
#include "original_options_view.hpp"
#include <cmath>
namespace odh_host {
namespace {
constexpr const char* keys[]={"CPanel/Buttons/OptSave.bmp","CPanel/Buttons/OptNbhd.bmp",
    "CPanel/Buttons/OptExit.bmp","CPanel/Buttons/OptGraphics.bmp",
    "CPanel/Buttons/OptSound.bmp","CPanel/Buttons/OptPlay.bmp"};
}
OriginalOptionsLayout original_options_layout(std::uint32_t width,std::uint32_t height){
    const auto scale=original_control_panel_layout(width,height).scale;
    const auto rect=[&](int x,int y,int w,int h){return BuyUiRect{int(std::lround(x*scale)),
        int(height-std::lround((600-y)*scale)),int(std::lround(w*scale)),int(std::lround(h*scale))};};
    return {{{rect(250,506,39,39),rect(307,507,49,37),rect(379,506,50,40),
        rect(250,556,38,40),rect(307,556,47,40),rect(379,556,55,40)}},
        rect(236,551,201,2),rect(441,505,2,89)};
}
OriginalOptionsView::OriginalOptionsView(const std::filesystem::path& game):art_(game,
    {keys[0],keys[1],keys[2],keys[3],keys[4],keys[5],
     "CPanel/Backgrounds/OptionsToothpkHoriz.bmp","CPanel/Backgrounds/OptionsToothpkVert.bmp"}){}
std::optional<std::size_t> OriginalOptionsView::hit(const OriginalOptionsLayout& layout,int x,int y,
        const std::array<bool,6>& enabled)const{
    for(std::size_t i=0;i<layout.buttons.size();++i){
        const auto& box=layout.buttons[i];if(!enabled[i]||!box.contains(x,y))continue;
        const auto& image=art_.at(keys[i]);
        if(image.width%4||!image.height)throw odh::FormatError("invalid original option strip");
        // These separate rectangular controls have hollow line artwork.
        // Magenta inside the drawing is still part of the button, unlike
        // overlapping circular controls on the shared left panel.
        return i;
    }
    return {};
}
void OriginalOptionsView::draw(odh::RasterFrame& frame,const std::array<bool,6>& enabled,
        std::optional<std::array<int,2>> pointer,std::optional<std::size_t> pressed,std::optional<std::size_t> selected)const{
    const auto layout=original_options_layout(frame.width,frame.height);
    const auto hovered=pointer?hit(layout,(*pointer)[0],(*pointer)[1],enabled):std::nullopt;
    for(std::size_t i=0;i<layout.buttons.size();++i){
        const auto hover=hovered==i;
        const unsigned state=!enabled[i]?3:(pressed==i&&hover)||selected==i?1:hover?2:0;
        const auto& image=art_.at(keys[i]);
        if(image.width%4||!image.height)throw odh::FormatError("invalid original option strip");
        const int span=int(image.width/4);
        odh::blit_color_keyed_bitmap(frame,image,layout.buttons[i],odh::UiBitmapRect{int(state)*span,0,span,int(image.height)},{255,0,255});
    }
    odh::blit_color_keyed_bitmap(frame,art_.at("CPanel/Backgrounds/OptionsToothpkHoriz.bmp"),layout.horizontal_divider,std::nullopt,{255,0,255});
    odh::blit_color_keyed_bitmap(frame,art_.at("CPanel/Backgrounds/OptionsToothpkVert.bmp"),layout.vertical_divider,std::nullopt,{255,0,255});
}
}
