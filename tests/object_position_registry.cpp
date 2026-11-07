// SPDX-License-Identifier: MPL-2.0
#include "odh/object_position_registry.hpp"
#include "odh/formats.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace odh;
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class F>void malformed(F f,const char* why){bool failed=false;try{f();}catch(const FormatError&){failed=true;}require(failed,why);}
ObjectDefinition definition(){ObjectDefinition d{};d.version=138;d.guid=100;d.tile_width=16;d.footprint_mask=0;return d;}
struct Fixture {
    World world;PreparedArchitecture prepared;GeometricRoomMap rooms;DefinitionIndex definitions;
    std::vector<ObjectPositionIdentity> group_order;bool authenticated=true;std::size_t slot_count=0;
    Fixture():definitions({{"fixture","",128,definition()}}){
        prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        for(auto& level:prepared.levels){level.floors.assign(16,1);level.walls.resize(16);}
        // Explicit source-format room snapshot: two ground rooms and one upper
        // room. This fixture tests registration, not commercial room discovery.
        rooms.width=rooms.height=4;rooms.floor_resources_resolved=true;rooms.light_base_rooms_resolved=true;
        rooms.rooms={{},{1,0,8,{0,0,2,4},false,false,{},1},{2,0,8,{2,0,2,4},false,false,{},2},{3,1,16,{0,0,4,4},false,false,{},3}};
        rooms.levels[0].resize(16);rooms.levels[1].assign(16,0x00030003U);
        for(std::size_t i=0;i<16;++i)rooms.levels[0][i]=(i%4<2)?0x00010001U:0x00020002U;
        for(std::int16_t id=1;id<=5;++id){require(world.create(id,EntityKind::object,{0,0,2})&&world.define_type(id,100),"actual source registration object");
            for(std::int16_t field:{1,7,8,29,34,40,51})require(world.declare_object_word(id,field,0),"actual source registration word");
            std::vector<EntryPoint> entries(27);if(id<=2)entries[15].action=500;
            require(world.define_entry_points(id,entries)&&world.set_position(id,{id<=1?24:40,24,1,0}),"actual object resource/pose");}
        word(1,40,static_cast<std::int16_t>(0x8000));word(2,40,static_cast<std::int16_t>(0x8000));
        require(world.set_position(4,{24,40,1,0}),"actual window position");word(4,40,0x4000);
        group_order={identity(1),identity(3),identity(2)};
    }
    ObjectPositionIdentity identity(std::int16_t id)const{const auto* e=world.find(id);return {id,e->generation};}
    void word(std::int16_t id,std::int16_t index,std::int16_t value){VmFrame f;f.context.caller=id;require(world.write(f,3,index,value),"actual source object word mutation");}
    ObjectPositionRegistryServices services(){ObjectPositionRegistryServices s;s.definitions=&definitions;
        s.group=[&](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{if(id.id==4)return std::vector<ObjectPositionIdentity>{identity(4),identity(3)};return group_order;};
        s.routing_slot_count=[&](auto)->std::optional<std::size_t>{return slot_count;};s.moved_self=[](auto)->std::optional<bool>{return false;};return s;}
    std::function<bool()> authority(){return [&]{return authenticated;};}
    std::uint16_t room(std::int16_t id)const{const auto* e=world.find(id);return *geometric_room_at_runtime(rooms,e->position->x,e->position->y,e->position->level);}
};
bool contains(const std::vector<std::uint16_t>& values,std::uint16_t value){return std::find(values.begin(),values.end(),value)!=values.end();}
void native_avatar_motion(){
    World world;require(world.create(1,EntityKind::avatar),"native avatar motion owner");
    ObjectPositionRegistrySnapshot state;const ObjectPositionIdentity ref{1,world.find(1)->generation};
    require(!record_object_moved_self(state,world,ref,true,[]{return false;})&&state.motion.empty(),"avatar motion requires actual authority");
    require(record_object_moved_self(state,world,ref,true,[]{return true;})&&
        known_object_moved_self(state,ref)==std::optional<bool>{true},"native4341a6 records person as well as object motion");
    require(!record_object_moved_self(state,world,{1,ref.generation+1},false,[]{return true;})&&
        known_object_moved_self(state,ref)==std::optional<bool>{true},"avatar stale generation cannot overwrite actual motion");
}
void registration(){
    Fixture f;auto services=f.services();ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,services);
    require(registry.unregister_position(f.identity(5),true,f.authority()),"source child may unregister before any registration");
    require(registry.snapshot().rooms[2].entities.empty(),"early unregister is a list no-op");
    require(registry.register_position(f.identity(1),1,true,f.authority()),"door register runs native source event");
    const auto& first=registry.snapshot().rooms[1];
    require(first.entities.size()==1&&first.static_obstacles.size()==1&&first.portals.size()==1&&first.portals[0].target_room==2&&first.window_portals.empty(),
        "portal picks first OTHER portal member, skipping same-room/nonportal group members");
    require(f.world.find(1)->object_words.at(29)==0&&contains(registry.snapshot().deferred_lighting,1),"source SetRoom and stationary furniture lighting are owned");
    require(registry.register_position(f.identity(4),1,true,f.authority()),"source window registers");
    require(registry.snapshot().rooms[1].window_portals.size()==1&&registry.snapshot().rooms[1].window_portals[0].target_room==2,
        "window accepts first different-room group member regardless of its Portal flag");
    require(registry.register_position(f.identity(1),1,true,f.authority())&&registry.snapshot().rooms[1].entities.size()==2&&
        registry.snapshot().rooms[1].static_obstacles.size()==2&&registry.snapshot().rooms[1].portals.size()==2,
        "source room list and static set are idempotent while portal list appends");
    require(registry.unregister_position(f.identity(1),true,f.authority())&&registry.snapshot().rooms[1].portals.size()==1,
        "source portal removal removes only first matching edge");
    require(registry.snapshot().rooms[1].entities.size()==1&&registry.snapshot().rooms[1].static_obstacles.size()==1&&registry.snapshot().rooms[1].boundaries_known[0],
        "source removal owns room membership, footprint and regenerated boundary");
    const auto before=registry.snapshot().tiles.size();f.authenticated=false;
    require(!registry.register_position(f.identity(3),2,true,f.authority())&&registry.snapshot().tiles.size()==before,"foreign event has no registration effects");
    f.authenticated=true;
    require(f.world.place_out_of_world(5)&&registry.register_position(f.identity(5),0,true,f.authority())&&
        registry.snapshot().rooms[0].static_obstacles.size()==1&&registry.snapshot().rooms[0].static_obstacles.front().box.x1 < -32768&&
        registry.snapshot().tiles.front().offset==-1,"source OOW registers Room0 sentinel footprint and separate query key -1");
}
void lighting_and_queries(){
    Fixture f;ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());
    f.word(3,34,1);f.word(3,7,30);
    require(registry.register_position(f.identity(3),2,true,f.authority()),"hidden stationary object registers");
    require(!contains(registry.snapshot().deferred_lighting,2)&&registry.snapshot().rooms[2].room_score==std::optional<std::int16_t>{20},
        "roomChange register Hidden guard suppresses lighting and positive impact immediately updates score");
    require(registry.unregister_position(f.identity(3),true,f.authority())&&contains(registry.snapshot().deferred_lighting,2),
        "unregister has no Hidden guard for stationary object lighting");
    require(f.world.set_static_footprint(3,false)&&registry.record_moved_self(f.identity(3),true,f.authority()),"actual moving-object state");
    require(registry.register_position(f.identity(3),2,true,f.authority())&&registry.register_position(f.identity(3),2,true,f.authority())&&
        registry.snapshot().rooms[2].dynamic_obstacles.size()==2&&registry.snapshot().rooms[2].entities.size()==1,
        "source dynamic obstacle list preserves duplicate registration while entities stay unique");
    require(registry.unregister_position(f.identity(3),false,f.authority())&&registry.snapshot().rooms[2].dynamic_obstacles.size()==2,
        "same-room unregister leaves dynamic list in place");
    require(registry.unregister_position(f.identity(3),true,f.authority())&&registry.snapshot().rooms[2].dynamic_obstacles.size()==1,
        "room-change unregister removes first dynamic occurrence");
    // Separate complete source load for query filtering and exact restoration.
    Fixture q;require(q.world.set_static_footprint(3,false),"dynamic query source");ObjectPositionRegistry complete(q.world,q.prepared,q.rooms,q.services());
    std::vector<ObjectPositionIdentity> order;for(std::int16_t id=1;id<=5;++id){order.push_back(q.identity(id));require(complete.register_position(q.identity(id),q.room(id),true,q.authority()),"complete actual source registration");}
    require(complete.record_moved_self(q.identity(3),true,q.authority())&&complete.finish_initial_load(order,q.authority())&&
        known_object_moved_self(complete.snapshot(),q.identity(3))==std::optional<bool>{true},"load completion preserves real MovedSelf event over bootstrap fallback");
    const CollisionBox candidate{30,14,50,34};const auto hits=complete.query(2,candidate,3);
    require(hits.status==RoomInfoObstacleIndexStatus::complete&&std::none_of(hits.intersections.begin(),hits.intersections.end(),[](const auto& hit){return hit.owner_id==3;})&&
        std::any_of(hits.intersections.begin(),hits.intersections.end(),[](const auto& hit){return hit.owner_id==2;}),"native registry query excludes only dynamic target and includes static intersections");
    auto restored_services=q.services();restored_services.moved_self={};ObjectPositionRegistry resumed(q.world,q.prepared,q.rooms,restored_services,complete.snapshot());
    require(resumed.unregister_position(q.identity(3),false,q.authority()),"native restored MovedSelf owner works without replay/defaultfalse");
    unsigned transitions=0;
    require(q.world.set_static_footprint_observer([&](World& actual,std::int16_t id,std::uint64_t generation,bool old,bool value){
        ++transitions;require(&actual==&q.world&&old!=value&&actual.find(id)->static_footprint==value,"observer owns actual changed World and identity");
        require(!actual.set_static_footprint(id,value),"observer cannot recursively publish another collection event");
        return resumed.static_footprint_changed({id,generation},value,q.authority());}),"install actual collection observer");
    require(q.world.set_static_footprint(2,false)&&q.world.set_static_footprint(2,false)&&transitions==1&&
        std::none_of(resumed.snapshot().rooms[2].static_obstacles.begin(),resumed.snapshot().rooms[2].static_obstacles.end(),[](const auto& item){return item.owner.id==2;}),
        "source SetObstacleStatic moves bound footprint once and equal value no-ops");
    require(q.world.set_static_footprint(2,true)&&transitions==2&&
        std::any_of(resumed.snapshot().rooms[2].static_obstacles.begin(),resumed.snapshot().rooms[2].static_obstacles.end(),[](const auto& item){return item.owner.id==2;}),
        "source walk end restores static membership through actual observer");
    require(q.world.set_static_footprint(2,false,FootprintClassChangeSource::flags_assignment)&&transitions==2&&
        q.world.set_static_footprint(2,true,FootprintClassChangeSource::flags_assignment)&&transitions==2,
        "source Flags assignment changes property without fabricating SetObstacleStatic collection event");
    require(q.world.set_static_footprint_observer([](auto&,auto,auto,bool,bool){return false;})&&
        !q.world.set_static_footprint(2,false)&&!q.world.find(2)->static_footprint,
        "incomplete observer returns false while retaining already-published source property");
    auto constructor_state=complete.snapshot();const auto before_motion=constructor_state.motion.size();
    require(!record_object_moved_self(constructor_state,q.world,{2,q.identity(2).generation+1},true,q.authority())&&constructor_state.motion.size()==before_motion,
        "free motion producer authenticates actual identity");
    require(record_object_moved_self(constructor_state,q.world,q.identity(2),true,q.authority())&&known_object_moved_self(constructor_state,q.identity(2))==std::optional<bool>{true},
        "source constructor/movement can record motion without reconstructing registry");
    auto malformed_state=complete.snapshot();malformed_state.tiles.front().entities.push_back(malformed_state.tiles.front().entities.front());
    malformed([&]{validate_object_position_registry_snapshot(malformed_state);},"duplicate tile identity is rejected");
}
void containment(){
    Fixture f;require(f.world.attach(1,0,5)&&f.world.refresh_contained_position(5),"actual contained source position");
    ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());
    require(registry.register_position(f.identity(5),1,true,f.authority())&&registry.snapshot().rooms[1].entities.size()==1&&
        registry.snapshot().rooms[1].static_obstacles.empty()&&registry.snapshot().tiles.size()==1&&registry.snapshot().tiles.front().entities.front().id==5,
        "contained objects register RoomInfo and tile query, but no object footprint");
    require(f.world.create(6,EntityKind::avatar,{0,0,1})&&f.world.define_type(6,100)&&f.world.set_position(6,{24,24,1,0}),"actual avatar carrier");
    for(std::int16_t field:{1,7,8,29,34,40,51})require(f.world.declare_object_word(6,field,0),"avatar registration words");
    require(f.world.define_entry_points(6,std::vector<EntryPoint>(27))&&f.world.detach(1,0)&&f.world.attach(6,0,5)&&f.world.refresh_contained_position(5),"actual avatar-held object");
    auto no_motion=f.services();no_motion.moved_self={};no_motion.routing_slot_count={};ObjectPositionRegistry carried(f.world,f.prepared,f.rooms,no_motion);
    require(carried.register_position(f.identity(5),1,true,f.authority())&&carried.snapshot().deferred_lighting.empty(),
        "MovesOften follows container avatar without inventing child motion or SLOT metadata");
}
void retired_container_moves_often(){
    for(unsigned mode=0;mode<3;++mode){
        Fixture f;
        const auto parent_id=static_cast<std::int16_t>(mode==2?6:5);
        if(mode==2)require(f.world.create(6,EntityKind::avatar,{0,88,1})&&f.world.set_position(6,{40,24,1,0}),"actual avatar container identity");
        require(f.world.place_in_slot(parent_id,0,3),"source child belongs to actual container before removal");
        const WorldEntityReference parent{parent_id,f.world.find(parent_id)->generation};
        const auto child=f.identity(3);
        auto services=f.services();World::RetiredContainer lease;bool authorized=true;std::optional<bool> captured;
        unsigned reads=0;
        services.retained_moves_often=[&](ObjectPositionIdentity identity)->std::optional<bool>{
            ++reads;
            if(!f.world.retired_container_active(lease)||!authorized||identity.id!=parent.id||identity.generation!=parent.generation)return {};
            const auto* body=f.world.resolve_reference(parent);if(!body)return {};
            if(body->kind==EntityKind::avatar)return true;
            return captured;
        };
        ObjectPositionRegistry initial(f.world,f.prepared,f.rooms,services);
        require(initial.register_position(child,2,true,f.authority()),"actual child registration before container retirement");
        auto state=initial.snapshot();state.deferred_lighting.clear();
        ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,services,std::move(state));
        require(f.world.remove_retaining_contents(parent,[&](const auto& current){
            lease=current;
            require(f.world.create(parent.id,EntityKind::object,{0,0,1}),"numeric parent replacement does not own old child");
            if(mode!=2)require(!registry.unregister_position(child,true,f.authority()),"unknown retained MovedSelf cannot be assumed false");
            captured=mode==1;
            authorized=false;
            require(!registry.unregister_position(child,true,f.authority()),"expired source authority cannot supply retained property");
            authorized=true;
            require(registry.unregister_position(child,true,f.authority()),"exact active retained parent property supports child Pre unregister");
            const auto lighting=registry.snapshot().deferred_lighting;
            require((mode==0&&!lighting.empty()&&lighting.front()==2)||(mode!=0&&lighting.empty()),
                "actual false/true/avatar MovesOften preserves source lighting decision");
            require(f.world.detach_retained_slot(lease,0,{child.id,child.generation}),"source Pre detaches child after unregister");
            return true;
        }),"source retained-container unregister scope");
        require(reads>=2&&!services.retained_moves_often({parent.id,parent.generation}),"retained property callback rejects opaque lease replay after expiry");
    }
}
void footprint_events(){
    Fixture f;ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());
    const auto id=f.identity(3);
    require(registry.register_position(id,2,true,f.authority()),"initial actual footprint binding");
    const auto current=[&]()->const ObjectPositionRegistryFootprintBinding&{
        const auto& values=registry.snapshot().footprints;const auto at=std::find_if(values.begin(),values.end(),[&](const auto& item){return item.owner.id==3;});
        require(at!=values.end(),"actual footprint binding exists");return *at;};
    const auto first_instance=current().instance;
    require(first_instance&&current().present&&current().room==std::optional<std::uint16_t>{2},"source Footprint.Set binding retained explicitly");
    unsigned shape_events=0;
    require(f.world.set_footprint_observer([&](World& actual,std::int16_t owner,std::uint64_t generation){
        ++shape_events;require(&actual==&f.world&&!actual.notify_footprint_changed(owner),"shape observer owns World and rejects reentry");
        return registry.update_footprint({owner,generation},f.authority());}),"install actual source UpdateFootprint observer");
    require(f.world.set_static_footprint_observer([&](World&,std::int16_t owner,std::uint64_t generation,bool,bool value){return registry.static_footprint_changed({owner,generation},value,f.authority());}),
        "install source SetObstacleStatic observer for binding lifecycle");
    require(f.world.declare_object_word(3,8,4),"raw bootstrap Flags declaration");
    require(current().present&&registry.snapshot().rooms[2].static_obstacles.size()==1,"raw/bootstrap word write does not manufacture UpdateFootprint event");
    require(f.world.notify_footprint_changed(3)&&!current().present&&!current().room&&registry.snapshot().rooms[2].static_obstacles.empty()&&
        registry.snapshot().rooms[2].entities.size()==1&&registry.snapshot().tiles.size()==1,
        "HasZeroExtent source event removes footprint without removing room or tile registration");
    f.word(3,8,0);require(f.world.notify_footprint_changed(3)&&current().present&&current().instance>first_instance&&
        registry.snapshot().rooms[2].static_obstacles.size()==1,"clearing zero extent creates and binds NEW source obstacle instance");
    const auto second_instance=current().instance;
    require(f.world.notify_footprint_changed(3)&&current().instance==second_instance&&registry.snapshot().rooms[2].static_obstacles.size()==1,
        "present-to-present UpdateFootprint reuses source obstacle instead of duplicate registration");
    require(registry.unregister_position(id,false,f.authority())&&current().room==std::optional<std::uint16_t>{2}&&
        registry.snapshot().rooms[2].static_obstacles.empty()&&f.world.set_static_footprint(3,false)&&registry.snapshot().rooms[2].dynamic_obstacles.size()==1,
        "same-room static removal preserves Set binding so later dynamic transition restores exact old footprint");
    require(f.world.set_static_footprint(3,true),"restore actual static source obstacle");
    // Exact combined Flags order: bank write, UpdateFootprint using OLD static
    // property, then direct dynamic-property assignment (not SetObstacleStatic).
    f.word(3,8,static_cast<std::int16_t>(0x8004));
    require(f.world.notify_footprint_changed(3)&&f.world.set_static_footprint(3,false,FootprintClassChangeSource::flags_assignment)&&
        registry.snapshot().rooms[2].static_obstacles.empty()&&registry.snapshot().rooms[2].dynamic_obstacles.empty(),
        "combined zero/dynamic flags remove using previous static collection before property assignment");
    f.word(3,8,static_cast<std::int16_t>(0x8000));require(f.world.notify_footprint_changed(3)&&registry.snapshot().rooms[2].dynamic_obstacles.size()==1,
        "clearing zero extent with dynamic property adds dynamic obstacle");
    f.word(3,8,0);require(f.world.set_static_footprint(3,true,FootprintClassChangeSource::flags_assignment)&&
        registry.snapshot().rooms[2].dynamic_obstacles.size()==1&&f.world.set_static_footprint(3,false)&&
        registry.snapshot().rooms[2].dynamic_obstacles.size()==2&&f.world.set_static_footprint(3,true)&&
        registry.snapshot().rooms[2].dynamic_obstacles.size()==1&&registry.snapshot().rooms[2].static_obstacles.size()==1,
        "source direct dynamic flag and later SetObstacleStatic preserve list quirks rather than repairing state");
    const auto orphan_instance=current().instance;
    f.word(3,8,4);require(f.world.notify_footprint_changed(3)&&registry.snapshot().rooms[2].dynamic_obstacles.size()==1&&
        registry.snapshot().rooms[2].static_obstacles.empty(),"source Unregister removes selected current collection, preserving old orphan reference");
    f.word(3,8,0);require(f.world.notify_footprint_changed(3)&&current().instance!=orphan_instance&&
        registry.snapshot().rooms[2].dynamic_obstacles.front().instance==orphan_instance&&registry.snapshot().rooms[2].static_obstacles.front().instance==current().instance,
        "new obstacle identity cannot overwrite distinct orphan obstacle with same Parent");
    validate_object_position_registry_snapshot(registry.snapshot());
    auto contradictory=registry.snapshot();--contradictory.rooms[2].static_obstacles.front().box.x1;
    malformed([&]{validate_object_position_registry_snapshot(contradictory);},"same source footprint instance cannot carry contradictory geometry");
    auto state=registry.snapshot();require(!record_object_footprint_absent(state,f.world,id,f.authority()),"constructor absence event cannot overwrite an existing live footprint");
    require(f.world.create(6,EntityKind::object),"actual fresh constructor for footprint ownership");const auto fresh=f.identity(6);
    require(record_object_footprint_absent(state,f.world,fresh,f.authority())&&state.footprints.back().owner.id==6&&!state.footprints.back().present,
        "actual fresh constructor records known null footprint independently of registration");
    require(shape_events>=7,"shape lifecycle exercised actual observer events");
}
void footprint_budget(){
    Fixture f;ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());const auto id=f.identity(3);
    require(registry.register_position(id,2,true,f.authority())&&registry.unregister_position(id,false,f.authority()),"source footprint retains binding after same-room removal");
    auto state=registry.snapshot();const auto at=std::find_if(state.footprints.begin(),state.footprints.end(),[&](const auto& value){return value.owner.id==3;});
    require(at!=state.footprints.end()&&at->room.has_value(),"actual bound source footprint for resource budget");
    state.rooms[2].dynamic_obstacles.assign(65535,{id,at->box,at->instance});
    validate_object_position_registry_snapshot(state);ObjectPositionRegistry bounded(f.world,f.prepared,f.rooms,f.services(),std::move(state));
    require(f.world.set_static_footprint(3,false)&&!bounded.static_footprint_changed(id,false,f.authority())&&
        !bounded.snapshot().complete&&bounded.snapshot().rooms[2].dynamic_obstacles.size()==65536,
        "static transition resource exhaustion reports incomplete after retaining its actual source append");
}
void world_event_order(){
    World world;require(world.create(1,EntityKind::object)&&world.declare_object_word(1,8,0),"actual World source Flags owner");
    unsigned shape_events=0,static_events=0,spatial_events=0;
    require(world.set_footprint_observer([&](World& actual,std::int16_t id,std::uint64_t generation){
        require(&actual==&world&&id==1&&actual.find(id)->generation==generation,"Flags observer keeps exact source identity");
        ++shape_events;const auto* entity=actual.find(id);
        if(shape_events==1)require(entity->object_words.at(8)==static_cast<std::int16_t>(0x8004)&&entity->static_footprint,"combined Flags publishes bank before footprint, with old static property");
        if(shape_events==2)require(entity->object_words.at(8)==0&&!entity->static_footprint,"StackObject Flags clear follows same source order");
        return true;}),"install source shape observer");
    require(world.set_static_footprint_observer([&](auto&,auto,auto,bool,bool){++static_events;return true;}),"install distinct source SetObstacleStatic observer");
    require(world.set_spatial_change_observer([&](World& actual,std::uint64_t revision){
        ++spatial_events;require(&actual==&world&&revision==actual.spatial_revision(),"invalidation observes actual source publication");
        require(!actual.set_spatial_change_observer({}),"invalidation cannot replace observer during notification");}),"install runtime spatial invalidation");
    VmFrame frame;frame.context.caller=1;frame.context.stack_object=1;
    require(world.write(frame,3,8,static_cast<std::int16_t>(0x8004))&&shape_events==1&&static_events==0&&!world.find(1)->static_footprint,
        "Caller Flags centralizes source shape event then direct dynamic assignment");
    require(world.write(frame,4,8,0)&&shape_events==2&&static_events==0&&world.find(1)->static_footprint,
        "StackObject Flags shares exact source side effects");
    require(world.write(frame,4,8,0)&&shape_events==2,"identical Flags value has no source shape event");
    require(world.declare_object_word(1,8,4)&&shape_events==2,"raw bootstrap declaration has no source runtime shape event");
    require(spatial_events>=3&&world.set_position(1,{24,24,1,0}),"actual physical publication invalidates derived consumers");
    require(world.set_spatial_change_observer([](auto&,auto){throw std::runtime_error("invalidation diagnostics");})&&world.set_position(1,{40,24,1,0})&&world.find(1)->position->x==40,
        "invalidation observer cannot veto or roll back World publication");
    require(world.set_footprint_observer([](auto&,auto,auto){return false;})&&!world.write(frame,3,8,static_cast<std::int16_t>(0x8000))&&
        world.find(1)->object_words.at(8)==static_cast<std::int16_t>(0x8000)&&world.find(1)->static_footprint,
        "incomplete shape observer retains Flags bank but stops before later dynamic property assignment");
}
void retired_history(){
    Fixture f;ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());auto state=registry.snapshot();const auto id=f.identity(3);
    state.retired_owners={id,{id.id,id.generation+1}};
    validate_object_position_registry_snapshot(state);
    ObjectPositionRegistry historical(f.world,f.prepared,f.rooms,f.services(),state);
    require(historical.query(2,{24,8,56,40},0).status==RoomInfoObstacleIndexStatus::unsupported,
        "retired provenance never supplies query authority");
    auto invalid=state;invalid.complete=true;malformed([&]{validate_object_position_registry_snapshot(invalid);},"retired provenance cannot be marked complete");
    invalid=state;std::swap(invalid.retired_owners[0],invalid.retired_owners[1]);malformed([&]{validate_object_position_registry_snapshot(invalid);},"retired provenance is lexicographically ordered");
    invalid=state;invalid.retired_owners[1]=invalid.retired_owners[0];malformed([&]{validate_object_position_registry_snapshot(invalid);},"retired provenance rejects duplicate identity");
    invalid=state;invalid.retired_owners[0].generation=0;malformed([&]{validate_object_position_registry_snapshot(invalid);},"retired provenance requires positive generation");
    invalid=state;invalid.retired_owners.resize(65537,id);malformed([&]{validate_object_position_registry_snapshot(invalid);},"retired provenance is resource bounded");
}
void retired_constructor(){
    Fixture f;ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());const auto old=f.identity(3);
    require(registry.register_position(old,2,true,f.authority())&&registry.record_moved_self(old,true,f.authority()),"actual prior source owner metadata");
    auto state=registry.snapshot();const auto prior_obstacle=state.rooms[2].static_obstacles.front();
    require(f.world.remove(3)&&f.world.create(3,EntityKind::object),"actual destruction and new constructor reuse same numeric ID");
    const auto current=f.identity(3);require(current.generation>old.generation,"constructor has actual new World generation");
    require(!record_object_moved_self(state,f.world,current,false,f.authority())&&!record_object_footprint_absent(state,f.world,current,f.authority()),
        "untagged stale metadata cannot be replaced merely from matching numeric ID");
    state.retired_owners={{old.id,old.generation+1}};
    require(!record_object_moved_self(state,f.world,current,false,f.authority())&&!record_object_footprint_absent(state,f.world,current,f.authority()),
        "retirement tag must name exact prior generation");
    state.retired_owners={old};state.complete=true;
    require(!record_object_moved_self(state,f.world,current,false,f.authority())&&!record_object_footprint_absent(state,f.world,current,f.authority()),
        "retirement never authorizes metadata replacement in complete query state");
    state.complete=false;f.authenticated=false;
    require(!record_object_moved_self(state,f.world,current,false,f.authority())&&!record_object_footprint_absent(state,f.world,current,f.authority()),
        "actual constructor authority remains mandatory");
    f.authenticated=true;
    require(!record_object_moved_self(state,f.world,old,false,f.authority())&&!record_object_footprint_absent(state,f.world,old,f.authority()),
        "retired generation cannot impersonate actual new World owner");
    require(record_object_moved_self(state,f.world,current,false,f.authority())&&record_object_footprint_absent(state,f.world,current,f.authority())&&
        known_object_moved_self(state,current)==std::optional<bool>{false},"tagged exact old metadata replaced only by authenticated current constructor");
    const auto at=std::find_if(state.footprints.begin(),state.footprints.end(),[&](const auto& value){return value.owner.id==3;});
    require(at!=state.footprints.end()&&at->owner.generation==current.generation&&!at->present&&!at->room&&!at->instance&&
        state.rooms[2].static_obstacles.front().owner.generation==prior_obstacle.owner.generation&&
        state.rooms[2].static_obstacles.front().instance==prior_obstacle.instance&&!state.complete,
        "new constructor metadata preserves historical obstacle identity without query authority or pruning");
    validate_object_position_registry_snapshot(state);
}
}
int main(){try{
    {
        Fixture f;ObjectPositionRegistry registry(f.world,f.prepared,f.rooms,f.services());
        require(registry.defer_lighting(0,f.authority())&&registry.defer_lighting(2,f.authority())&&
            registry.defer_lighting(0,f.authority()),"lighting requests include room0 and deduplicate in insertion order");
        require(!registry.defer_lighting(1,[]{return false;})&&!registry.defer_lighting(4,f.authority()),
            "unauthorized and nonexistent room requests cannot mutate the queue");
        require(registry.snapshot().deferred_lighting==std::vector<std::uint16_t>{0,2},
            "rejected requests preserve the actual pending sequence");
    }
    native_avatar_motion();retired_container_moves_often();registration();lighting_and_queries();containment();footprint_events();footprint_budget();world_event_order();retired_history();retired_constructor();std::cout<<"object position registry source event matrix passed\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
