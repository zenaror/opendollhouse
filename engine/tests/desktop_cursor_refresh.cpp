/* SPDX-License-Identifier: MPL-2.0 */
#include "app/ui/DesktopCursorRefresh.h"
#include <stdexcept>
#include <iostream>
static void require(bool ok){if(!ok)throw std::runtime_error("contract failed");}
int main(){
    using simapp::original::DesktopCursorRefresh;
    const std::array<double,8> camera{};
    const std::array<std::uintptr_t,4> identity{100,1,200,2};
    DesktopCursorRefresh moving,stationary,animated;
    int movingPicks=0,stillPicks=0,animatedPicks=0,checks=0;
    for(int i=0;i<120;++i){
        const double now=i*(1000./120.);
        movingPicks+=moving.PickDue(now,i,20,camera,0);
        checks+=moving.EligibilityDue(now,identity);
        stillPicks+=stationary.PickDue(now,10,20,camera,0);
        animatedPicks+=animated.PickDue(now,10,20,camera,i);
    }
    require(movingPicks<=30 && movingPicks>=24);
    require(stillPicks==5 && checks==5);
    require(animatedPicks<=30);
    require(moving.EligibilityDue(1000,{101,1,200,2})); // numeric ID reused
    require(moving.EligibilityDue(1001,{101,1,201,2})); // selected actor changed
    require(!moving.EligibilityDue(1002,{101,1,201,2}));
    require(moving.EligibilityDue(1201,{101,1,201,2}));
    DesktopCursorRefresh view;
    require(view.PickDue(0,10,20,camera,0));
    auto rotated=camera;rotated[3]=1;
    require(!view.PickDue(10,10,20,rotated,0)); // all dirty paths obey budget
    require(view.PickDue(34,10,20,rotated,0));
    require(!view.PickDue(35,10,20,rotated,0));
    require(view.PickDue(234,10,20,rotated,0)); // stationary TTL
    // Recycled ID mapping must stay neutral until the budget admits a real
    // repick; the replacement cannot inherit the old hit's authority.
    simapp::original::DesktopCursorPickIdentity picked;
    int oldEntity=1,replacement=2;
    picked.Capture(7,&oldEntity);
    require(picked.ID()==7 && picked.Matches(&oldEntity));
    require(!picked.Matches(&replacement) && !picked.Matches(nullptr));
    picked.Clear();view.InvalidatePick();
    require(picked.ID()==0 && picked.Matches(nullptr));
    require(!view.PickDue(240,10,20,rotated,0));
    require(view.PickDue(268,10,20,rotated,0));
    picked.Capture(7,&replacement);
    require(picked.Matches(&replacement) && !picked.Matches(&oldEntity));
    std::cout<<"cursor refresh budgets passed: moving="<<movingPicks<<" stationary="<<stillPicks
        <<" eligibility="<<checks<<" animated="<<animatedPicks<<"\n";
}
