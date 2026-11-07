// SPDX-License-Identifier: MPL-2.0
#include "odh/room_lighting.hpp"
#include "odh/room_score.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
namespace odh { namespace {
void check(bool value,const char* message){if(!value)throw FormatError(message);}
bool same(ObjectPositionIdentity a,ObjectPositionIdentity b){return a.id==b.id&&a.generation==b.generation;}
void identity(ObjectPositionIdentity value){check(value.id>0&&value.generation,"lighting identity is invalid");}
bool finite(float value){return std::isfinite(value);}
void bounds(const RoomLightingBounds& b){check(b.width>=0&&b.height>=0&&
    std::int64_t(b.x)+b.width<=INT32_MAX&&std::int64_t(b.y)+b.height<=INT32_MAX,"lighting bounds are invalid");}
RoomLightingBounds unite(RoomLightingBounds a,RoomLightingBounds b){
    const auto x=std::min(a.x,b.x),y=std::min(a.y,b.y);
    const auto right=std::max(std::int64_t(a.x)+a.width,std::int64_t(b.x)+b.width);
    const auto bottom=std::max(std::int64_t(a.y)+a.height,std::int64_t(b.y)+b.height);
    check(right-x<=INT32_MAX&&bottom-y<=INT32_MAX,"lighting union overflows bounds");
    return {x,y,static_cast<std::int32_t>(right-x),static_cast<std::int32_t>(bottom-y)};
}
void map_shape(const GeometricRoomMap& map,const RoomLightingLimits& limits){
    check(map.width>0&&map.height>0&&map.width<=64&&map.height<=64&&map.light_base_rooms_resolved&&!map.rooms.empty()&&map.rooms.size()<=limits.rooms&&map.rooms.size()<=32768,"lighting requires resolved bounded light-base rooms");
    for(std::size_t i=0;i<map.rooms.size();++i){const auto& r=map.rooms[i];
        check(r.id==i&&r.light_base_room<map.rooms.size()&&map.rooms[r.light_base_room].light_base_room==r.light_base_room&&r.level<=1,"lighting room topology is invalid");
        if(i)check(r.light_base_room!=0&&r.bounds.x<=map.width&&r.bounds.y<=map.height&&
            r.bounds.width<=map.width-r.bounds.x&&r.bounds.height<=map.height-r.bounds.y,
            "lighting room has dummy base or out-of-map bounds");
    }
}
const Entity& entity(const World& world,ObjectPositionIdentity id){
    identity(id);const auto* e=world.find(id.id);
    check(e&&e->generation==id.generation&&!e->dead&&!e->ui_ghost,"lighting entity is missing, dead or replaced");return *e;
}
std::int16_t word(const World& world,const Entity& e,std::int16_t index){
    VmFrame f;f.context.caller=e.id;f.context.caller_generation=e.generation;
    f.context.entity_references_bound=true;const auto value=world.read(f,3,index);
    check(value.has_value(),"lighting requires an available source ObjectData field");return *value;
}
std::uint16_t scaled_ushort(float value){check(finite(value)&&value>=0&&value<2147483648.0f,"lighting scaled contribution exceeds source integer range");return static_cast<std::uint16_t>(static_cast<std::uint32_t>(value));}
void update_bounds(RoomLightSource& l){
    check(finite(l.x)&&finite(l.y)&&finite(l.size)&&l.size>=0&&l.size<1073741824.0f&&
        l.x-l.size>static_cast<float>(INT32_MIN)&&l.y-l.size>static_cast<float>(INT32_MIN)&&
        l.x-l.size<static_cast<float>(INT32_MAX)&&l.y-l.size<static_cast<float>(INT32_MAX),"lighting cluster exceeds rectangle range");
    l.bounds={static_cast<std::int32_t>(l.x-l.size),static_cast<std::int32_t>(l.y-l.size),static_cast<std::int32_t>(l.size)*2,static_cast<std::int32_t>(l.size)*2};bounds(l.bounds);
}
void cluster(std::vector<RoomLightSource>& lights,std::uint64_t& remaining){
    for(std::size_t i=0;i<lights.size();++i)for(std::size_t j=i+1;j<lights.size();){
        check(remaining>0,"lighting cluster comparison budget exceeded");--remaining;
        auto& a=lights[i];const auto& b=lights[j];const float dx=a.x-b.x,dy=a.y-b.y;
        if(a.outdoors_color==b.outdoors_color&&dx*dx+dy*dy<=1024.0f){
            const float weight=a.weight+b.weight;
            a.x=a.x*(a.weight/weight)+b.x*(b.weight/weight);a.y=a.y*(a.weight/weight)+b.y*(b.weight/weight);
            a.size=(a.size+b.size)*0.6f;a.intensity=std::min(1.25f,(a.intensity+b.intensity)*0.75f);
            for(unsigned c=0;c<4;++c)a.color[c]=static_cast<std::uint8_t>(std::min(255U,unsigned(a.color[c])+unsigned(b.color[c])));
            // Source Cluster never assigns newWeight back to l1.Weight.
            update_bounds(a);lights.erase(lights.begin()+static_cast<std::ptrdiff_t>(j));
        }else ++j;
    }
}
}
void validate_room_lighting_snapshot(const RoomLightingSnapshot& state,const RoomLightingLimits& limits){
    check(state.version==1&&state.by_base_room.size()<=limits.rooms,"unsupported lighting snapshot version or room budget");
    check(state.visual_rooms.size()<=limits.entities,"lighting visual identity budget exceeded");
    std::pair<std::int16_t,std::uint64_t> previous{};
    for(const auto& visual:state.visual_rooms){identity(visual.owner);const auto key=std::make_pair(visual.owner.id,visual.owner.generation);check(key>previous,"lighting visual identities repeat or are reordered");previous=key;}
    std::size_t lights=0,components=0,footprints=0;
    for(const auto& [id,r]:state.by_base_room){
        check(id>0&&id<32768&&r.base_room==id&&r.ambient_light<=100&&r.outside_light<=100&&r.room_score>=-100&&r.room_score<=100,"invalid lighting base or levels/score");bounds(r.bounds);
        check(r.lights.size()<=limits.lights-lights&&r.components.size()<=limits.components-components&&r.object_footprints.size()<=limits.footprints-footprints,"lighting aggregate snapshot budget exceeded");
        lights+=r.lights.size();components+=r.components.size();footprints+=r.object_footprints.size();
        for(const auto& l:r.lights){check(l.room==id&&l.level<=1&&finite(l.x)&&finite(l.y)&&finite(l.size)&&finite(l.intensity)&&finite(l.weight)&&finite(l.height)&&l.size>0&&l.intensity>=0&&l.intensity<=1.25f&&l.weight>0,"invalid lighting source fields");bounds(l.bounds);if(l.component)identity(*l.component);if(l.window_room)check(*l.window_room<32768,"invalid lighting window room");}
        for(const auto& c:r.components)identity(c);
        for(const auto& f:r.object_footprints)check(f.x1<=f.x2&&f.y1<=f.y2,"invalid lighting footprint");
    }
}
void validate_room_lighting_snapshot(const RoomLightingSnapshot& state,const GeometricRoomMap& map,const RoomLightingLimits& limits){
    validate_room_lighting_snapshot(state,limits);map_shape(map,limits);
    for(const auto& [id,r]:state.by_base_room){check(id<map.rooms.size()&&map.rooms[id].light_base_room==id,"lighting snapshot base differs from room map");for(const auto& l:r.lights){check(l.level==map.rooms[id].level,"lighting source floor differs from base room");if(l.window_room)check(*l.window_room<map.rooms.size(),"lighting window target absent from map");}}
}
const RoomLighting* room_lighting_for_room(const RoomLightingSnapshot& state,const GeometricRoomMap& map,std::uint16_t id) noexcept {
    if(state.version!=1||!id||id>=map.rooms.size())return nullptr;const auto base=map.rooms[id].light_base_room;
    const auto it=state.by_base_room.find(base);return it==state.by_base_room.end()?nullptr:&it->second;
}
std::int16_t room_lighting_score(const RoomLightingSnapshot& state,const GeometricRoomMap& map,std::uint16_t id) noexcept {const auto* light=room_lighting_for_room(state,map,id);return light?light->room_score:0;}
bool refresh_room_lighting_score(const World& world,const GeometricRoomMap& map,ObjectPositionRegistrySnapshot& registry,
    RoomLightingSnapshot& state,std::uint16_t requested,const RoomLightingLimits& limits){
    if(!requested)return true;validate_room_lighting_snapshot(state,map,limits);
    check(requested<map.rooms.size()&&registry.width==map.width&&registry.height==map.height,"room-score registry/map differs");
    const auto base=map.rooms[requested].light_base_room;std::int64_t impact=0;std::uint64_t area=0;bool outside=false;
    std::set<std::int16_t> owners;
    for(const auto& support:map.rooms)if(support.id&&support.light_base_room==base){
        area+=support.area;outside=support.outside;
        const auto room=std::find_if(registry.rooms.begin(),registry.rooms.end(),[&](const auto& r){return r.id==support.id;});check(room!=registry.rooms.end(),"room-score support registry is absent");
        for(const auto owner:room->entities){identity(owner);const auto* e=world.find(owner.id);
            check(e&&e->generation==owner.generation&&!e->ui_ghost&&owners.insert(owner.id).second&&owners.size()<=limits.entities,"room-score identity is missing, repeated or over budget");
            impact+=word(world,*e,7);
        }
    }
    check(impact>=INT32_MIN&&impact<=INT32_MAX,"room-score impact sum exceeds source int range");
    const auto actual_base=std::find_if(registry.rooms.begin(),registry.rooms.end(),[&](const auto& r){return r.id==base;});check(actual_base!=registry.rooms.end(),"room-score base registry is absent");
    auto candidate=state;auto& light=candidate.by_base_room[base];light.base_room=base;
    light.room_score=source_room_score(static_cast<std::int32_t>(impact),area,outside);
    validate_room_lighting_snapshot(candidate,map,limits);state=std::move(candidate);
    actual_base->room_score=state.by_base_room.at(base).room_score;
    if(std::find(registry.score_dirty.begin(),registry.score_dirty.end(),base)==registry.score_dirty.end())registry.score_dirty.push_back(base);
    return true;
}
std::optional<std::uint16_t> room_lighting_visual_room(const World& world,ObjectPositionIdentity id,std::uint8_t disabled){
    const auto* e=world.find(id.id);if(!e||e->generation!=id.generation||e->dead||e->ui_ghost)return {};
    if(e->kind==EntityKind::object&&disabled>=4)return std::uint16_t{65533};
    VmFrame f;f.context.caller=id.id;const auto flags=world.read(f,3,40),contribution=world.read(f,3,51),room=world.read(f,3,29);
    if(!flags||!contribution||!room)return {};
    if(e->kind==EntityKind::object&&(std::uint16_t(*flags)&0x400)&&*contribution>0&&!(std::uint16_t(*flags)&0xc000))return std::uint16_t{65535};
    return static_cast<std::uint16_t>(static_cast<std::int32_t>(*room)+1);
}
bool refresh_room_lighting_visual(RoomLightingSnapshot& state,const World& world,ObjectPositionIdentity owner,std::uint8_t disabled,const RoomLightingLimits& limits){
    validate_room_lighting_snapshot(state,limits);
    const auto room=room_lighting_visual_room(world,owner,disabled);if(!room)return false;
    const auto same_id=std::find_if(state.visual_rooms.begin(),state.visual_rooms.end(),[&](const auto& v){return v.owner.id==owner.id;});
    if(same_id==state.visual_rooms.end()&&state.visual_rooms.size()>=limits.entities)return false;
    state.visual_rooms.erase(std::remove_if(state.visual_rooms.begin(),state.visual_rooms.end(),[&](const auto& v){return v.owner.id==owner.id;}),state.visual_rooms.end());
    const auto at=std::lower_bound(state.visual_rooms.begin(),state.visual_rooms.end(),owner.id,[](const auto& v,auto id){return v.owner.id<id;});state.visual_rooms.insert(at,{owner,*room});return true;
}
std::optional<std::uint16_t> room_lighting_cached_visual_room(const RoomLightingSnapshot& state,ObjectPositionIdentity owner) noexcept {
    if(state.version!=1)return {};const auto at=std::find_if(state.visual_rooms.begin(),state.visual_rooms.end(),[&](const auto& v){return same(v.owner,owner);});return at==state.visual_rooms.end()?std::nullopt:std::optional<std::uint16_t>{at->room};
}
void erase_room_lighting_visual(RoomLightingSnapshot& state,ObjectPositionIdentity owner){state.visual_rooms.erase(std::remove_if(state.visual_rooms.begin(),state.visual_rooms.end(),[&](const auto& v){return same(v.owner,owner);}),state.visual_rooms.end());}
static std::optional<RoomLighting> recalculate_room_lighting_impl(const World& world,const GeometricRoomMap& map,
    const ObjectPositionRegistrySnapshot& registry,std::uint16_t requested,const RoomLightingServices& services,const RoomLightingLimits& limits,std::vector<std::uint16_t>* affected_bases,std::uint64_t& work_left){
    if(!requested)return {};map_shape(map,limits);
    check(requested<map.rooms.size()&&registry.complete&&registry.width==map.width&&registry.height==map.height,"lighting registry/map is unavailable or mismatched");
    check(services.definitions&&services.group&&services.object_state,"lighting source services are unavailable");
    std::set<std::uint16_t> registered_rooms;std::set<std::int16_t> registered_entities;
    for(const auto& r:registry.rooms){check(r.id<map.rooms.size()&&registered_rooms.insert(r.id).second,"lighting registry room repeats or is out of range");std::int16_t previous=0;for(const auto id:r.entities){entity(world,id);check(id.id>previous&&registered_entities.insert(id.id).second&&registered_entities.size()<=limits.entities,"lighting room entities repeat or exceed budget");previous=id.id;}}
    const auto base=map.rooms[requested].light_base_room;const auto& info=map.rooms[base];RoomLighting light;light.base_room=base;
    light.bounds={static_cast<std::int32_t>(info.bounds.x),static_cast<std::int32_t>(info.bounds.y),static_cast<std::int32_t>(info.bounds.width),static_cast<std::int32_t>(info.bounds.height)};
    std::map<std::int16_t,RoomLightingObjectState> metadata;
    std::map<std::int16_t,std::shared_ptr<const std::vector<ObjectPositionIdentity>>> groups;
    const auto object_state=[&](ObjectPositionIdentity id)->const RoomLightingObjectState&{
        const auto& e=entity(world,id);auto it=metadata.find(id.id);if(it!=metadata.end())return it->second;
        check(e.definition&&services.definitions->unique(e.definition->guid),"lighting definition is absent or ambiguous");
        check(metadata.size()<limits.entities,"lighting entity metadata budget exceeded");auto state=services.object_state(id);check(state.has_value(),"lighting object state is unknown");
        if(state->particle)check(finite(state->particle->offset_x)&&finite(state->particle->offset_y)&&finite(state->particle->height),"lighting particle metadata is invalid");
        return metadata.emplace(id.id,*state).first->second;
    };
    const auto group=[&](ObjectPositionIdentity id)->const std::vector<ObjectPositionIdentity>&{
        const auto found=groups.find(id.id);if(found!=groups.end())return *found->second;
        auto members=services.group(id);check(members&& !members->empty()&&members->size()<=limits.group_members,"lighting physical group is unknown or exceeds budget");
        std::set<std::int16_t> ids;bool contains=false;
        for(const auto member:*members){entity(world,member);check(ids.insert(member.id).second&&!groups.count(member.id),"lighting physical group overlaps or repeats a member");contains|=same(member,id);}
        check(contains&&groups.size()+members->size()<=limits.entities,"lighting physical group omits queried member or exceeds budget");
        auto owned=std::make_shared<const std::vector<ObjectPositionIdentity>>(std::move(*members));for(const auto member:*owned)groups.emplace(member.id,owned);return *owned;
    };
    const auto add_source=[&](const Entity& e,const RoomLightingObjectState& state,bool outdoors,float size,std::uint8_t level,bool component){
        check(e.position.has_value()&&e.position->x>=-32768&&e.position->x<=32767&&e.position->y>=-32768&&e.position->y<=32767,"lighting source has no bounded source position");RoomLightSource value;value.x=static_cast<float>(e.position->x);value.y=static_cast<float>(e.position->y);value.size=size;value.intensity=outdoors?0.8f:1.0f;value.outdoors_color=outdoors;value.color=state.color;value.room=base;value.level=level;
        value.bounds={e.position->x-static_cast<std::int32_t>(size),e.position->y-static_cast<std::int32_t>(size),static_cast<std::int32_t>(size)*2,static_cast<std::int32_t>(size)*2};
        if(component&&e.kind==EntityKind::object){value.component=ObjectPositionIdentity{e.id,e.generation};if(state.particle){value.x+=state.particle->offset_x;value.y+=state.particle->offset_y;value.height=state.particle->height;}}
        check(light.lights.size()<limits.lights,"lighting source count budget exceeded");light.lights.push_back(value);
    };
    std::int32_t inside=0,outside=0,impact=0;std::uint32_t area=0;std::vector<std::uint16_t> affected;
    for(const auto& room:map.rooms){if(!room.id||room.light_base_room!=base)continue;
        check(room.level==info.level,"lighting support rooms disagree on floor");
        area+=room.area;light.bounds=unite(light.bounds,{static_cast<std::int32_t>(room.bounds.x),static_cast<std::int32_t>(room.bounds.y),static_cast<std::int32_t>(room.bounds.width),static_cast<std::int32_t>(room.bounds.height)});
        const auto ri=std::find_if(registry.rooms.begin(),registry.rooms.end(),[&](const auto& r){return r.id==room.id;});check(ri!=registry.rooms.end()&&ri->entities.size()<=limits.entities,"lighting support room is absent or exceeds entity budget");
        for(const auto owner:ri->entities){const auto& e=entity(world,owner);const auto& all=group(owner);std::vector<ObjectPositionIdentity> local;
            for(const auto member:all)if(static_cast<std::int32_t>(word(world,entity(world,member),29))+1==room.id)local.push_back(member);
            if(local.empty()||!same(local.front(),owner))continue;const auto& state=object_state(owner);
            if(e.kind==EntityKind::object&&state.disabled>0)continue;
            const auto flags=static_cast<std::uint16_t>(word(world,e,40));std::size_t lit=0;for(const auto member:local)if(word(world,entity(world,member),51)>0)++lit;
            if(lit){
                if(flags&0xc000){for(const auto member:local){const auto& sub=entity(world,member);add_source(sub,object_state(member),true,160,room.level,true);outside+=static_cast<std::uint16_t>(word(world,sub,51));}}
                else if(lit==local.size()){
                    float x=0,y=0;for(const auto member:local){const auto& sub=entity(world,member);check(sub.position.has_value(),"combined light has no position");x+=sub.position->x;y+=sub.position->y;inside+=static_cast<std::uint16_t>(word(world,sub,51));}
                    add_source(e,state,false,160,room.level,true);auto& source=light.lights.back();const auto ox=e.kind==EntityKind::object&&state.particle?state.particle->offset_x:0.0f,oy=e.kind==EntityKind::object&&state.particle?state.particle->offset_y:0.0f;source.x=x/static_cast<float>(local.size())+ox;source.y=y/static_cast<float>(local.size())+oy;
                    const auto bx=static_cast<std::int32_t>(x/static_cast<float>(local.size())),by=static_cast<std::int32_t>(y/static_cast<float>(local.size()));source.bounds={bx-160,by-160,320,320};
                }else for(const auto member:local){const auto& sub=entity(world,member);const auto contribution=static_cast<std::uint16_t>(word(world,sub,51));if(contribution){add_source(sub,object_state(member),false,160,room.level,true);inside+=contribution;}}
            }else if(e.kind==EntityKind::object&&!state.moves_often){
                const auto& first=entity(world,all.front());std::optional<RoomLightingBounds> bound;
                if(first.kind!=EntityKind::avatar&&!first.attachment)for(const auto member:all){const auto& sub=entity(world,member);const auto f=static_cast<std::uint16_t>(word(world,sub,8));if((f&2)||!(f&16)){
                    const auto fp=std::find_if(registry.footprints.begin(),registry.footprints.end(),[&](const auto& b){return same(b.owner,member);});
                    check(fp!=registry.footprints.end(),"lighting physical footprint presence is unknown");if(fp->present){check(std::int64_t(fp->box.x2)-fp->box.x1>=0&&std::int64_t(fp->box.x2)-fp->box.x1<=INT32_MAX&&std::int64_t(fp->box.y2)-fp->box.y1>=0&&std::int64_t(fp->box.y2)-fp->box.y1<=INT32_MAX,"lighting footprint range invalid");RoomLightingBounds b{fp->box.x1,fp->box.y1,static_cast<std::int32_t>(std::int64_t(fp->box.x2)-fp->box.x1),static_cast<std::int32_t>(std::int64_t(fp->box.y2)-fp->box.y1)};bounds(b);bound=bound?unite(*bound,b):b;}
                }}
                if(bound){check(light.object_footprints.size()<limits.footprints,"lighting footprint budget exceeded");light.object_footprints.push_back({bound->x,bound->y,bound->x+bound->width,bound->y+bound->height});}
                check(local.size()<=limits.components-light.components.size(),"lighting component budget exceeded");light.components.insert(light.components.end(),local.begin(),local.end());
            }
            for(const auto member:local)impact+=word(world,entity(world,member),7);
        }
        for(const auto& portal:ri->window_portals){check(portal.target_room<map.rooms.size(),"lighting window portal target is unknown");const auto target_base=map.rooms[portal.target_room].light_base_room;
            if(map.rooms[target_base].outside)continue;const auto& window=entity(world,portal.owner);add_source(window,object_state(portal.owner),false,100,room.level,false);light.lights.back().window_room=portal.target_room;if(std::find(affected.begin(),affected.end(),target_base)==affected.end())affected.push_back(target_base);
        }
    }
    const float scale=std::max(1.0f,static_cast<float>(area)/100.0f),outside_scale=static_cast<float>(outside)/scale;
    if(outside>0){const float mul=1.0f/std::min(3.0f,std::max(1.0f,outside_scale/200.0f));for(auto& source:light.lights)if(source.outdoors_color)source.intensity*=mul;}
    cluster(light.lights,work_left);
    light.outside_light=std::min<std::uint16_t>(100,scaled_ushort(outside_scale));light.ambient_light=std::min<std::uint16_t>(100,scaled_ushort(static_cast<float>(inside)/scale));
    if(info.outside){light.ambient_light=0;light.outside_light=100;}
    light.room_score=source_room_score(impact,area,info.outside);
    RoomLightingSnapshot checked;checked.by_base_room.emplace(base,light);validate_room_lighting_snapshot(checked,map,limits);if(affected_bases)*affected_bases=std::move(affected);return light;
}
std::optional<RoomLighting> recalculate_room_lighting(const World& world,const GeometricRoomMap& map,
    const ObjectPositionRegistrySnapshot& registry,std::uint16_t requested,const RoomLightingServices& services,
    const RoomLightingLimits& limits,std::vector<std::uint16_t>* affected_bases){
    auto work_left=limits.cluster_comparisons;
    return recalculate_room_lighting_impl(world,map,registry,requested,services,limits,affected_bases,work_left);
}
RoomLightingReceipt refresh_all_room_lighting(const World& world,const GeometricRoomMap& map,
    ObjectPositionRegistrySnapshot& registry,RoomLightingSnapshot& state,const RoomLightingServices& services,
    const RoomLightingLimits& limits){
    validate_room_lighting_snapshot(state,map,limits);auto candidate=state;RoomLightingReceipt receipt;
    std::set<std::uint16_t> visited;auto work_left=limits.cluster_comparisons;
    for(std::size_t index=0;index<map.rooms.size();++index){
        const auto room=static_cast<std::uint16_t>(index);receipt.processed_rooms.push_back(room);
        if(!room)continue;
        const auto base=map.rooms[room].light_base_room;
        if(visited.insert(base).second){
            std::vector<std::uint16_t> affected;
            const auto light=recalculate_room_lighting_impl(world,map,registry,room,services,limits,&affected,work_left);
            check(light.has_value(),"full lighting refresh did not calculate a nonzero base");
            candidate.by_base_room[base]=*light;receipt.refreshed_bases.push_back(base);
            for(const auto target:affected)if(std::find(receipt.affected_bases.begin(),receipt.affected_bases.end(),target)==receipt.affected_bases.end())receipt.affected_bases.push_back(target);
        }
        // Source commit is outside the visited branch, but after room0 return.
        if(index+1==map.rooms.size())receipt.visual_commit=true;
    }
    validate_room_lighting_snapshot(candidate,map,limits);state=std::move(candidate);
    for(const auto base:receipt.refreshed_bases){const auto& light=state.by_base_room.at(base);for(auto& room:registry.rooms)if(room.id==base)room.room_score=light.room_score;
        registry.score_dirty.erase(std::remove_if(registry.score_dirty.begin(),registry.score_dirty.end(),[&](auto room){return room<map.rooms.size()&&map.rooms[room].light_base_room==base;}),registry.score_dirty.end());}
    return receipt;
}
RoomLightingReceipt consume_deferred_lighting(const World& world,const GeometricRoomMap& map,ObjectPositionRegistrySnapshot& registry,
    RoomLightingSnapshot& state,const RoomLightingServices& services,const RoomLightingLimits& limits){
    validate_room_lighting_snapshot(state,map,limits);auto candidate=state;RoomLightingReceipt receipt;
    std::set<std::uint16_t> queued;auto work_left=limits.cluster_comparisons;
    for(std::size_t i=0;i<registry.deferred_lighting.size();++i){check(i<limits.deferred_requests,"deferred lighting request budget exceeded");const auto room=registry.deferred_lighting[i];check(queued.insert(room).second,"deferred lighting queue is not source-unique");
        receipt.processed_rooms.push_back(room);std::vector<std::uint16_t> affected;const auto light=recalculate_room_lighting_impl(world,map,registry,room,services,limits,&affected,work_left);
        if(light){candidate.by_base_room[light->base_room]=*light;receipt.refreshed_bases.push_back(light->base_room);
            for(const auto target:affected)if(std::find(receipt.affected_bases.begin(),receipt.affected_bases.end(),target)==receipt.affected_bases.end())receipt.affected_bases.push_back(target);}
        receipt.visual_commit=light.has_value()&&i+1==registry.deferred_lighting.size();
    }
    validate_room_lighting_snapshot(candidate,map,limits);state=std::move(candidate);
    for(const auto base:receipt.refreshed_bases){const auto& light=state.by_base_room.at(base);for(auto& room:registry.rooms)if(room.id==base)room.room_score=light.room_score;
        registry.score_dirty.erase(std::remove_if(registry.score_dirty.begin(),registry.score_dirty.end(),[&](auto room){return room<map.rooms.size()&&map.rooms[room].light_base_room==base;}),registry.score_dirty.end());}
    registry.deferred_lighting.clear();return receipt;
}
} // namespace odh
