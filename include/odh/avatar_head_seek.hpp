// SPDX-License-Identifier: MPL-2.0
// Source: FreeSO7e VMAvatar.UpdateHeadSeek/ApplyHeadSeek, SimAvatar.SlideHeadToTarget.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace odh {
using HeadSeekQuaternion=std::array<float,4>;
struct AvatarHeadSeekSnapshot {
    std::int16_t actor=0;std::uint64_t actor_generation=0,vm_generation=0;
    float weight=0,last_fraction=0;
    HeadSeekQuaternion current{0,0,0,1},target{0,0,0,1};
    std::optional<std::uint64_t> last_tick;
    bool operator==(const AvatarHeadSeekSnapshot& b)const{return actor==b.actor&&actor_generation==b.actor_generation&&
        vm_generation==b.vm_generation&&weight==b.weight&&last_fraction==b.last_fraction&&current==b.current&&target==b.target&&last_tick==b.last_tick;}
    bool operator!=(const AvatarHeadSeekSnapshot& b)const{return !(*this==b);}
};
struct AvatarHeadSeekVisualInput {
    std::int16_t actor=0,target=0;std::uint64_t actor_generation=0,target_generation=0,vm_generation=0,tick=0;
    HeadSeekQuaternion quaternion{0,0,0,1};
};
inline bool head_seek_valid(const AvatarHeadSeekSnapshot& s){
    if(s.actor<=0||!s.actor_generation||!s.vm_generation||!std::isfinite(s.weight)||s.weight<0||s.weight>15||
       !std::isfinite(s.last_fraction)||s.last_fraction<0||s.last_fraction>1)return false;
    for(const auto* q:{&s.current,&s.target}){float length=0;for(float v:*q){if(!std::isfinite(v))return false;length+=v*v;}
        if(!std::isfinite(length)||std::abs(length-1)>0.001f)return false;}
    return true;
}
inline HeadSeekQuaternion head_seek_slerp(const HeadSeekQuaternion& a,const HeadSeekQuaternion& b,float t){
    float dot=0;for(unsigned i=0;i<4;++i)dot+=a[i]*b[i];const bool opposite=dot<0;if(opposite)dot=-dot;
    float x=1-t,y=t;if(dot<=0.999999f){const float angle=std::acos(std::clamp(dot,-1.f,1.f));
        const float reciprocal=1/std::sin(angle);x=std::sin((1-t)*angle)*reciprocal;y=std::sin(t*angle)*reciprocal;}
    if(opposite)y=-y;HeadSeekQuaternion q{};for(unsigned i=0;i<4;++i)q[i]=x*a[i]+y*b[i];return q;
}
inline void slide_head_seek(AvatarHeadSeekSnapshot& s,float frames){
    if(s.current==s.target)return;float dot=0;for(unsigned i=0;i<4;++i)dot+=s.current[i]*s.target[i];
    const float distance=static_cast<float>(std::acos(double(2*(dot*dot)-1)));
    if(distance==0||std::isnan(distance))return;
    s.current=head_seek_slerp(s.current,s.target,std::min((0.2f*frames)/distance,1.f));
}
// No resources, mesh, VM dispatch or clock advancement. The graphics provider
// supplies a real quaternion only when rendering is enabled; headless follows
// exactly the source's UseWorld=false path for simulation weight/person banks.
inline void step_head_seek(AvatarHeadSeekSnapshot& s,std::int16_t& state,std::int16_t& timeout,
    std::int16_t& finish,bool target_present,const std::optional<HeadSeekQuaternion>& visual_target){
    if(state==1&&target_present){
        if(visual_target){s.target=*visual_target;if(s.weight==0)s.current=s.target;
            else{slide_head_seek(s,1-s.last_fraction);s.last_fraction=0;}}
        s.weight=std::min(15.f,s.weight+1);
        if(timeout>0&&--timeout==0)state=4;
    }else if(state==4||(state==1&&!target_present)){
        slide_head_seek(s,1-s.last_fraction);s.last_fraction=0;s.weight-=1;
        if(s.weight<=0){s.weight=0;state=8;finish=0;}
    }
}
} // namespace odh
