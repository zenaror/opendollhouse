/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/DesktopPieHeadMotion.h"
#include <stdexcept>
#include <iostream>
static void require(bool ok){if(!ok)throw std::runtime_error("contract failed");}
int main(){
    using simapp::original::DesktopPieHeadMotion;
    const std::array<int,2> center{400,300},target{500,220};
    DesktopPieHeadMotion motion;
    require(motion.Update(center,target,0)==center);
    const auto first=motion.Update(center,target,1000./60.);
    require(first[0]>center[0] && first[0]<target[0]);
    require(first[1]<center[1] && first[1]>target[1]);
    DesktopPieHeadMotion sixty,thirty;
    std::array<int,2> a,b;
    for(int i=0;i<60;++i)a=sixty.Update(center,target,1000./60.);
    for(int i=0;i<30;++i)b=thirty.Update(center,target,1000./30.);
    require(a==b && a==target);
    motion.Reset();require(motion.Update(center,target,0)==center);
    const auto settled=sixty.Update(center,center,1000./60.);
    require(settled[0]>center[0] && settled[0]<target[0]);
    std::cout<<"desktop pie motion: rate equivalence, gradual turn/return, reopen reset passed\n";
}
