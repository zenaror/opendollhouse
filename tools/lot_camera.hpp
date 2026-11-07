#pragma once
#include "odh/projection.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace odh_host {
// Simitone desktop camera. Distances are expressed in the rotated 2D scroll
// basis from World.GetScrollBasis; moving the view never moves Sims or objects.
inline bool scroll_lot_camera(odh::WorldProjection& view,unsigned width,unsigned height,
                              float horizontal,float vertical,bool diagonal_edge=false){
    if((!horizontal&&!vertical)||!width||!height||view.rotation>3||view.zoom<1||view.zoom>3||
       !std::isfinite(horizontal)||!std::isfinite(vertical))return false;
    const float scale=static_cast<float>(((1<<(3-view.zoom))*3)/2)*.70710678118f;
    float x=(horizontal+2*vertical)*scale,y=(-horizontal+2*vertical)*scale;
    const std::array<std::array<float,2>,4> delta{{{x,y},{y,-x},{-x,-y},{-y,x}}};
    const auto& step=delta[view.rotation];
    const auto next_x=std::clamp(view.center_tile[0]+step[0],0.f,static_cast<float>(width-1));
    const auto next_y=std::clamp(view.center_tile[1]+step[1]*(diagonal_edge?.5f:1.f),0.f,static_cast<float>(height-1));
    if(next_x==view.center_tile[0]&&next_y==view.center_tile[1])return false;
    view.center_tile[0]=next_x;view.center_tile[1]=next_y;return true;
}
inline std::array<int,2> lot_camera_edge(int x,int y,int width,int height){
    if(x<0||x>=width||y<0||y>=height)return {};
    return {x<20?-1:x>=width-20?1:0,y<20?-1:y>=height-20?1:0};
}
}
