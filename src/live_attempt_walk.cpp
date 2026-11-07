// SPDX-License-Identifier: MPL-2.0
// Native port of FreeSO VMRoutingFrame.AttemptWalk at
// 7e76c1960252ddd033d8937d5edb47d326f5b69c; MPL-2.0.
#include "odh/live_attempt_walk.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <set>

namespace odh {
namespace {
bool same(LiveAttemptWalkIdentity a,LiveAttemptWalkIdentity b){return a.id==b.id&&a.generation==b.generation;}
bool live(const World& world,LiveAttemptWalkIdentity id){const auto* e=world.find(id.id);
    return id.id>0&&id.generation&&e&&e->generation==id.generation;}
bool rectangle(const RectRouteObstacle& r){return r.x1<r.x2&&r.y1<r.y2;}
bool same_rectangle(const RectRouteObstacle& a,const RectRouteObstacle& b){
    return a.x1==b.x1&&a.y1==b.y1&&a.x2==b.x2&&a.y2==b.y2;
}
std::optional<std::uint16_t> flags(const World& world,LiveAttemptWalkIdentity id){
    if(!live(world,id))return {};
    const VmFrame frame{routine_key(4096),0,{id.id,id.id,id.id,0},{},{}};
    const auto value=world.read(frame,3,8);
    return value?std::optional<std::uint16_t>{static_cast<std::uint16_t>(*value)}:std::nullopt;
}
bool source_bounds(const LiveAttemptWalkRequest& request){
    const auto& b=request.room_bounds;
    if(b.x<0||b.y<0||b.width<=0||b.height<=0)return false;
    const auto x=(static_cast<std::int64_t>(b.x)-1)*16,y=(static_cast<std::int64_t>(b.y)-1)*16;
    const auto right=x+(static_cast<std::int64_t>(b.width)+2)*16;
    const auto bottom=y+(static_cast<std::int64_t>(b.height)+2)*16;
    const auto fits=[](std::int64_t v){return v>=INT32_MIN&&v<=INT32_MAX;};
    if(!fits(x-16)||!fits(y-16)||!fits(right+16)||!fits(bottom+16))return false;
    const auto a=static_cast<std::int32_t>(x),c=static_cast<std::int32_t>(right);
    const auto d=static_cast<std::int32_t>(y),e=static_cast<std::int32_t>(bottom);
    const RectRouteObstacle bands[]={{a-16,d-16,c+16,d},{a-16,e,c+16,e+16},
        {a-16,d-16,a,e+16},{c,d-16,c+16,e+16}};
    for(std::size_t n=0;n<4;++n)
        if(!same_rectangle(bands[n],request.architectural_and_bounds[request.architectural_count+n]))return false;
    return true;
}
}

LiveAttemptWalkResult compose_live_attempt_walk_obstacles(const World& world,
    const LiveAttemptWalkRequest& request,const LiveAttemptWalkServices& services,const LiveAttemptWalkLimits& limits){
    LiveAttemptWalkResult result;
    const auto fail=[&](std::string message,LiveAttemptWalkState state=LiveAttemptWalkState::unsupported,
                        std::optional<std::int16_t> entity=std::nullopt){
        return LiveAttemptWalkResult{state,{},{},result.entry_point5_calls,entity,std::move(message)};
    };
    if(!limits.entities||limits.entities>4096||!limits.obstacles||limits.obstacles>16384||
       !limits.entry_point5_calls||limits.entry_point5_calls>8192)
        return fail("live AttemptWalk limits exceed the bounded profile",LiveAttemptWalkState::budget_exhausted);
    if(request.entities.size()>limits.entities||request.avatars_to_consider.size()>limits.entities||
       request.architectural_and_bounds.size()>limits.obstacles)
        return fail("live AttemptWalk input exceeds its work bounds",LiveAttemptWalkState::budget_exhausted);
    if(!request.room||!request.level||!request.architecture_complete||!request.entities_complete||
       !request.avatars_to_consider_complete||!services.inventory_still_matches||!services.footprint||
       request.architectural_count>request.architectural_and_bounds.size()||
       request.architectural_and_bounds.size()-request.architectural_count!=4||!source_bounds(request))
        return fail("complete source room, architecture, ordered inventory and services are required");
    if(std::any_of(request.architectural_and_bounds.begin(),request.architectural_and_bounds.end(),
        [](const auto& value){return !rectangle(value);}))return fail("invalid architectural obstacle rectangle");
    bool actor_present=false;std::int16_t previous=0;
    for(const auto id:request.entities){
        if(id.id<=previous||!live(world,id))return fail("RoomInfo identities are incomplete, stale or unordered");
        previous=id.id;actor_present=actor_present||same(id,request.actor);
    }
    std::set<std::int16_t> considered;
    for(const auto id:request.avatars_to_consider){
        const auto* entity=world.find(id.id);
        if(!live(world,id)||!entity||entity->kind!=EntityKind::avatar||!considered.insert(id.id).second)
            return fail("AvatarsToConsider identity is unavailable");
    }
    const auto* actor=world.find(request.actor.id);
    if(!actor_present||!live(world,request.actor)||!actor||actor->kind!=EntityKind::avatar||
       !actor->position||actor->position->level!=request.level)return fail("live AttemptWalk caller identity is unavailable");
    const auto inventory_matches=[&](){
        if(!services.inventory_still_matches(request))return false;
        for(const auto id:request.entities)if(!live(world,id))return false;
        for(const auto id:request.avatars_to_consider)if(!live(world,id))return false;
        return live(world,request.actor);
    };
    try{
        if(!inventory_matches())return fail("source RoomInfo membership is not proven complete");
        const auto actor_flags=flags(world,request.actor);
        if(!actor_flags)return fail("caller Flags8 are unknown");
        const bool consider_avatars=(*actor_flags&16U)==0;
        result.obstacles=request.architectural_and_bounds;
        const auto ep5=[&](LiveAttemptWalkIdentity tested,LiveAttemptWalkIdentity other)
                ->LiveAttemptWalkEntryPoint5Result{
            const auto* entity=world.find(tested.id);
            if(!live(world,tested)||!entity||!entity->entry_points)return {};
            if(entity->entry_points->size()<=5||(*entity->entry_points)[5].action<=255)
                return {LiveAttemptWalkEntryPoint5State::returned_false,{}};
            if(!services.entry_point5)return {};
            if(result.entry_point5_calls>=limits.entry_point5_calls)
                return {LiveAttemptWalkEntryPoint5State::budget_exhausted,"live EP5 call budget exhausted"};
            const LiveAttemptWalkEntryPoint5Request call{tested,other,(*entity->entry_points)[5].action,1,{other.id,1,0,0}};
            ++result.entry_point5_calls;
            const auto answer=services.entry_point5(call);
            if(!inventory_matches())return {LiveAttemptWalkEntryPoint5State::unsupported,"source RoomInfo membership changed during live EP5"};
            return answer;
        };
        for(const auto id:request.entities){
            if(!inventory_matches())return fail("source RoomInfo membership changed before its next entity",LiveAttemptWalkState::unsupported,id.id);
            // Source captures both values before testing identity, avatar policy
            // or passability. Keep this rectangle even when EP5 changes it.
            const auto footprint=services.footprint(id);
            const auto entity_flags=flags(world,id);
            if(!inventory_matches())return fail("source RoomInfo membership changed during footprint lookup",LiveAttemptWalkState::unsupported,id.id);
            const auto* current=world.find(id.id);
            if(!current||!entity_flags||(footprint.state!=LiveAttemptWalkFootprintState::present&&
               footprint.state!=LiveAttemptWalkFootprintState::absent)||
               (footprint.state==LiveAttemptWalkFootprintState::present&&(!footprint.rectangle||!rectangle(*footprint.rectangle)))||
               (footprint.state==LiveAttemptWalkFootprintState::absent&&footprint.rectangle))
                return fail("live entity footprint or Flags8 are unknown",LiveAttemptWalkState::unsupported,id.id);
            if(id.id==request.actor.id||footprint.state==LiveAttemptWalkFootprintState::absent||
               (current->kind==EntityKind::avatar&&(!consider_avatars||!considered.count(id.id)))||
               ((*entity_flags&2U)==0&&(*entity_flags&16U)!=0))continue;
            auto answer=ep5(request.actor,id);
            if(answer.state==LiveAttemptWalkEntryPoint5State::returned_false)answer=ep5(id,request.actor);
            if(answer.state==LiveAttemptWalkEntryPoint5State::returned_true)continue;
            if(answer.state!=LiveAttemptWalkEntryPoint5State::returned_false)
                return fail(answer.diagnostic.empty()?"live EP5 did not return synchronously":answer.diagnostic,
                    answer.state==LiveAttemptWalkEntryPoint5State::budget_exhausted?LiveAttemptWalkState::budget_exhausted:LiveAttemptWalkState::unsupported,id.id);
            const auto& r=*footprint.rectangle;
            if(r.x1<INT32_MIN+3||r.y1<INT32_MIN+3||r.x2>INT32_MAX-3||r.y2>INT32_MAX-3)
                return fail("inflated footprint exceeds source integer coordinates",LiveAttemptWalkState::unsupported,id.id);
            if(result.obstacles.size()>=limits.obstacles)return fail("live obstacle budget exhausted",LiveAttemptWalkState::budget_exhausted,id.id);
            result.obstacles.push_back({r.x1-3,r.y1-3,r.x2+3,r.y2+3});result.entity_obstacle_ids.push_back(id.id);
        }
        if(!inventory_matches())return fail("source RoomInfo membership changed at composition completion");
        result.state=LiveAttemptWalkState::ready;return result;
    }catch(const std::exception& error){return fail(std::string("live AttemptWalk callback failed: ")+error.what());}
    catch(...){return fail("live AttemptWalk callback raised a non-standard exception");}
}
} // namespace odh
