// SPDX-License-Identifier: MPL-2.0
// Portable C++ port of the WALKING/AdvanceWaypoint arithmetic in FreeSO
// TSOClient/tso.simantics/Engine/VMRoutingFrame.cs:746-960,1181-1209 at
// 7e76c1960252ddd033d8937d5edb47d326f5b69c. Source cache reconstruction:
// Marshals/Threads/VMRoutingFrameMarshal.cs:149-151; angles: DirectionUtils.cs.
// Original FreeSO source is available under Mozilla Public License 2.0,
// https://www.mozilla.org/MPL/2.0/ . Source is retained read-only in refs/work.
// This component deliberately leaves World placement and collision lifecycle
// external. It does not execute the commented-out TurnFrames interpolation.
#include "odh/walk_motion.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace odh {
namespace {
constexpr double pi=3.141592653589793238462643383279502884;
float product(float a,float b){volatile float value=a*b;return value;}
float quotient(float a,float b){volatile float value=a/b;return value;}
double normalize(double angle){
    const auto value=std::fmod(std::fmod(angle,2*pi)+2*pi,2*pi);
    return value>pi?value-2*pi:value;
}
WalkMotionStatus status(WalkPathStatus value){
    switch(value){case WalkPathStatus::success:return WalkMotionStatus::success;
        case WalkPathStatus::invalid_input:return WalkMotionStatus::invalid_input;
        case WalkPathStatus::unsupported_arithmetic:return WalkMotionStatus::unsupported_arithmetic;
        case WalkPathStatus::budget_exhausted:return WalkMotionStatus::budget_exhausted;}
    return WalkMotionStatus::invalid_input;
}
bool heading_fits(double value){return std::isfinite(value)&&value>=-static_cast<double>(std::numeric_limits<float>::max())&&
    value<=static_cast<double>(std::numeric_limits<float>::max());}
bool valid_pose(const WalkMotionPose& pose){return pose.position.x>=INT16_MIN&&pose.position.x<=INT16_MAX&&
    pose.position.y>=INT16_MIN&&pose.position.y<=INT16_MAX&&pose.level>=INT8_MIN&&pose.level<=INT8_MAX&&std::isfinite(pose.radians);}
bool same_float(float a,float b){std::uint32_t x=0,y=0;std::memcpy(&x,&a,sizeof(x));std::memcpy(&y,&b,sizeof(y));return x==y;}
bool same_vector(WalkVector a,WalkVector b){return same_float(a.x,b.x)&&same_float(a.y,b.y);}
bool same_step(const WalkMotionStepData& a,const WalkMotionStepData& b){
    if(a.level!=b.level||a.velocity!=b.velocity||a.move_frames!=b.move_frames||a.portal_turn_queried!=b.portal_turn_queried||
       a.sample.status!=b.sample.status||a.sample.position.x!=b.sample.position.x||a.sample.position.y!=b.sample.position.y||
       !same_vector(a.sample.visual,b.sample.visual)||!same_vector(a.sample.velocity,b.sample.velocity))return false;
    for(std::size_t n=0;n<a.animation_weights.size();++n)if(!same_float(a.animation_weights[n],b.animation_weights[n]))return false;
    return true;
}
bool valid_limit(const WalkMotionLimits& limits){return limits.segments>0&&limits.segments<=4096;}
bool usable_phase(WalkMotionPhase phase){return phase==WalkMotionPhase::walking||phase==WalkMotionPhase::collision_required||phase==WalkMotionPhase::finished;}
struct Waypoint {
    WalkMotionStatus status=WalkMotionStatus::invalid_input;
    std::optional<WalkPathSegment> segment;
    std::int32_t duration=0;
    double target=0;
};
Waypoint make_waypoint(const WalkSegmentGeometry& geometry,const WalkMotionPose& pose,
    const WalkMotionProfile& profile,bool walking){
    Waypoint result;if(!valid_pose(pose))return result;
    WalkPathSegment path(geometry);const auto calculated=path.calculate_total_frames();
    if(calculated.status!=WalkPathStatus::success){result.status=status(calculated.status);return result;}
    auto duration=std::int64_t(calculated.frames);
    if((profile.is_pet&&profile.person_type<254)||profile.in_pool)duration*=2;
    // The source WalkStyle getter always returns0 while in a pool.
    duration/=(profile.in_pool?0:profile.walk_style)==1?3:1;
    duration=std::max<std::int64_t>(1,duration);
    if(duration>INT32_MAX){result.status=WalkMotionStatus::unsupported_arithmetic;return result;}
    // Source multiplies before dividing; do not admit an overflowing *2 even
    // when an ensuing run /3 could bring it back within the signed int range.
    if(((profile.is_pet&&profile.person_type<254)||profile.in_pool)&&calculated.frames>INT32_MAX/2){
        result.status=WalkMotionStatus::unsupported_arithmetic;return result;
    }
    const auto dx=std::int64_t(geometry.d.x)-std::int64_t(pose.position.x)*walk_path_hip;
    const auto dy=std::int64_t(pose.position.y)*walk_path_hip-geometry.d.y;
    if(!walking&&(dx<INT32_MIN||dx>INT32_MAX||dy<INT32_MIN||dy>INT32_MAX)){
        result.status=WalkMotionStatus::unsupported_arithmetic;return result;
    }
    result.target=walking?static_cast<double>(pose.radians):std::atan2(static_cast<double>(dx),static_cast<double>(dy));
    result.duration=static_cast<std::int32_t>(duration);
    const auto updated=path.update_total_frames(result.duration);
    if(updated!=WalkPathStatus::success){result.status=status(updated);return result;}
    result.segment=path;result.status=WalkMotionStatus::success;return result;
}
WalkMotionStatus validate_geometry(const WalkSegmentGeometry& geometry){
    WalkPathSegment value(geometry);return status(value.calculate_total_frames().status);
}
}
struct PreparedWalkMotionStep::Lease {WalkMotionStepData data;bool active=true;};
PreparedWalkMotionStep::PreparedWalkMotionStep(const std::shared_ptr<Lease>& lease):lease_(lease),data_(lease->data){}
WalkMotionStatus WalkMotion::initialize(const std::vector<WalkSegmentGeometry>& path,
    const WalkMotionPose& pose,const WalkMotionProfile& profile,bool walking,std::int32_t velocity){
    if(!valid_limit(limits_)||pending_||state_.phase==WalkMotionPhase::awaiting_placement||!valid_pose(pose)||velocity<0||velocity>8)
        return WalkMotionStatus::invalid_input;
    if(path.size()>limits_.segments)return WalkMotionStatus::budget_exhausted;
    try{
        for(const auto& geometry:path){const auto checked=validate_geometry(geometry);if(checked!=WalkMotionStatus::success)return checked;}
        WalkMotionSnapshot prepared;prepared.previous_position=pose;prepared.velocity=velocity;
        prepared.remaining=path;prepared.phase=WalkMotionPhase::waypoint_required;
        std::optional<WalkPathSegment> sampler;
        if(!path.empty()){
            const auto next=make_waypoint(path.front(),pose,profile,walking);if(next.status!=WalkMotionStatus::success)return next.status;
            sampler=next.segment;prepared.current=path.front();prepared.remaining.erase(prepared.remaining.begin());
            prepared.move_total_frames=next.duration;prepared.turn_frames=std::min<std::int32_t>(10,next.duration);
            prepared.walk_direction=pose.radians;prepared.target_direction=next.target;prepared.phase=WalkMotionPhase::walking;
        }else{prepared.move_total_frames=-1;prepared.phase=WalkMotionPhase::finished;}
        state_=std::move(prepared);current_=sampler;
        return state_.phase==WalkMotionPhase::finished?WalkMotionStatus::finished:WalkMotionStatus::success;
    }catch(const std::bad_alloc&){return WalkMotionStatus::budget_exhausted;}
}
WalkMotionStatus WalkMotion::advance_waypoint(const WalkMotionPose& pose,const WalkMotionProfile& profile,bool walking) noexcept {
    if(pending_||state_.phase!=WalkMotionPhase::waypoint_required)return WalkMotionStatus::invalid_input;
    if(state_.remaining.empty()){state_.move_total_frames=-1;state_.phase=WalkMotionPhase::finished;return WalkMotionStatus::finished;}
    const auto next=make_waypoint(state_.remaining.front(),pose,profile,walking);
    if(next.status!=WalkMotionStatus::success)return next.status;
    state_.current=state_.remaining.front();state_.remaining.erase(state_.remaining.begin());
    current_=next.segment;state_.previous_position=pose;state_.move_frames=0;state_.move_total_frames=next.duration;
    state_.walk_direction=pose.radians;state_.target_direction=next.target;state_.turn_frames=std::min<std::int32_t>(10,next.duration);
    state_.phase=WalkMotionPhase::walking;return WalkMotionStatus::success;
}
WalkMotionPrepared WalkMotion::prepare_step(RouteFrameContext& frame,RouteFrameContext* parent) noexcept {
    WalkMotionPrepared result;
    if(state_.phase==WalkMotionPhase::waypoint_required){result.status=WalkMotionStatus::waypoint_required;return result;}
    if(state_.phase==WalkMotionPhase::collision_required){result.status=WalkMotionStatus::collision_required;return result;}
    if(state_.phase==WalkMotionPhase::finished){result.status=WalkMotionStatus::finished;return result;}
    if(state_.phase!=WalkMotionPhase::walking||pending_||!current_||state_.move_total_frames<=0||
       state_.move_frames<0||state_.move_frames>state_.move_total_frames||state_.velocity<0||state_.velocity>8||
       frame.state!=RouteFrameState::walking||(parent&&static_cast<std::uint8_t>(parent->state)>static_cast<std::uint8_t>(RouteFrameState::failed)))return result;
    if(!heading_fits(state_.target_direction)){result.status=WalkMotionStatus::unsupported_arithmetic;return result;}
    try{
        auto lease=std::make_shared<PreparedWalkMotionStep::Lease>();
        auto next_frame=frame;std::optional<RouteFrameContext> next_parent;if(parent)next_parent=*parent;
        auto velocity=state_.velocity;
        const bool query_turn=state_.remaining.empty()&&state_.move_total_frames-state_.move_frames<=28;
        const auto& effective=next_parent?*next_parent:next_frame;
        if(query_turn&&effective.state==RouteFrameState::room_portal&&effective.portal_turns>=static_cast<std::uint32_t>(INT32_MAX)){
            result.status=WalkMotionStatus::unsupported_arithmetic;return result;
        }
        const bool tail=query_turn&&consume_route_portal_turn(next_frame,next_parent?&*next_parent:nullptr);
        if(tail){if(velocity<=0)velocity=1;if(velocity>1)--velocity;}
        else if(velocity<8)++velocity;
        const auto advanced=std::int64_t(state_.move_frames)+velocity;
        if(advanced>INT32_MAX){result.status=WalkMotionStatus::unsupported_arithmetic;return result;}
        const auto frames=std::min(static_cast<std::int32_t>(advanced),state_.move_total_frames);
        auto sampler=*current_;const auto sample=sampler.next_point_and_velocity(frames);
        if(sample.status!=WalkPathStatus::success){result.status=status(sample.status);return result;}
        lease->data={sample,state_.previous_position.level,velocity,frames,
            {quotient(static_cast<float>(8-velocity),8),product(quotient(static_cast<float>(velocity),8),0.66f),
             product(quotient(static_cast<float>(velocity),8),0.33f)},query_turn};
        // Nothing after this point allocates or invokes external code.
        frame=next_frame;if(parent)*parent=*next_parent;
        state_.velocity=velocity;state_.move_frames=frames;current_=sampler;
        pending_=lease;state_.phase=WalkMotionPhase::awaiting_placement;
        result.step=PreparedWalkMotionStep(lease);result.status=WalkMotionStatus::success;return result;
    }catch(const std::bad_alloc&){result.status=WalkMotionStatus::budget_exhausted;return result;}
    catch(...){result.status=WalkMotionStatus::unsupported_arithmetic;return result;}
}
bool WalkMotion::can_commit(const PreparedWalkMotionStep& step) const noexcept {
    const auto lease=step.lease_.lock();return state_.phase==WalkMotionPhase::awaiting_placement&&pending_&&
        lease&&lease->active&&lease==pending_&&same_step(step.data_,lease->data);
}
WalkMotionCommitted WalkMotion::commit_step(const PreparedWalkMotionStep& step) noexcept {
    WalkMotionCommitted result;if(!can_commit(step))return result;
    const auto& data=pending_->data;
    const auto heading=static_cast<float>(state_.target_direction);
    WalkMotionEffects effects;effects.visual=data.sample.visual;
    effects.visual_z=product(static_cast<float>(state_.previous_position.level-1),2.95f);
    effects.radian_direction=heading;
    effects.velocity={product(data.sample.velocity.x,static_cast<float>(data.velocity)),
        product(data.sample.velocity.y,static_cast<float>(data.velocity))};
    const auto target=std::atan2(static_cast<double>(effects.velocity.x),static_cast<double>(-effects.velocity.y));
    auto difference=normalize(static_cast<double>(heading)-target);const auto absolute=std::abs(difference);
    auto velocity=state_.velocity;
    if(absolute>0.25){difference=difference>0?0.25:-0.25;
        if(absolute>0.5){if(velocity>4)velocity-=2;
            if(absolute>1){if(velocity>2)velocity-=2;if(absolute>pi/2)velocity=0;}}}
    const auto next_target=normalize(static_cast<double>(heading)-difference);
    if(!std::isfinite(heading)||!std::isfinite(next_target)||!std::isfinite(effects.velocity.x)||!std::isfinite(effects.velocity.y)){
        result.status=WalkMotionStatus::unsupported_arithmetic;return result;
    }
    effects.turn_velocity=difference;state_.velocity=velocity;state_.target_direction=next_target;
    pending_->active=false;pending_.reset();
    if(state_.move_frames==state_.move_total_frames){state_.move_total_frames=0;state_.phase=WalkMotionPhase::waypoint_required;result.status=WalkMotionStatus::waypoint_required;}
    else{state_.phase=WalkMotionPhase::walking;result.status=WalkMotionStatus::success;}
    result.effects=effects;return result;
}
WalkMotionStatus WalkMotion::reject_step(const PreparedWalkMotionStep& step) noexcept {
    if(!can_commit(step)||!current_)return WalkMotionStatus::invalid_input;
    current_->reset_to_frame(state_.move_frames);pending_->active=false;pending_.reset();
    state_.phase=WalkMotionPhase::collision_required;return WalkMotionStatus::collision_required;
}
WalkMotionStatus WalkMotion::set_collision_velocity(std::int32_t velocity) noexcept {
    if(pending_||state_.phase!=WalkMotionPhase::collision_required||velocity<0||velocity>8)
        return WalkMotionStatus::invalid_input;
    state_.velocity=velocity;return WalkMotionStatus::success;
}
WalkMotionStatus WalkMotion::resume_after_collision() noexcept {
    if(pending_||state_.phase!=WalkMotionPhase::collision_required||!current_)
        return WalkMotionStatus::invalid_input;
    state_.phase=WalkMotionPhase::walking;return WalkMotionStatus::success;
}
WalkMotionStatus WalkMotion::set_velocity(std::int32_t velocity) noexcept {
    if(pending_||!usable_phase(state_.phase)||velocity<0||velocity>8)
        return WalkMotionStatus::invalid_input;
    state_.velocity=velocity;return WalkMotionStatus::success;
}
WalkMotionCaptured WalkMotion::snapshot() const {
    WalkMotionCaptured result;
    if(pending_||!usable_phase(state_.phase))return result;
    try{result.snapshot=state_;result.status=WalkMotionStatus::success;}
    catch(const std::bad_alloc&){result.status=WalkMotionStatus::budget_exhausted;}
    return result;
}
WalkMotionStatus WalkMotion::restore(const WalkMotionSnapshot& saved){
    if(!valid_limit(limits_)||pending_||!usable_phase(saved.phase)||!valid_pose(saved.previous_position)||saved.velocity<0||saved.velocity>8||
       saved.turn_frames<0||saved.turn_frames>10||!heading_fits(saved.walk_direction)||!heading_fits(saved.target_direction)||
       saved.move_frames<0)return WalkMotionStatus::invalid_input;
    if(saved.remaining.size()>limits_.segments||saved.remaining.size()+(saved.current?1U:0U)>limits_.segments)
        return WalkMotionStatus::budget_exhausted;
    if(saved.phase==WalkMotionPhase::finished){
        if(!saved.remaining.empty()||saved.move_total_frames!=-1)return WalkMotionStatus::invalid_input;
    }else if(!saved.current||saved.move_total_frames<=0||saved.move_frames>saved.move_total_frames)return WalkMotionStatus::invalid_input;
    if(!saved.current&&(saved.move_frames||saved.turn_frames))return WalkMotionStatus::invalid_input;
    try{
        for(const auto& geometry:saved.remaining){const auto checked=validate_geometry(geometry);if(checked!=WalkMotionStatus::success)return checked;}
        std::optional<WalkPathSegment> sampler;
        if(saved.current){WalkPathSegment prepared;const auto rebuilt=prepared.restore({*saved.current,saved.move_total_frames});
            if(rebuilt!=WalkPathStatus::success)return status(rebuilt);
            prepared.reset_to_frame(saved.move_frames);sampler=prepared;}
        auto prepared=saved;state_=std::move(prepared);current_=sampler;return WalkMotionStatus::success;
    }catch(const std::bad_alloc&){return WalkMotionStatus::budget_exhausted;}
}
const char* walk_motion_status_name(WalkMotionStatus value) noexcept {
    switch(value){case WalkMotionStatus::success:return "success";case WalkMotionStatus::waypoint_required:return "waypoint_required";
        case WalkMotionStatus::collision_required:return "collision_required";case WalkMotionStatus::finished:return "finished";
        case WalkMotionStatus::invalid_input:return "invalid_input";case WalkMotionStatus::unsupported_arithmetic:return "unsupported_arithmetic";
        case WalkMotionStatus::budget_exhausted:return "budget_exhausted";}
    return "invalid_input";
}
} // namespace odh
