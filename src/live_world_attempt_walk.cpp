// SPDX-License-Identifier: MPL-2.0
#include "odh/live_world_attempt_walk.hpp"
#include "odh/collision.hpp"
#include <algorithm>
#include <exception>
#include <memory>
#include <set>
#include <tuple>

namespace odh {
namespace {
bool identity(LiveAttemptWalkIdentity a,LiveAttemptWalkIdentity b){return a.id==b.id&&a.generation==b.generation;}
bool rectangles_equal(const std::vector<RectRouteObstacle>& a,const std::vector<RectRouteObstacle>& b){
    if(a.size()!=b.size())return false;
    for(std::size_t n=0;n<a.size();++n)if(a[n].x1!=b[n].x1||a[n].y1!=b[n].y1||a[n].x2!=b[n].x2||a[n].y2!=b[n].y2)return false;
    return true;
}
bool identities_equal(const std::vector<LiveAttemptWalkIdentity>& a,const std::vector<LiveAttemptWalkIdentity>& b){
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),identity);
}
struct State {
    const World& world;const DefinitionIndex& definitions;LiveWorldAttemptWalkServices services;
    LiveWorldAttemptWalkLimits limits;LiveAttemptWalkRequest request;
    std::tuple<std::uint64_t,std::size_t,std::uint64_t> identity_revision;
    std::uint64_t serial=0;
    bool active=false,invalid=false;
    std::string diagnostic;
    State(const World& w,const DefinitionIndex& d,LiveWorldAttemptWalkServices s,
        LiveWorldAttemptWalkLimits l,std::uint64_t revision):world(w),definitions(d),services(std::move(s)),
        limits(l),identity_revision(w.entity_identity_revision()),serial(revision){}
    bool enumerate(std::vector<LiveAttemptWalkIdentity>& members,bool initial){
        members.clear();std::size_t count=0;
        for(std::int16_t cursor=0;;){
            const auto next=world.next_entity(cursor);if(!next)break;cursor=*next;
            if(++count>limits.entities){diagnostic="complete World membership inventory exceeds its bound";return false;}
            const auto* entity=world.find(cursor);if(!entity){diagnostic="World membership identity disappeared";return false;}
            if(initial&&entity->attachment&&!entity->attachment->positioned){
                const auto* parent=world.find(entity->attachment->container);
                if(!parent||parent->ui_ghost||bool(parent->position)!=bool(entity->position)||
                   (entity->position&&(parent->position->x!=entity->position->x||
                    parent->position->y!=entity->position->y||parent->position->level!=entity->position->level))){
                    diagnostic="legacy contained entity has no normalized source registration position: "+std::to_string(cursor);return false;
                }
            }
            if(!entity->position)continue; // Source OUT_OF_WORLD belongs to room zero.
            const auto position=*entity->position;const auto generation=entity->generation;
            const auto room=services.room_at(position);
            const auto* after=world.find(cursor);
            if(!room||!after||after->generation!=generation||!after->position||!(*after->position==position)){
                diagnostic="exact RoomInfo membership room or identity is unknown";return false;
            }
            if(*room==request.room)members.push_back({cursor,generation});
        }
        return true;
    }
    bool matches(const LiveAttemptWalkRequest& input){
        if(active||invalid)return false;
        struct Guard{bool& active;explicit Guard(bool& v):active(v){active=true;}~Guard(){active=false;}} guard(active);
        const auto reject=[&](){invalid=true;return false;};
        if(!identity(input.actor,request.actor)||input.room!=request.room||input.level!=request.level||
           input.room_bounds.x!=request.room_bounds.x||input.room_bounds.y!=request.room_bounds.y||
           input.room_bounds.width!=request.room_bounds.width||input.room_bounds.height!=request.room_bounds.height||
           input.architectural_count!=request.architectural_count||!input.architecture_complete||!input.entities_complete||
           !input.avatars_to_consider_complete||!identities_equal(input.entities,request.entities)||
           !identities_equal(input.avatars_to_consider,request.avatars_to_consider)||
           !rectangles_equal(input.architectural_and_bounds,request.architectural_and_bounds)||
           !services.context_still_matches()||world.entity_identity_revision()!=identity_revision)return reject();
        const auto current=world.room_registration_revision();if(!current)return reject();
        if(*current==serial)return true;
        std::vector<WorldRoomRegistrationEvent> events;
        if(!world.room_registration_events_since(serial,events)||events.size()>limits.registration_events)return reject();
        for(const auto& event:events){
            if(event.all_rooms)return reject();
            for(const auto& position:{event.before,event.after})if(position){
                const auto room=services.room_at(*position);
                if(!room||*room==request.room)return reject();
            }
        }
        std::vector<LiveAttemptWalkIdentity> current_members;
        if(!enumerate(current_members,false)||!identities_equal(current_members,request.entities)||
           !services.context_still_matches()||world.entity_identity_revision()!=identity_revision||
           world.room_registration_revision()!=current)return reject();
        serial=*current;return true;
    }
    LiveAttemptWalkFootprint footprint(LiveAttemptWalkIdentity id){
        if(!matches(request)||std::none_of(request.entities.begin(),request.entities.end(),[&](auto value){return identity(id,value);}))return {};
        const auto* entity=world.find(id.id);
        if(!entity||entity->generation!=id.generation)return {};
        if(!entity->position||entity->attachment)return {LiveAttemptWalkFootprintState::absent,{}};
        const auto flags=entity->object_words.find(8);if(flags==entity->object_words.end())return {};
        const auto bits=static_cast<std::uint16_t>(flags->second);
        CollisionShapeResult shape{};
        if(entity->kind==EntityKind::avatar){
            if(!entity->kill_timeout)return {};
            if(*entity->kill_timeout>-1&&(bits&4U)==0)return {LiveAttemptWalkFootprintState::absent,{}};
            shape=avatar_candidate_collision_shape(*entity->position);
        }else{
            if(bits&4U)return {LiveAttemptWalkFootprintState::absent,{}};
            if(!entity->definition)return {};
            const auto* candidate=definitions.unique(entity->definition->guid);if(!candidate)return {};
            shape=object_collision_shape(candidate->definition,*entity->position,bits,false);
        }
        if(shape.state==CollisionShapeState::absent)return {LiveAttemptWalkFootprintState::absent,{}};
        if(shape.state!=CollisionShapeState::present)return {};
        return {LiveAttemptWalkFootprintState::present,RectRouteObstacle{shape.box.x1,shape.box.y1,shape.box.x2,shape.box.y2}};
    }
};
}
SourceWalkDomain build_live_world_attempt_walk_domain(const World& world,const DefinitionIndex& definitions,
    const LiveWorldAttemptWalkRequest& input,LiveWorldAttemptWalkServices services,const LiveWorldAttemptWalkLimits& limits){
    SourceWalkDomain result;
    if(!limits.entities||limits.entities>4096||!limits.registration_events||limits.registration_events>16384){
        result.state=LiveAttemptWalkState::budget_exhausted;result.diagnostic="live World inventory/history limit exceeds bounded profile";return result;
    }
    if(input.architectural_and_bounds.size()>16384||input.avatars_to_consider.size()>limits.entities){
        result.state=LiveAttemptWalkState::budget_exhausted;result.diagnostic="source architectural or avatar input exceeds its bound";return result;
    }
    if(!input.architecture_complete||!input.room||!input.level||!input.avatars_to_consider_complete||
       input.architectural_count>input.architectural_and_bounds.size()||
       input.architectural_and_bounds.size()-input.architectural_count!=4||
       !services.room_at||!services.context_still_matches){result.diagnostic="complete source architecture, room map, avatar set and execution proof are required";return result;}
    try{
        const auto serial=world.room_registration_revision();if(!serial){result.diagnostic="World registration history is unavailable";return result;}
        auto state=std::make_shared<State>(world,definitions,std::move(services),limits,*serial);
        auto& request=state->request;request.actor=input.actor;request.room=input.room;request.level=input.level;
        request.room_bounds=input.room_bounds;request.architectural_and_bounds=input.architectural_and_bounds;
        request.architectural_count=input.architectural_count;request.architecture_complete=true;
        request.avatars_to_consider=input.avatars_to_consider;request.avatars_to_consider_complete=true;request.entities_complete=true;
        if(!state->services.context_still_matches()||!state->enumerate(request.entities,true)||
           world.entity_identity_revision()!=state->identity_revision||world.room_registration_revision()!=serial){
            result.diagnostic=state->diagnostic.empty()?"source World membership changed while building domain":state->diagnostic;return result;
        }
        const auto* actor=world.find(input.actor.id);
        if(!actor||actor->generation!=input.actor.generation||actor->kind!=EntityKind::avatar||
           !actor->position||actor->position->level!=input.level||
           std::none_of(request.entities.begin(),request.entities.end(),[&](auto id){return identity(id,input.actor);})){result.diagnostic="exact source actor is not a member of the selected room";return result;}
        std::set<std::int16_t> avatars;
        for(const auto id:request.avatars_to_consider){const auto* avatar=world.find(id.id);
            if(!avatar||avatar->kind!=EntityKind::avatar||avatar->generation!=id.generation||!id.generation||
               !avatars.insert(id.id).second){result.diagnostic="source avatar consideration identity is unknown or duplicated";return result;}}
        result.request=request;
        result.services.inventory_still_matches=[state](const auto& value){return state->matches(value);};
        result.services.footprint=[state](auto id){return state->footprint(id);};
        result.services.entry_point5=[state](const LiveAttemptWalkEntryPoint5Request& call){
            if(!state->matches(state->request)||!state->services.entry_point5)return LiveAttemptWalkEntryPoint5Result{};
            const auto is_member=[&](auto id){return std::any_of(state->request.entities.begin(),state->request.entities.end(),
                [&](auto member){return identity(member,id);});};
            const auto* tested=state->world.find(call.tested.id);
            if(call.ghost!=1||call.arguments!=std::array<std::int16_t,4>{call.other.id,1,0,0}||
               call.tested.id==call.other.id||!is_member(call.tested)||!is_member(call.other)||
               (!identity(call.tested,state->request.actor)&&!identity(call.other,state->request.actor))||
               !tested||tested->generation!=call.tested.generation||!tested->entry_points||tested->entry_points->size()<=5||
               (*tested->entry_points)[5].action!=call.action||call.action<=255)return LiveAttemptWalkEntryPoint5Result{};
            return state->services.entry_point5(call);
        };
        if(!state->matches(result.request)){result={};result.diagnostic="source World membership proof is not current";return result;}
        result.state=LiveAttemptWalkState::ready;return result;
    }catch(const std::exception& error){result={};result.diagnostic=std::string("live World domain construction failed: ")+error.what();return result;}
}
} // namespace odh
