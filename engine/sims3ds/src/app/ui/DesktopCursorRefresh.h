/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#include <array>
#include <cstdint>

namespace simapp::original {
// Only scheduling state: no renderer, VM or extra per-pixel storage. Picking
// and TTAB eligibility have independent invalidation/budgets.
class DesktopCursorRefresh {
public:
    void InvalidatePick(){invalidated_=true;}
    bool PickDue(double now,int x,int y,const std::array<double,8>& camera,std::uint64_t scene){
        const auto age=now-pickedAt_;
        const bool dirty=invalidated_ || x!=x_ || y!=y_ || camera!=camera_ || scene!=scene_;
        if(picked_ && age>=0 && age<200 && (!dirty || age<1000./30.))return false;
        picked_=true;invalidated_=false;pickedAt_=now;x_=x;y_=y;camera_=camera;scene_=scene;
        return true;
    }
    bool EligibilityDue(double now,const std::array<std::uintptr_t,4>& identity){
        const auto age=now-checkedAt_;
        if(checked_ && identity==identity_ && age>=0 && age<200)return false;
        checked_=true;checkedAt_=now;identity_=identity;return true;
    }
private:
    bool picked_=false,checked_=false,invalidated_=false;
    double pickedAt_=0,checkedAt_=0;
    int x_=0,y_=0;
    std::uint64_t scene_=0;
    std::array<double,8> camera_{};
    std::array<std::uintptr_t,4> identity_{};
};
// A numeric object ID alone can be recycled. Retain the exact mapping returned
// by the pick, and compare it before any target access or eligibility query.
class DesktopCursorPickIdentity {
public:
    void Capture(std::int16_t id,const void* pointer){id_=id;pointer_=pointer;}
    void Clear(){Capture(0,nullptr);}
    std::int16_t ID()const{return id_;}
    bool Matches(const void* current)const{return current==pointer_;}
private:
    std::int16_t id_=0;
    const void* pointer_=nullptr;
};
}
