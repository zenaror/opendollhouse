// SPDX-License-Identifier: MPL-2.0
// Native source port: FreeSO VMEntity.cs1196-1311 and VMContext.cs1238-1310,
// revision7e76c1960252ddd033d8937d5edb47d326f5b69c. MPL-2.0.
// https://www.mozilla.org/MPL/2.0/
#include "odh/object_position_valid.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>

namespace odh {
namespace {
using Result=ObjectPositionValidResult;
using Status=ObjectPositionValidStatus;
Result unknown(const char* why){Result value;value.diagnostic=why;return value;}
Result result(std::int16_t error,ObjectPositionIdentity other={},bool support=false){
    Result value;value.status=error==-1?Status::success:Status::blocked;value.placement_error=error;
    value.object=other;value.support_slot=support&&error==-1;return value;
}
std::optional<std::int16_t> word(const Entity& entity,std::int16_t index){
    const auto found=entity.object_words.find(index);
    return found==entity.object_words.end()?std::nullopt:std::optional<std::int16_t>{found->second};
}
std::uint32_t rotate(std::uint8_t segments,unsigned amount){
    return ((static_cast<std::uint32_t>(segments)<<(4U-amount))&15U)|((segments&15U)>>amount);
}
bool set_direction(World& world,ObjectPositionIdentity identity,std::int16_t direction){
    const auto* object=world.find(identity.id);
    if(!object||object->generation!=identity.generation||object->kind!=EntityKind::object||direction<0||direction>7)return false;
    const auto notches=word(*object,23);
    if(!notches||!word(*object,1))return false;
    if(*notches>1)direction=static_cast<std::int16_t>(direction-direction%*notches);
    VmFrame context;context.context.caller=identity.id;
    return world.write(context,3,1,direction);
}
}
Result ObjectPositionValid::evaluate(const ObjectPositionValidRequest& request){
    if(executing_||!request.source_still_matches||request.target.id<=0||!request.target.generation||request.direction>7||
       request.flags>15||request.candidate.x<INT16_MIN||request.candidate.x>INT16_MAX||
       request.candidate.y<INT16_MIN||request.candidate.y>INT16_MAX||request.candidate.level<INT8_MIN||request.candidate.level>INT8_MAX)
        return unknown("object placement needs a bounded real source capability and candidate");
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    try {
        auto lease=std::make_shared<bool>(true);
        struct LeaseEnd {std::shared_ptr<bool> value;~LeaseEnd(){*value=false;}} lease_end{lease};
        const auto valid=[world=&world_,identity=request.target,auth=request.source_still_matches,weak=std::weak_ptr<bool>(lease)](){
            const auto alive=weak.lock();if(!alive||!*alive||!auth())return false;
            const auto* target=world->find(identity.id);
            return target&&target->generation==identity.generation&&target->kind==EntityKind::object&&!target->ui_ghost;
        };
        if(!valid())return unknown("object placement source or target identity is unavailable");
        if(request.heading_already_published){
            const auto* object=world_.find(request.target.id);
            if(word(*object,1)!=std::optional<std::int16_t>{request.direction}||
               (object->position&&object->position->direction!=request.direction))
                return unknown("native group placement heading was not already published");
        }
        bool restored_heading=true;
        const auto publish_heading=[&](std::int16_t direction) noexcept {
            try{
                if(!services_.publish_heading)return set_direction(world_,request.target,direction);
                const auto* current=world_.find(request.target.id);
                if(!current||current->generation!=request.target.generation||current->kind!=EntityKind::object||direction<0||direction>7)return false;
                const auto notches=word(*current,23);if(!notches||!word(*current,1))return false;
                if(*notches>1)direction=static_cast<std::int16_t>(direction-direction%*notches);
                struct State{bool active=true,completed=false;unsigned calls=0;};
                auto state=std::make_shared<State>();
                struct Expire{std::shared_ptr<State> state;~Expire(){state->active=false;}} expire{state};
                const std::function<bool()> fixed=[state,world=&world_,identity=request.target,direction]{
                    if(!state->active||++state->calls!=1)return false;
                    state->completed=set_direction(*world,identity,direction);return state->completed;
                };
                const bool completed=services_.publish_heading(request.target,static_cast<std::uint8_t>(direction),fixed);
                state->active=false;current=world_.find(request.target.id);
                return completed&&state->calls==1&&state->completed&&current&&current->generation==request.target.generation&&
                    word(*current,1)==std::optional<std::int16_t>{direction}&&
                    (!current->position||current->position->direction==static_cast<std::uint8_t>(direction));
            }catch(...){return false;}
        };
        const auto run=[&]()->Result {
            const auto id=request.target.id;
            const auto& pos=request.candidate;
            if(pos.x==-32768&&pos.y==-32768&&pos.level==1)return result(-1);
            if(!services_.architecture)return unknown("object placement architecture capability is unavailable");
            const auto architecture=services_.architecture(request.target,pos);
            if(!architecture||!valid())return unknown("object placement architecture is unknown or changed its source");
            if(architecture->out_of_bounds||((request.flags&2U)&&architecture->user_out_of_bounds))return result(0);
            const auto wall=word(*world_.find(id),13),exclusive=word(*world_.find(id),63);
            if(!wall||!exclusive)return unknown("object wall placement words are unknown");
            const auto wall_flags=static_cast<std::uint16_t>(*wall);
            const bool diagonal=(architecture->wall_segments&48U)!=0;
            if(diagonal&&!(wall_flags&128U))return result(6);
            if(!diagonal&&(wall_flags&64U))return result(22);
            // DirectionToWallOff returns0 for every non-cardinal Direction.
            const unsigned direction=(request.direction&1U)?0U:request.direction/2U;
            const unsigned rotation=(direction+1U)%4U;
            const auto walls=rotate(architecture->wall_segments,rotation),used=rotate(architecture->occupied_walls,rotation);
            if((wall_flags&walls)!=(wall_flags&15U))return result(5);
            if((static_cast<std::uint16_t>(*exclusive)&2U)&&(wall_flags&used))return result(21);
            if(wall_flags&(walls<<8U))return result(6);
            const auto placement_word=word(*world_.find(id),42);
            if(!placement_word)return unknown("object floor placement flags are unknown");
            const auto placement=static_cast<std::uint16_t>(*placement_word),floor=architecture->precise_floor;
            if(floor==65535){if(!(placement&((1U<<11)|(1U<<12))))return result(28);}
            else {
                if(placement&(1U<<12))return result(30);
                if(floor==65534){if(!(placement&((1U<<2)|(1U<<13))))return result(28);}
                else {
                    if(placement&(1U<<13))return result(29);
                    if(floor==0){
                        if(pos.level==1){if(!(placement&2U))return result(19);}
                        else if(!(placement&(1U<<9))){
                            if(!architecture->level_offset)return unknown("object floor requires exact OBJD LevelOffset");
                            if(*architecture->level_offset==0)return result(17);
                        }
                    }else if(!(placement&1U)){
                        if(!architecture->level_offset)return unknown("object floor requires exact OBJD LevelOffset");
                        if(*architecture->level_offset==0||!(placement&(1U<<9)))return result(18);
                    }
                }
            }
            if(!architecture->user_out_of_bounds&&architecture->sloped&&(placement&1U)&&!(placement&(1U<<8)))return result(16);
            if(request.flags&4U)return result(-1);
            const auto original_direction=word(*world_.find(id),1);
            if(!original_direction||*original_direction<0||*original_direction>7)return unknown("source object Direction is unknown");
            struct Heading {
                World& world;ObjectPositionIdentity identity;std::int16_t direction;bool& restored;
                const decltype(publish_heading)& publish;bool temporary;
                ~Heading(){const auto* current=world.find(identity.id);if(temporary&&current&&current->generation==identity.generation&&current->kind==EntityKind::object)
                    restored=publish(direction);}
            } heading{world_,request.target,*original_direction,restored_heading,publish_heading,!request.heading_already_published};
            if((!request.heading_already_published&&!publish_heading(request.direction))||!valid())return unknown("temporary object heading could not be set");
            const auto heights=word(*world_.find(id),4),weight=word(*world_.find(id),27),flags=word(*world_.find(id),8);
            if(!heights||!weight||!flags)return unknown("GetObjPlace target height/weight/flags are unknown");
            const bool no_floor=(*heights&1)==0;
            const bool allow_avatars=(*flags&16)&&!(*flags&2); // AllAvatarsSolid is ignored in GetObjPlace.
            Result answer=result(no_floor?23:-1);
            if(!services_.intersections)return unknown("object candidate footprint/RoomInfo capability is unavailable");
            auto intersections=services_.intersections(request.target,pos,request.direction);
            if(!intersections||!valid())return unknown("object candidate footprint or source is unknown");
            if(!intersections->footprint_present||pos.level<1)return answer;
            auto& query=intersections->query;
            if(query.status!=RoomInfoObstacleIndexStatus::complete||query.intersections.size()>4096)
                return unknown("object candidate RoomInfo inventory is incomplete or over budget");
            struct Hit {ObjectPositionIdentity identity;};std::vector<Hit> hits;hits.reserve(query.intersections.size());
            std::set<std::int16_t> owners;
            for(const auto& hit:query.intersections){
                if(hit.owner_id<=0||hit.owner_id>32767)return unknown("RoomInfo obstacle owner exceeds source identity domain");
                const auto* other=world_.find(static_cast<std::int16_t>(hit.owner_id));
                if(!other||!owners.insert(other->id).second)return unknown("RoomInfo obstacle owner is absent or duplicated");
                hits.push_back({{other->id,other->generation}});
            }
            std::stable_sort(hits.begin(),hits.end(),[](const auto& a,const auto& b){return a.identity.id<b.identity.id;});
            for(const auto& hit:hits){
                const auto other=hit.identity;const auto current=[&](){const auto* value=world_.find(other.id);
                    return value&&value->generation==other.generation?value:nullptr;};
                if(!valid()||!current())return unknown("object placement participant was replaced");
                if(other.id==id)continue;
                if(!services_.same_group)return unknown("object group identity is unknown");
                const auto group=services_.same_group(request.target,other);
                if(!group||!valid()||!current())return unknown("object group identity or participant changed");
                if(*group||(current()->kind==EntityKind::avatar&&allow_avatars))continue;
                if(!services_.ignore_intersection)return unknown("object IgnoreIntersection membership is unknown");
                const auto ignored=services_.ignore_intersection(request.target,other);
                if(!ignored||!valid()||!current())return unknown("object IgnoreIntersection membership or participant changed");
                if(*ignored)continue;
                if(!services_.ghost_image)return unknown("source GhostImage state is unknown");
                const auto mine=services_.ghost_image(request.target),theirs=services_.ghost_image(other);
                if(!mine||!theirs||!valid()||!current())return unknown("source GhostImage state or participant changed");
                const auto ghost=static_cast<std::int16_t>(*mine||*theirs?1:0);
                const auto ep5=[&](ObjectPositionIdentity tested,ObjectPositionIdentity paired)->std::optional<bool>{
                    const auto* entity=world_.find(tested.id);
                    if(!entity||entity->generation!=tested.generation||!entity->entry_points||entity->entry_points->size()<=5)return {};
                    const auto action=(*entity->entry_points)[5].action;
                    if(action<=255)return false;
                    if(!services_.entry_point5)return {};
                    ObjectPositionEntryPoint5Request check{tested,paired,action,ghost,{paired.id,ghost,0,0},valid};
                    auto value=services_.entry_point5(check);
                    const auto* after=world_.find(tested.id);
                    const auto* counterpart=world_.find(paired.id);
                    if(!valid()||!after||after->generation!=tested.generation||!counterpart||counterpart->generation!=paired.generation)return {};
                    return value;
                };
                const auto first=ep5(request.target,other);if(!first)return unknown("target EP5 live result is unknown");if(*first)continue;
                const auto second=ep5(other,request.target);if(!second)return unknown("obstacle EP5 live result is unknown");if(*second)continue;
                answer=result(10,other);
                const auto* support=current();if(!support)return unknown("support was replaced by EP5");
                if(*heights>1&&!support->slots.empty()&&(support->slots[0]==0||support->slots[0]==id)){
                    if(!services_.slot0_height)return unknown("source support slot height is unknown");
                    const auto height=services_.slot0_height(other);
                    if(!height||!valid()||!current())return unknown("source support slot height or participant changed");
                    const auto bit=std::uint32_t{1}<<((static_cast<std::uint32_t>(*height)-1U)&31U);
                    if((static_cast<std::uint32_t>(*heights)&bit)!=0){
                        const auto strength=word(*current(),28);if(!strength)return unknown("support strength is unknown");
                        if(*weight<*strength){
                            answer=result(-1,other,true);
                            if(!(request.flags&1U))return result(10,other);
                            return answer;
                        }
                        answer=result(13,other);
                    }else if(no_floor)answer=result((*heights&8)?37:(*height==8?44:23),other);
                }
            }
            // PositionValid overrides ANY non-null result.Object without AcceptSlots,
            // including a more specific support failure status.
            if(answer.object.id&&!(request.flags&1U))return result(10,answer.object);
            return answer;
        };
        const auto answer=run();
        if(!restored_heading||!valid())return unknown("object placement source changed after restoring temporary heading");
        return answer;
    }catch(...){return unknown("object placement callback or bounded allocation failed");}
}
} // namespace odh
