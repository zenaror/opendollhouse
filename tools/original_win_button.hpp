// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ui_bitmap.hpp"
#include <array>
#include <string_view>
namespace odh_host {
// Actual original WinBtn frame: two unscaled 32px ends and one repeatable
// centre column. The original 100x33 Quit control matches this mapping.
// These coordinates say nothing about caption measurement or control width.
struct OriginalWinButtonPiece { int x,width,source_x,source_width; };
inline std::array<OriginalWinButtonPiece,3> original_win_button_pieces(int width) {
    return {{{0,32,0,32},{32,width-64,32,1},{width-32,32,33,32}}};
}
inline int original_win_button_source_x(int x,int width) {
    return x<32?x:x>=width-32?x-(width-65):32;
}
inline bool original_win_button_key(std::string_view key) {
    constexpr std::string_view expected="shared/sys/winbtn.bmp";
    if(key.size()!=expected.size())return false;
    for(size_t i=0;i<key.size();++i){char c=key[i];if(c=='\\')c='/';if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(c!=expected[i])return false;}
    return true;
}
// False retains the caller's existing compact/non-native scaling policy.
// Only the measured native shape is admitted; other art is never guessed.
inline bool draw_original_win_button(odh::RasterFrame& frame,const odh::BitmapImage& strip,
        odh::UiBitmapRect box,unsigned state) {
    if(strip.width!=260||strip.height!=33||box.width<65||box.height!=33||state>=4)return false;
    for(const auto& part:original_win_button_pieces(box.width))
        odh::blit_color_keyed_bitmap(frame,strip,{box.x+part.x,box.y,part.width,33},
            odh::UiBitmapRect{int(state)*65+part.source_x,0,part.source_width,33},{255,0,255});
    return true;
}
} // namespace odh_host
