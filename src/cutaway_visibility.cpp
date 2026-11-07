// SPDX-License-Identifier: MPL-2.0
#include "odh/cutaway_visibility.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace odh {
std::uint8_t source_adjacent_wall(std::int16_t flags,std::uint8_t direction){
    if(direction>7)throw std::invalid_argument("cutaway direction is outside source notches");
    const unsigned quarter=(direction%2U)==0?direction/2U:0U;
    const unsigned rotate=(8U-(quarter+1U))%4U;
    // Source deliberately uses signed remainder modulo15, NOT bitwise &15.
    const auto used=static_cast<std::uint32_t>(static_cast<std::int32_t>(flags)%15);
    return static_cast<std::uint8_t>(((used<<(4U-rotate))&15U)|((used&15U)>>rotate));
}
std::optional<std::array<std::int32_t,2>> source_cutaway_tile(float x,float y) noexcept {
    const auto rounded=[](float value)->std::optional<std::int32_t>{
        if(!std::isfinite(value))return {};
        const double source=value,low=std::floor(source),part=source-low;
        const double result=part<0.5?low:part>0.5?low+1.0:
            std::fmod(std::abs(low),2.0)==0.0?low:low+1.0;
        if(result<std::numeric_limits<std::int32_t>::min()||result>std::numeric_limits<std::int32_t>::max())return {};
        return static_cast<std::int32_t>(result);
    };
    const auto rx=rounded(x),ry=rounded(y);if(!rx||!ry)return {};
    return std::array<std::int32_t,2>{*rx,*ry};
}
std::optional<bool> full_cutaway_hidden(const FullCutawayVisibilityInput& input) noexcept {
    if(!input.hide_for_cutaway||input.object_level<=0)return input.previous_hidden;
    if(input.object_level!=input.view_level||input.build_mode>1)return false;
    const auto tile=source_cutaway_tile(input.x,input.y);if(!tile)return {};
    const auto inside=[&](std::int64_t x,std::int64_t y){return x>=0&&y>=0&&
        static_cast<std::uint64_t>(x)<input.width&&static_cast<std::uint64_t>(y)<input.height;};
    if(!inside((*tile)[0],(*tile)[1]))return input.previous_hidden;
    if(!input.walls_down)return false; // Full walls-up Cutaway is all false.
    const auto offset=[](std::uint8_t mask)->std::optional<std::array<int,2>>{
        switch(mask){case 8:return std::array<int,2>{0,1};case 1:return std::array<int,2>{-1,0};
            case 2:return std::array<int,2>{0,-1};case 4:return std::array<int,2>{1,0};default:return {};}
    };
    if((input.adjacent_wall&&*input.adjacent_wall>15)||(input.occupied_walls&&*input.occupied_walls>15))return {};
    std::optional<bool> proved;
    for(unsigned a=0;a<16;++a){
        if(input.adjacent_wall&&a!=*input.adjacent_wall)continue;
        for(unsigned w=0;w<16;++w){
            if(input.occupied_walls&&w!=*input.occupied_walls)continue;
            auto step=offset(static_cast<std::uint8_t>(a&input.wall_segments));
            if(!step)step=offset(static_cast<std::uint8_t>(w&input.wall_segments));
            const auto delta=step.value_or(std::array<int,2>{0,0});
            const bool hidden=inside(static_cast<std::int64_t>((*tile)[0])+delta[0],static_cast<std::int64_t>((*tile)[1])+delta[1]);
            if(proved&&*proved!=hidden)return {};
            proved=hidden;
        }
    }
    return proved;
}
}
