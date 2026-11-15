/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include "app/Input.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

namespace simapp::original {
struct DesktopCameraMotion {
    float horizontal=0,vertical=0;
    bool diagonal_edge=false,right_drag=false;
    std::array<int,2> cursor{};
};
// Presentation capture only. LotScreen supplies its actual canvas/HUD/modal
// authority and recovered edge classification; no VM, geometry or pointer copy.
class DesktopCamera {
public:
    void Cancel(){anchor_.reset();blocked_=true;}
    std::optional<std::array<int,2>> Anchor()const{return blocked_?std::nullopt:anchor_;}
    DesktopCameraMotion Update(const AppInput& in,bool canvas,bool modal,bool edges,
                              std::array<int,2> edge,double dtMs){
        DesktopCameraMotion out;
        if(!in.DesktopPointer || in.PointerCancelled || modal || !canvas || in.Touching || in.TouchDown){Cancel();return out;}
        if(in.MouseRightUp){anchor_.reset();blocked_=false;return out;}
        if(in.MouseRightDown){anchor_=std::array<int,2>{in.TouchX,in.TouchY};blocked_=false;}
        const float frames=std::isfinite(dtMs)?float(std::clamp(dtMs,0.,250.)*.06):0.f;
        if(in.MouseRightHeld || in.MouseRightDown){
            if(blocked_ || !anchor_)return out;
            const int dx=in.TouchX-(*anchor_)[0],dy=in.TouchY-(*anchor_)[1];
            out.horizontal=dx*.0005f*frames;out.vertical=dy*.0005f*frames;out.right_drag=true;
            // Pinned Simitone UILotControl: angle0 is Up, then clockwise.
            const int direction=(int(std::lround(std::atan2(double(dx),double(-dy)*2.)*4./3.141592653589793))+8)%8;
            constexpr std::array<std::array<int,2>,8> directions{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
            out.cursor=directions[direction];return out;
        }
        anchor_.reset();blocked_=false;
        if(edges){out.cursor=edge;out.horizontal=edge[0]*.0625f*frames;out.vertical=edge[1]*.0625f*frames;
            out.diagonal_edge=edge[0] && edge[1];}
        return out;
    }
private:
    std::optional<std::array<int,2>> anchor_;
    bool blocked_=false;
};
}
