#pragma once
#include "odh/raster.hpp"
#include "odh/ts1_simulation.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
namespace odh_host {
struct StatusHud {
    std::int16_t actor=0;
    std::optional<odh::Ts1ClockSnapshot> clock;
    std::optional<std::array<std::int16_t,odh::ts1_need_count>> needs;
    std::optional<std::int16_t> room_score;
};
// Numeric, font-independent diagnostic HUD. Rows 1..7 follow Ts1Need order;
// row 8 displays the separately stored TS1 Room motive.
// Writes only RGBA: depth, owner IDs and separate picking maps stay intact.
inline void draw_status_hud(odh::RasterFrame& frame,const StatusHud& state) {
    if(frame.width<160||frame.height<112||frame.pixels.size()!=std::size_t(frame.width)*frame.height)
        throw std::invalid_argument("status HUD requires a valid frame of at least 160x112");
    using Color=std::array<std::uint8_t,4>;
    const Color ink{240,245,250,255},background{18,26,38,255};
    const auto rect=[&](unsigned x,unsigned y,unsigned w,unsigned h,Color color){
        for(unsigned row=y;row<y+h;++row)for(unsigned col=x;col<x+w;++col)
            frame.pixels[std::size_t(row)*frame.width+col]=color;
    };
    const auto number=[&](const std::string& text,unsigned x,unsigned y,unsigned scale){
        constexpr std::array<unsigned,10> glyphs{0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,
                                                0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
        for(char ch:text){
            unsigned bits=ch>='0'&&ch<='9'?glyphs[static_cast<unsigned>(ch-'0')]:
                ch==':'?0x0410U:ch=='-'?0x01c0U:0U;
            for(unsigned row=0;row<5;++row)for(unsigned col=0;col<3;++col)
                if(bits&(1U<<(14U-row*3U-col)))rect(x+col*scale,y+row*scale,scale,scale,ink);
            x+=4*scale;
        }
    };
    rect(4,4,152,108,background);
    rect(8,9,8,8,ink);rect(9,10,6,6,background);
    rect(12,11,1,3,ink);rect(12,13,2,1,ink);
    if(state.clock){
        const auto& c=*state.clock;
        number((c.hour<10?"0":"")+std::to_string(c.hour)+":"+
               (c.minute<10?"0":"")+std::to_string(c.minute),22,9,2);
    }else number("--:--",22,9,2);
    number(std::to_string(state.actor),116,11,1);
    const auto draw_row=[&](unsigned row,std::optional<std::int16_t> value){
        const unsigned y=28+row*10;
        number(std::to_string(row+1),9,y,1);
        number(value?std::to_string(*value):"--",20,y,1);
        rect(50,y,100,6,{57,66,78,255});
        rect(99,y,1,6,{140,150,160,255});
        if(value){
            const auto width=static_cast<unsigned>((std::clamp<int>(*value,-100,100)+100)/2);
            if(width)rect(50,y,width,6,*value<0?Color{218,117,69,255}:Color{81,190,128,255});
        }
    };
    for(unsigned row=0;row<odh::ts1_need_count;++row)
        draw_row(row,state.needs?std::optional<std::int16_t>{(*state.needs)[row]}:std::nullopt);
    draw_row(7,state.room_score);
}
} // namespace odh_host
