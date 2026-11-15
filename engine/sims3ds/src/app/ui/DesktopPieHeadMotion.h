/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace simapp::original {
// Simitone 4906db3 UIPieMenu.RotateHeadCam interpolates 1/8 of the
// remaining offset at 60 Hz. Real elapsed time keeps the same response
// at different presentation rates; this state never touches the live Sim.
class DesktopPieHeadMotion {
public:
    void Reset(){offset_={0,0};}
    std::array<int,2> Update(std::array<int,2> center,std::array<int,2> target,double dtMs){
        const double amount=std::isfinite(dtMs)?1.-std::pow(.875,std::clamp(dtMs,0.,250.)*.06):0.;
        for(size_t i=0;i<2;++i)offset_[i]+=(double(target[i])-center[i]-offset_[i])*amount;
        return {center[0]+int(std::lround(offset_[0])),center[1]+int(std::lround(offset_[1]))};
    }
private:
    std::array<double,2> offset_{};
};
}
