// SPDX-License-Identifier: MPL-2.0
#include "odh/object_position_registry.hpp"
#include "odh/room_score.hpp"
#include "odh/formats.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace odh {namespace {
bool same(ObjectPositionIdentity a,ObjectPositionIdentity b){return a.id==b.id&&a.generation==b.generation;}
bool valid(ObjectPositionIdentity id){return id.id>0&&id.generation;}
bool retired_constructor_owner(const ObjectPositionRegistrySnapshot& state,ObjectPositionIdentity old,ObjectPositionIdentity current){
    return !state.complete&&old.id==current.id&&current.generation>old.generation&&
        std::any_of(state.retired_owners.begin(),state.retired_owners.end(),[&](auto owner){return same(owner,old);});
}
std::optional<std::int16_t> word(const Entity& e,std::int16_t index){const auto i=e.object_words.find(index);return i==e.object_words.end()?std::nullopt:std::optional<std::int16_t>{i->second};}
template<class T,class P>void erase_first(std::vector<T>& items,P predicate){const auto at=std::find_if(items.begin(),items.end(),predicate);if(at!=items.end())items.erase(at);}
void add_id(std::vector<ObjectPositionIdentity>& items,ObjectPositionIdentity id){
    const auto at=std::lower_bound(items.begin(),items.end(),id.id,[](auto a,auto b){return a.id<b;});
    if(at!=items.end()&&at->id==id.id){if(!same(*at,id))throw FormatError("registry contains recycled object identity");return;}items.insert(at,id);
}
void add_room(std::vector<std::uint16_t>& items,std::uint16_t room){if(std::find(items.begin(),items.end(),room)==items.end())items.push_back(room);}
void validate_ids(const std::vector<ObjectPositionIdentity>& ids){std::int16_t previous=0;for(const auto id:ids){if(!valid(id)||id.id<=previous)throw FormatError("registry identity list invalid");previous=id.id;}}
bool box_valid(const CollisionBox& b,bool empty=false){return (empty?b.x1<=b.x2:b.x1<b.x2)&&(empty?b.y1<=b.y2:b.y1<b.y2)&&b.x1>=-65536&&b.y1>=-65536&&b.x2<=65536&&b.y2<=65536;}
auto binding(ObjectPositionRegistrySnapshot& state,ObjectPositionIdentity id){
    auto at=std::lower_bound(state.footprints.begin(),state.footprints.end(),id.id,[](const auto& value,auto owner){return value.owner.id<owner;});
    return at!=state.footprints.end()&&same(at->owner,id)?at:state.footprints.end();
}
void new_footprint(ObjectPositionRegistrySnapshot& state,ObjectPositionRegistryFootprintBinding& value,CollisionBox box){
    if(state.next_footprint_instance==UINT64_MAX)throw FormatError("registry footprint identity exhausted");
    value.present=true;value.instance=++state.next_footprint_instance;value.box=box;value.room.reset();
}
void clear_footprint(ObjectPositionRegistryFootprintBinding& value){value.present=false;value.instance=0;value.box={};value.room.reset();}
void update_visible_boxes(ObjectPositionRegistrySnapshot& state,std::uint64_t instance,CollisionBox box){
    for(auto& room:state.rooms)for(auto* collection:{&room.static_obstacles,&room.dynamic_obstacles})for(auto& item:*collection)if(item.instance==instance)item.box=box;
}
void insert_static(std::vector<ObjectPositionRegistryFootprint>& collection,const ObjectPositionRegistryFootprintBinding& value){
    erase_first(collection,[&](const auto& item){return item.instance==value.instance;});collection.push_back({value.owner,value.box,value.instance});
}
std::int32_t tile_offset(const Entity& e,std::uint32_t width,std::uint32_t height){
    if(!e.position)return -1;const auto& p=*e.position;
    if(p.level<1||p.level>2||p.x<0||p.y<0||p.x/16>=std::int32_t(width)||p.y/16>=std::int32_t(height))throw FormatError("registry position outside prepared extent");
    return p.x/16+(p.y/16)*std::int32_t(width)+(p.level-1)*std::int32_t(width*height);
}
}
void validate_object_position_registry_snapshot(const ObjectPositionRegistrySnapshot& s){
    if(!s.width||s.width!=s.height||s.width>64||s.rooms.empty()||s.rooms.size()>32768||s.tiles.size()>8193)throw FormatError("registry snapshot extent invalid");
    std::size_t entries=0,rectangles=0;
    std::map<std::uint64_t,std::pair<ObjectPositionIdentity,CollisionBox>> instances;
    const auto remember_instance=[&](std::uint64_t instance,ObjectPositionIdentity owner,CollisionBox box){
        const auto inserted=instances.emplace(instance,std::make_pair(owner,box));
        if(!inserted.second&&(!same(inserted.first->second.first,owner)||!(inserted.first->second.second==box)))
            throw FormatError("registry footprint instance has inconsistent owner or geometry");
    };
    for(std::size_t room=0;room<s.rooms.size();++room){const auto& r=s.rooms[room];if(r.id!=room)throw FormatError("registry room identity invalid");validate_ids(r.entities);entries+=r.entities.size();
        for(const auto* footprints:{&r.static_obstacles,&r.dynamic_obstacles})for(const auto& f:*footprints){if(!valid(f.owner)||!box_valid(f.box,true)||!f.instance||f.instance>s.next_footprint_instance)throw FormatError("registry footprint invalid");remember_instance(f.instance,f.owner,f.box);++entries;}
        std::set<std::uint64_t> static_ids;for(const auto& f:r.static_obstacles)if(!static_ids.insert(f.instance).second)throw FormatError("registry static footprint duplicated");
        for(const auto* links:{&r.portals,&r.window_portals})for(const auto& p:*links){if(!valid(p.owner)||p.target_room>=s.rooms.size()||p.target_room==room)throw FormatError("registry portal invalid");++entries;}
        for(std::size_t level=0;level<2;++level){if(!r.boundaries_known[level]&&!r.boundaries[level].empty())throw FormatError("unknown registry boundary contains geometry");
            for(const auto& b:r.boundaries[level])if(!box_valid({b.x1,b.y1,b.x2,b.y2}))throw FormatError("registry room boundary invalid");rectangles+=r.boundaries[level].size();}
        if(r.room_score&&(*r.room_score < -100||*r.room_score>100))throw FormatError("registry room score invalid");
    }
    std::int32_t previous=-2;for(const auto& tile:s.tiles){if(tile.offset<=previous||tile.offset < -1||tile.offset>=std::int32_t(2*s.width*s.height)||tile.entities.empty())throw FormatError("registry tile index invalid");previous=tile.offset;validate_ids(tile.entities);entries+=tile.entities.size();}
    for(const auto* dirty:{&s.deferred_lighting,&s.score_dirty}){std::set<std::uint16_t> unique;for(auto room:*dirty)if(room>=s.rooms.size()||!unique.insert(room).second)throw FormatError("registry dirty-room sequence invalid");}
    std::int16_t last_motion=0;for(const auto& motion:s.motion){if(!valid(motion.owner)||motion.owner.id<=last_motion)throw FormatError("registry motion history invalid");last_motion=motion.owner.id;}
    if(s.motion.size()>4096)throw FormatError("registry motion history budget exceeded");
    std::int16_t last_footprint=0;for(const auto& f:s.footprints){if(!valid(f.owner)||f.owner.id<=last_footprint)throw FormatError("registry footprint binding identity invalid");last_footprint=f.owner.id;
        if(f.present){if(!f.instance||f.instance>s.next_footprint_instance||!box_valid(f.box,true)||(f.room&&*f.room>=s.rooms.size()))throw FormatError("registry footprint binding invalid");remember_instance(f.instance,f.owner,f.box);}
        else if(f.instance||f.room||!(f.box==CollisionBox{}))throw FormatError("absent registry footprint binding contains state");}
    if(s.footprints.size()>4096)throw FormatError("registry footprint binding budget exceeded");
    if(s.retired_owners.size()>65536||(s.complete&&!s.retired_owners.empty()))throw FormatError("registry retired owners require incomplete bounded state");
    ObjectPositionIdentity last_retired{};
    for(const auto owner:s.retired_owners){
        if(!valid(owner)||owner.id<last_retired.id||(owner.id==last_retired.id&&owner.generation<=last_retired.generation))
            throw FormatError("registry retired owner sequence invalid");
        last_retired=owner;
    }
    entries+=s.retired_owners.size();
    if(entries>65536||rectangles>65536)throw FormatError("registry snapshot resource budget exceeded");
}
bool object_position_known_absent_oow(const ObjectPositionRegistrySnapshot& state,const Entity& body,std::string& error){
    const auto fail=[&](const char* text){error=text;return false;};
    const auto heading=body.object_words.find(1);
    if(!state.complete||body.id<=0||!body.generation||body.ui_ghost||body.position||body.attachment||
       !body.native_out_of_world_level||heading==body.object_words.end()||heading->second<0||heading->second>7)
        return fail("known-absent OOW requires complete registry and exact known native scalar without physical parent");
    unsigned footprints=0,motions=0;
    for(const auto& value:state.footprints)if(value.owner.id==body.id){
        if(value.owner.generation!=body.generation||value.room)return fail("known-absent OOW footprint metadata is stale or room-bound");
        ++footprints;
    }
    for(const auto& value:state.motion)if(value.owner.id==body.id){
        if(value.owner.generation!=body.generation)return fail("known-absent OOW motion metadata has stale generation");
        ++motions;
    }
    if(footprints!=1||motions!=1)return fail("known-absent OOW requires unique exact Footprint and MovedSelf metadata");
    for(const auto& tile:state.tiles)for(const auto owner:tile.entities)if(owner.id==body.id)
        return fail("known-absent OOW still occurs in a tile collection");
    for(const auto& room:state.rooms){
        for(const auto owner:room.entities)if(owner.id==body.id)return fail("known-absent OOW still occurs in RoomInfo entities");
        for(const auto* values:{&room.static_obstacles,&room.dynamic_obstacles})for(const auto& value:*values)
            if(value.owner.id==body.id)return fail("known-absent OOW still occurs in a RoomInfo obstacle collection");
        for(const auto* values:{&room.portals,&room.window_portals})for(const auto& value:*values)
            if(value.owner.id==body.id)return fail("known-absent OOW still occurs in a portal collection");
    }
    error.clear();return true;
}
std::optional<bool> known_object_moved_self(const ObjectPositionRegistrySnapshot& state,ObjectPositionIdentity id){
    const auto at=std::lower_bound(state.motion.begin(),state.motion.end(),id.id,[](const auto& motion,auto owner){return motion.owner.id<owner;});
    return at!=state.motion.end()&&same(at->owner,id)?std::optional<bool>{at->moved_self}:std::nullopt;
}
bool record_object_moved_self(ObjectPositionRegistrySnapshot& state,const World& world,ObjectPositionIdentity id,bool value,const std::function<bool()>& authority){
    if(!authority||!authority())return false;const auto* e=world.find(id.id);if(!e||e->generation!=id.generation||e->ui_ghost||
        (e->kind!=EntityKind::object&&e->kind!=EntityKind::avatar))return false;
    auto at=std::lower_bound(state.motion.begin(),state.motion.end(),id.id,[](const auto& item,auto owner){return item.owner.id<owner;});
    if(at!=state.motion.end()&&at->owner.id==id.id){
        if(!same(at->owner,id)){if(!retired_constructor_owner(state,at->owner,id))return false;at->owner=id;}
        at->moved_self=value;return true;
    }
    if(state.motion.size()>=4096)return false;state.motion.insert(at,{id,value});return true;
}
bool record_object_footprint_absent(ObjectPositionRegistrySnapshot& state,const World& world,ObjectPositionIdentity id,const std::function<bool()>& authority){
    if(!authority||!authority())return false;const auto* e=world.find(id.id);if(!e||e->generation!=id.generation||e->ui_ghost)return false;
    const auto at=std::lower_bound(state.footprints.begin(),state.footprints.end(),id.id,[](const auto& value,auto owner){return value.owner.id<owner;});
    if(at!=state.footprints.end()&&at->owner.id==id.id){
        if(same(at->owner,id))return !at->present;
        if(!retired_constructor_owner(state,at->owner,id))return false;
        // Only the actual current constructor's metadata is replaced. Old
        // RoomInfo/query/obstacle references remain opaque and retired-tagged.
        *at={};at->owner=id;return true;
    }
    if(state.footprints.size()>=4096)return false;ObjectPositionRegistryFootprintBinding fresh;fresh.owner=id;state.footprints.insert(at,std::move(fresh));return true;
}
ObjectPositionRegistry::ObjectPositionRegistry(World& world,const PreparedArchitecture& architecture,const GeometricRoomMap& rooms,ObjectPositionRegistryServices services)
    :world_(world),architecture_(architecture),rooms_(rooms),services_(std::move(services)){
    if(!architecture.width||architecture.width!=architecture.height||architecture.width>64||rooms.width!=architecture.width||rooms.height!=architecture.height||rooms.rooms.empty())throw FormatError("registry prepared/room extent mismatch");
    state_.width=architecture.width;state_.height=architecture.height;
    if(rooms.rooms.size()>32768)throw FormatError("registry room count exceeds budget");
    for(const auto& room:rooms.rooms){if(room.id!=state_.rooms.size()||room.level>1)throw FormatError("registry room metadata invalid");ObjectPositionRegistryRoom entry;entry.id=room.id;
        if(room.id){const auto boundary=generate_room_obstacle_candidates(rooms,room.id,room.level,{},true);entry.boundaries[room.level]=boundary.rectangles;entry.boundaries_known[room.level]=boundary.complete;}
        state_.rooms.push_back(std::move(entry));}
    std::int16_t cursor=0;while(const auto next=world.next_entity(cursor)){cursor=*next;const auto* e=world.find(cursor);
        if(!record_object_footprint_absent(state_,world,{cursor,e->generation},[]{return true;}))throw FormatError("fresh registry footprint inventory budget exceeded");}
    validate_object_position_registry_snapshot(state_);
}
ObjectPositionRegistry::ObjectPositionRegistry(World& world,const PreparedArchitecture& architecture,const GeometricRoomMap& rooms,ObjectPositionRegistryServices services,ObjectPositionRegistrySnapshot snapshot)
    :world_(world),architecture_(architecture),rooms_(rooms),services_(std::move(services)),state_(std::move(snapshot)){
    validate_object_position_registry_snapshot(state_);
    if(state_.width!=architecture.width||state_.height!=architecture.height||rooms.width!=state_.width||rooms.height!=state_.height||rooms.rooms.size()!=state_.rooms.size())throw FormatError("restored registry geometry mismatch");
}
const Entity* ObjectPositionRegistry::entity(ObjectPositionIdentity id,const std::function<bool()>& authority)const{
    if(!authority||!authority())return nullptr;const auto* e=world_.find(id.id);return e&&e->generation==id.generation&&!e->ui_ghost?e:nullptr;
}
std::optional<CollisionShapeResult> ObjectPositionRegistry::footprint_shape(ObjectPositionIdentity id,const std::function<bool()>& authority)const{
    const auto* e=entity(id,authority);if(!e)return {};const auto flags=word(*e,8);if(!flags)return {};
    auto position=e->position.value_or(EntityPosition{0,0,1,0});
    if(!e->position){const auto direction=word(*e,1);if(!direction||*direction<0||*direction>7)return {};position.direction=std::uint8_t(*direction);}
    CollisionShapeResult shape{CollisionShapeState::absent,{}};
    if(e->kind==EntityKind::avatar){if(!e->kill_timeout)return {};if(!e->attachment&&(*e->kill_timeout<=-1||(std::uint16_t(*flags)&4U)))shape=avatar_candidate_collision_shape(position);}
    else if(!e->attachment&&!(std::uint16_t(*flags)&4U)){
        if(!services_.definitions||!e->definition)return {};const auto* definition=services_.definitions->unique(e->definition->guid);if(!definition)return {};
        shape=object_collision_shape(definition->definition,position,std::uint16_t(*flags),false);}
    if(shape.state==CollisionShapeState::unsupported)return {};
    if(!e->position&&shape.state==CollisionShapeState::present){shape.box.x1-=32768;shape.box.x2-=32768;shape.box.y1-=32768;shape.box.y2-=32768;}
    return shape;
}
std::optional<bool> ObjectPositionRegistry::moves_often(ObjectPositionIdentity id,const std::function<bool()>& authority,std::vector<std::int16_t>& path)const{
    const auto* e=entity(id,authority);if(!e||path.size()>=4096||std::find(path.begin(),path.end(),id.id)!=path.end())return {};
    if(e->kind==EntityKind::avatar)return true;
    path.push_back(id.id);struct Pop{std::vector<std::int16_t>& p;~Pop(){p.pop_back();}}pop{path};
    if(e->attachment){
        const auto parent=world_.attachment_parent({id.id,id.generation});if(!parent)return {};
        if(world_.is_live_reference(*parent))return moves_often({parent->id,parent->generation},authority,path);
        if(!services_.retained_moves_often||!world_.resolve_reference(*parent))return {};
        const auto value=services_.retained_moves_often({parent->id,parent->generation});
        const auto* after=entity(id,authority);const auto actual=world_.attachment_parent({id.id,id.generation});
        return after&&actual&&*actual==*parent&&!world_.is_live_reference(*parent)&&world_.resolve_reference(*parent)?value:std::nullopt;
    }
    if(!services_.routing_slot_count)return {};
    const auto count=services_.routing_slot_count(id);if(!count||!entity(id,authority))return {};if(*count>20)return true;
    const auto known=std::find_if(state_.motion.begin(),state_.motion.end(),[&](const auto& motion){return same(motion.owner,id);});
    if(known!=state_.motion.end())return known->moved_self;
    if(!services_.moved_self)return {};
    const auto moved=services_.moved_self(id);return entity(id,authority)?moved:std::nullopt;
}
bool ObjectPositionRegistry::refresh_score(std::uint16_t room,const std::function<bool()>& authority){
    if(room==0)return true;if(room>=rooms_.rooms.size()||!rooms_.light_base_rooms_resolved)return false;
    const auto base=rooms_.rooms[room].light_base_room;if(!base||base>=rooms_.rooms.size())return false;
    std::int64_t impact=0;std::uint64_t area=0;bool outside=false;
    for(const auto& support:rooms_.rooms)if(support.light_base_room==base){area+=support.area;outside=support.outside;
        for(const auto id:state_.rooms[support.id].entities){const auto* e=entity(id,authority);if(!e)return false;const auto value=word(*e,7);if(!value)return false;impact+=*value;}}
    if(impact<INT32_MIN||impact>INT32_MAX)return false;
    const auto score=source_room_score(static_cast<std::int32_t>(impact),area,outside);
    state_.rooms[base].room_score=static_cast<std::int16_t>(score);add_room(state_.score_dirty,base);return true;
}
bool ObjectPositionRegistry::regenerate_boundary(std::uint16_t room,std::int16_t level,const std::function<bool()>& authority){
    if(room>=state_.rooms.size()||level<1||level>2)return false;
    std::vector<RoomPortalOpening> openings;
    const auto cells=std::int32_t(state_.width*state_.height);
    // Source GenerateRoomObs queries the EVENT-OWNED tile index, before the
    // current Register/Unregister operation's final tile-index mutation.
    for(const auto& tile:state_.tiles){if(tile.offset<0||tile.offset/cells!=level-1)continue;
        std::optional<ObjectPositionIdentity> door;
        for(const auto id:tile.entities){const auto* e=entity(id,authority);if(!e)return false;const auto flags=word(*e,40);if(!flags)return false;if(std::uint16_t(*flags)&0x8000U){door=id;break;}}
        if(!door)continue;if(!services_.group)return false;const auto group=services_.group(*door);if(!group||group->size()>4096)return false;
        for(const auto id:*group){const auto* e=entity(id,authority);if(!e||!e->entry_points||e->entry_points->size()<=15)return false;
            const auto target=e->position?geometric_room_at_runtime(rooms_,e->position->x,e->position->y,e->position->level):std::optional<std::uint16_t>{0};if(!target)return false;
            if(*target==room&&e->entry_points->at(15).action){const auto tile_index=tile.offset%cells;openings.push_back({room,std::uint32_t(tile_index%std::int32_t(state_.width)),std::uint32_t(tile_index/std::int32_t(state_.width)),std::uint8_t(level-1)});break;}}
    }
    const auto boundary=generate_room_obstacle_candidates(rooms_,room,std::uint8_t(level-1),openings,true);if(!boundary.complete)return false;
    state_.rooms[room].boundaries[std::size_t(level-1)]=boundary.rectangles;state_.rooms[room].boundaries_known[std::size_t(level-1)]=true;return true;
}
bool ObjectPositionRegistry::apply(ObjectPositionIdentity id,std::optional<std::uint16_t> supplied_room,bool room_change,bool adding,const std::function<bool()>& authority){
    if(executing_||!entity(id,authority))return false;executing_=true;struct Reset{bool& v;~Reset(){v=false;}}reset{executing_};bool touched=false;
    try{
        const auto* e=entity(id,authority);const auto original_position=e->position;
        const auto room=e->position?geometric_room_at_runtime(rooms_,e->position->x,e->position->y,e->position->level):std::optional<std::uint16_t>{0};
        if(!room||*room>=state_.rooms.size()||(supplied_room&&*supplied_room!=*room)||!e->entry_points||e->entry_points->size()<=15)return false;
        const auto flags=word(*e,40),light=word(*e,51),hidden=word(*e,34),impact=word(*e,7);if(!flags||!light||!hidden||!impact)return false;
        std::vector<std::int16_t> path;const auto often=moves_often(id,authority,path);if(!often)return false;
        const auto offset=tile_offset(*e,state_.width,state_.height);const bool portal=e->entry_points->at(15).action!=0,window=(std::uint16_t(*flags)&0x4000U)!=0;
        std::optional<std::uint16_t> portal_target;
        if(adding&&room_change&&(portal||window)){
            if(!services_.group)return false;const auto group=services_.group(id);if(!group||group->size()>4096)return false;
            for(const auto other:*group){const auto* part=entity(other,authority);if(!part)return false;if(same(id,other))continue;
                const auto other_room=part->position?geometric_room_at_runtime(rooms_,part->position->x,part->position->y,part->position->level):std::optional<std::uint16_t>{0};if(!other_room)return false;
                if(*other_room==*room)continue;
                if(portal&&(!part->entry_points||part->entry_points->size()<=15))return false;
                if(!portal||part->entry_points->at(15).action){portal_target=*other_room;break;}}
        }
        e=entity(id,authority);if(!e||e->position.has_value()!=original_position.has_value()||(original_position&&!(*e->position==*original_position)))return false;
        auto bound=binding(state_,id);if(bound==state_.footprints.end())return false;
        std::optional<CollisionShapeResult> footprint;if(adding){footprint=footprint_shape(id,authority);if(!footprint)return false;}
        auto& target=state_.rooms[*room];const bool stat=e->static_footprint;
        touched=true;
        if(adding){if(footprint->state==CollisionShapeState::present){
                if(bound->present){bound->box=footprint->box;update_visible_boxes(state_,bound->instance,bound->box);}
                else new_footprint(state_,*bound,footprint->box);
            }else clear_footprint(*bound);}
        if(room_change){
            if(adding){add_id(target.entities,id);
                if(bound->present){bound->room=*room;if(stat)insert_static(target.static_obstacles,*bound);else target.dynamic_obstacles.push_back({id,bound->box,bound->instance});}
                if(portal_target)(portal?target.portals:target.window_portals).push_back({id,*portal_target});
            }else{erase_first(target.entities,[&](auto item){return same(item,id);});
                if(bound->present&&bound->room){auto& registered=state_.rooms[*bound->room];erase_first(stat?registered.static_obstacles:registered.dynamic_obstacles,[&](const auto& item){return item.instance==bound->instance;});bound->room.reset();}
                if(portal||window)erase_first(portal?target.portals:target.window_portals,[&](const auto& item){return same(item.owner,id);});}
            if(portal&&!regenerate_boundary(*room,original_position?original_position->level:std::int16_t{1},authority))throw FormatError("registry source room obstacle regeneration unavailable");
            if(adding){VmFrame frame;frame.context.caller=id.id;if(!world_.write(frame,3,29,static_cast<std::int16_t>(std::int32_t(*room)-1)))throw FormatError("registry source SetRoom unavailable");}
            const bool lighting=(*light>0||!*often)&&(!adding||*hidden==0);
            if(lighting)add_room(state_.deferred_lighting,*room);else if(*impact>0&&!refresh_score(*room,authority))throw FormatError("registry source room score unavailable");
        }else{
            if(stat&&bound->present&&bound->room){if(adding)insert_static(target.static_obstacles,*bound);
                else erase_first(state_.rooms[*bound->room].static_obstacles,[&](const auto& item){return item.instance==bound->instance;});}
            if(*light>0||!*often)add_room(state_.deferred_lighting,*room);
        }
        auto tile=std::lower_bound(state_.tiles.begin(),state_.tiles.end(),offset,[](const auto& row,auto value){return row.offset<value;});
        if(adding){if(tile==state_.tiles.end()||tile->offset!=offset)tile=state_.tiles.insert(tile,{offset,{}});add_id(tile->entities,id);}
        else if(tile!=state_.tiles.end()&&tile->offset==offset){erase_first(tile->entities,[&](auto item){return same(item,id);});if(tile->entities.empty())state_.tiles.erase(tile);}
        validate_object_position_registry_snapshot(state_);return entity(id,authority)!=nullptr;
    }catch(...){if(touched)state_.complete=false;return false;}
}
bool ObjectPositionRegistry::register_position(ObjectPositionIdentity id,std::uint16_t room,bool change,const std::function<bool()>& authority){return apply(id,room,change,true,authority);}
bool ObjectPositionRegistry::unregister_position(ObjectPositionIdentity id,bool change,const std::function<bool()>& authority){return apply(id,{},change,false,authority);}
bool ObjectPositionRegistry::finish_initial_load(const std::vector<ObjectPositionIdentity>& source,const std::function<bool()>& authority){
    if(executing_||source.size()>4096||!state_.retired_owners.empty()||!authority||!authority())return false;
    try{std::set<std::int16_t> unique;for(const auto id:source){const auto* e=entity(id,authority);if(!e||!unique.insert(id.id).second)return false;
            const auto offset=tile_offset(*e,state_.width,state_.height);const auto tile=std::find_if(state_.tiles.begin(),state_.tiles.end(),[&](const auto& value){return value.offset==offset;});
            if(tile==state_.tiles.end()||std::none_of(tile->entities.begin(),tile->entities.end(),[&](auto item){return same(item,id);}))return false;}
        std::size_t count=0;std::int16_t cursor=0;while(const auto next=world_.next_entity(cursor)){cursor=*next;if(!unique.count(cursor))return false;++count;}
        if(count!=source.size())return false;
        for(const auto id:source){const auto* e=entity(id,authority);if(!e)return false;if(e->kind==EntityKind::avatar)continue;
            if(known_object_moved_self(state_,id).has_value())continue;
            if(!services_.moved_self)return false;const auto moved=services_.moved_self(id);if(!moved||!record_moved_self(id,*moved,authority))return false;}
        validate_object_position_registry_snapshot(state_);state_.complete=true;return true;
    }catch(...){return false;}
}
bool ObjectPositionRegistry::record_moved_self(ObjectPositionIdentity id,bool value,const std::function<bool()>& authority){
    return record_object_moved_self(state_,world_,id,value,authority);
}
bool ObjectPositionRegistry::defer_lighting(std::uint16_t room,const std::function<bool()>& authority){
    if(!authority||!authority()||room>=state_.rooms.size())return false;
    add_room(state_.deferred_lighting,room);
    return true;
}
std::optional<bool> ObjectPositionRegistry::query_moves_often(ObjectPositionIdentity id,const std::function<bool()>& authority)const{
    std::vector<std::int16_t> path;return moves_often(id,authority,path);
}
bool ObjectPositionRegistry::static_footprint_changed(ObjectPositionIdentity id,bool new_static,const std::function<bool()>& authority){
    if(executing_)return false;const auto* e=entity(id,authority);if(!e||e->static_footprint!=new_static)return false;
    const auto bound=binding(state_,id);if(bound==state_.footprints.end()){state_.complete=false;return false;}
    if(!bound->present||!bound->room)return true;
    try{
        auto& room=state_.rooms[*bound->room];
        if(new_static){erase_first(room.dynamic_obstacles,[&](const auto& item){return item.instance==bound->instance;});insert_static(room.static_obstacles,*bound);}
        else{room.dynamic_obstacles.push_back({id,bound->box,bound->instance});erase_first(room.static_obstacles,[&](const auto& item){return item.instance==bound->instance;});}
        validate_object_position_registry_snapshot(state_);return entity(id,authority)!=nullptr;
    }catch(...){state_.complete=false;return false;}
}
bool ObjectPositionRegistry::update_footprint(ObjectPositionIdentity id,const std::function<bool()>& authority){
    if(executing_)return false;const auto* e=entity(id,authority);if(!e)return false;
    auto bound=binding(state_,id);if(bound==state_.footprints.end()){state_.complete=false;return false;}
    const auto shape=footprint_shape(id,authority);if(!shape)return false;
    try{
        if(bound->present&&shape->state==CollisionShapeState::present){bound->box=shape->box;update_visible_boxes(state_,bound->instance,bound->box);return true;}
        if(bound->present){if(bound->room){auto& room=state_.rooms[*bound->room];erase_first(e->static_footprint?room.static_obstacles:room.dynamic_obstacles,[&](const auto& item){return item.instance==bound->instance;});}clear_footprint(*bound);return true;}
        if(shape->state==CollisionShapeState::absent)return true;
        const auto room=e->position?geometric_room_at_runtime(rooms_,e->position->x,e->position->y,e->position->level):std::optional<std::uint16_t>{0};
        if(!room||*room>=state_.rooms.size())return false;
        new_footprint(state_,*bound,shape->box);bound->room=*room;
        if(e->static_footprint)insert_static(state_.rooms[*room].static_obstacles,*bound);
        else state_.rooms[*room].dynamic_obstacles.push_back({id,bound->box,bound->instance});
        validate_object_position_registry_snapshot(state_);return entity(id,authority)!=nullptr;
    }catch(...){state_.complete=false;return false;}
}
RoomInfoObstacleQuery ObjectPositionRegistry::query(std::uint16_t room,const CollisionBox& candidate,std::int32_t target_id)const{
    RoomInfoObstacleQuery result;if(!state_.complete||room>=state_.rooms.size()||!box_valid(candidate))return result;
    result.status=RoomInfoObstacleIndexStatus::complete;result.reason=RoomInfoObstacleIndexReason::none;
    const auto& r=state_.rooms[room];for(const auto* collection:{&r.static_obstacles,&r.dynamic_obstacles})for(const auto& item:*collection){
        const auto* owner=world_.find(item.owner.id);if(!owner||owner->generation!=item.owner.generation){result.status=RoomInfoObstacleIndexStatus::unsupported;result.reason=RoomInfoObstacleIndexReason::owner_identity_invalid;result.intersections.clear();return result;}
        if(collection==&r.dynamic_obstacles&&item.owner.id==target_id)continue;
        if(collision_boxes_intersect(item.box,candidate))result.intersections.push_back({item.owner.id,room,item.box,collection==&r.static_obstacles?RoomInfoObstacleCollection::static_set:RoomInfoObstacleCollection::dynamic_list});}
    return result;
}
}
