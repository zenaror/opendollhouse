// SPDX-License-Identifier: MPL-2.0
// Source: FreeSO TSOClient/tso.simantics/Engine/VMRoutingFrame.cs at
// 7e76c1960252ddd033d8937d5edb47d326f5b69c, lines 525-543, 795-913,
// 998-1029. https://www.mozilla.org/MPL/2.0/
#include "odh/source_route_collision.hpp"
#include <algorithm>
#include <limits>

namespace odh {
namespace {
using Action=SourceRouteCollisionAction;
using Phase=SourceRouteCollisionPhase;
bool valid(SourceRouteCollisionIdentity value){return value.id>0&&value.generation!=0;}
bool same(SourceRouteCollisionIdentity a,SourceRouteCollisionIdentity b){return a.id==b.id&&a.generation==b.generation;}
SourceRouteCollisionDecision decision(Action action,std::int32_t wait=0){SourceRouteCollisionDecision value;value.action=action;value.wait_time=wait;return value;}
SourceRouteCollisionDecision failure(std::int16_t code,SourceRouteCollisionIdentity blocker={}){
    auto value=decision(Action::soft_fail);value.failure_code=code;value.failure_blocker=blocker;return value;
}
bool stable(Phase phase){return phase==Phase::ready||phase==Phase::waiting||phase==Phase::soft_failure;}
bool valid_snapshot(const SourceRouteCollisionSnapshot& value){
    if(!stable(value.phase)||value.retries>5||value.wait_time<0||value.wait_time>60||
       value.timeout>300||value.considered.size()>4096||
       (value.phase==Phase::ready&&value.wait_time!=0))return false;
    for(std::size_t n=0;n<value.considered.size();++n){
        if(!valid(value.considered[n]))return false;
        for(std::size_t p=0;p<n;++p)if(same(value.considered[n],value.considered[p]))return false;
    }
    return true;
}
}
std::optional<bool> source_can_shoo_avatar(const SourceRouteShooFacts& facts) noexcept {
    if(!facts.has_route)return {};
    if(*facts.has_route){
        if(!facts.oldest_route_shooed)return {};
        if(*facts.oldest_route_shooed)return false;
        if(!facts.parent_route_shooed)return {};
        if(*facts.parent_route_shooed)return false;
    }
    if(!facts.queued_destination_shoo)return {};
    return !*facts.queued_destination_shoo;
}
SourceRouteCollisionDecision SourceRouteCollision::begin_collision(const SourceRouteCollisionInput& input){
    if(state_.phase!=Phase::ready||!valid(input.caller))return {};
    if(input.kind==SourceRouteCollisionBlocker::none){if(input.blocker.id||input.blocker.generation)return {};}
    else if((input.kind!=SourceRouteCollisionBlocker::object&&input.kind!=SourceRouteCollisionBlocker::avatar)||
            !valid(input.blocker)||same(input.caller,input.blocker))return {};
    const bool avatar=input.kind==SourceRouteCollisionBlocker::avatar;
    if(avatar&&(!input.avatar_top_route||(*input.avatar_top_route&&
       (!input.avatar_route_wait||*input.avatar_route_wait<0))))return {};
    // A source stopped, previously-considered non-route avatar decrements twice.
    // Do not execute C++ signed overflow at the supported arithmetic boundary.
    const bool already_considered=avatar&&std::any_of(state_.considered.begin(),state_.considered.end(),
        [&](const auto& identity){return same(identity,input.blocker);});
    if(state_.retries==std::numeric_limits<std::int32_t>::min()&&
       (!avatar||already_considered||!*input.avatar_top_route))return {};
    auto next=state_;
    bool around=true;
    if(avatar){
        const bool considered=std::any_of(next.considered.begin(),next.considered.end(),
            [&](const auto& identity){return same(identity,input.blocker);});
        if(considered&&--next.retries<=0){next.phase=Phase::soft_failure;state_=std::move(next);return failure(3,input.caller);}
        if(*input.avatar_top_route)around=*input.avatar_route_wait>0;
        else --next.retries; // Source does NOT test exhaustion at this decrement.
        if(around&&!considered){
            if(next.considered.size()==4096)return {};
            next.considered.push_back(input.blocker);
        }
    }else if(--next.retries<=0){next.phase=Phase::soft_failure;state_=std::move(next);return failure(3,input.caller);}
    if(!around){next.wait_time=std::max<std::int32_t>(30,next.wait_time);next.phase=Phase::waiting;
        state_=std::move(next);return decision(Action::wait_next_tick,state_.wait_time);}
    next.phase=Phase::reroute_pending;state_=std::move(next);pending_=input;
    return decision(Action::attempt_walk);
}
SourceRouteCollisionDecision SourceRouteCollision::finish_reroute(SourceRouteCollisionReplan result,std::optional<bool> can_shoo){
    if(state_.phase!=Phase::reroute_pending||!pending_)return {};
    if(result==SourceRouteCollisionReplan::ready){state_.phase=Phase::ready;pending_.reset();return decision(Action::continue_replanned);}
    if(result!=SourceRouteCollisionReplan::no_path)return {};
    const auto input=*pending_;
    if(input.kind!=SourceRouteCollisionBlocker::avatar){state_.phase=Phase::soft_failure;pending_.reset();return failure(8,input.blocker);}
    if(!can_shoo)return {};
    if(*can_shoo){
        const auto found=std::find_if(state_.considered.begin(),state_.considered.end(),
            [&](const auto& identity){return same(identity,input.blocker);});
        if(found!=state_.considered.end())state_.considered.erase(found);
        state_.phase=Phase::shoo_pending;
        return decision(*input.avatar_top_route?Action::shoo_direct_route:Action::shoo_interaction);
    }
    state_.wait_time=std::max<std::int32_t>(60,state_.wait_time);state_.phase=Phase::waiting;pending_.reset();
    return decision(Action::wait_next_tick,state_.wait_time);
}
SourceRouteCollisionDecision SourceRouteCollision::acknowledge_shoo(bool completed) noexcept{
    if(state_.phase!=Phase::shoo_pending||!pending_||!completed)return {};
    state_.wait_time=std::max<std::int32_t>(60,state_.wait_time);state_.phase=Phase::waiting;pending_.reset();
    return decision(Action::wait_next_tick,state_.wait_time);
}
SourceRouteCollisionDecision SourceRouteCollision::tick_wait(std::int32_t velocity) noexcept{
    if(state_.phase!=Phase::waiting||velocity<0||velocity>8)return {};
    if(state_.wait_time==0){state_.phase=Phase::ready;auto value=decision(Action::resume_walking);value.velocity=velocity;return value;}
    if(state_.timeout==std::numeric_limits<std::int32_t>::min())return {};
    auto value=decision(Action::wait_next_tick);value.velocity=velocity>0?velocity-1:0;
    value.animation_weights={static_cast<float>(8-value.velocity)/8.0f,
        (static_cast<float>(value.velocity)/8.0f)*0.66f,(static_cast<float>(value.velocity)/8.0f)*0.33f};
    --state_.wait_time;--state_.timeout;value.wait_time=state_.wait_time;
    if(state_.timeout<=0){state_.phase=Phase::soft_failure;value.action=Action::soft_fail;value.failure_code=3;}
    return value;
}
bool SourceRouteCollision::continue_after_soft_failure() noexcept{
    if(state_.phase!=Phase::soft_failure)return false;
    // Source SoftFail itself does not replenish counters or clear WaitTime.
    state_.phase=state_.wait_time>0?Phase::waiting:Phase::ready;return true;
}
std::optional<SourceRouteCollisionSnapshot> SourceRouteCollision::snapshot() const{
    if(!stable(state_.phase))return {};
    return state_;
}
bool SourceRouteCollision::restore(const SourceRouteCollisionSnapshot& saved){
    if(!valid_snapshot(saved))return false;
    auto next=saved;state_=std::move(next);pending_.reset();return true;
}
} // namespace odh
