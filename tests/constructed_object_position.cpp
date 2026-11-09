// SPDX-License-Identifier: MPL-2.0
#include "odh/object_position_change.hpp"
#include "odh/architecture_object_state.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "session_room_lighting.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
using namespace odh;
unsigned checks=0;
void require(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
ObjectDefinition definition(){ObjectDefinition d{};d.version=138;d.guid=1001;d.tile_width=16;d.footprint_mask=0;d.level_offset=0;return d;}
struct Fixture {
    enum class Initialization {construct,cold_restore};
    World world;PreparedArchitecture prepared;GeometricRoomMap rooms;
    DefinitionIndex definitions{{{"fixture","",128,definition()}}};
    ArchitectureObjectState state;ObjectPositionIdentity root{};
    ObjectPositionRegistrySnapshot* registry_owner=nullptr;
    const RoomLightingSnapshot* lighting_owner=nullptr;
    bool source_live=true,constructed_proof=true;std::function<void()> check_effect;
    unsigned constructed_calls=0,ordinary_calls=0;
    std::vector<std::string> events;
    explicit Fixture(Initialization initialization):Fixture(25,0,false,initialization){}
    Fixture(std::int16_t light=25,std::size_t slots=0,bool portal=false,
            Initialization initialization=Initialization::construct){
        prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        for(auto& level:prepared.levels){level.floors.assign(16,1);level.walls.resize(16);}
        rooms.width=rooms.height=4;rooms.floor_resources_resolved=rooms.light_base_rooms_resolved=true;
        rooms.rooms={{0,0,0,{},false,false,{},0},{1,0,16,{0,0,4,4},false,false,{},1},
            {2,1,16,{0,0,4,4},false,false,{},2}};
        rooms.levels[0].assign(16,0x00010001U);rooms.levels[1].assign(16,0x00020002U);
        state.object_support=make_architecture_object_support(4,4);
        for(auto& level:state.occupied_walls)level.assign(16,0);
        // Cold consumers prepare immutable geometry/providers only. The owned
        // World and event state come exclusively from the decoded checkpoint.
        if(initialization==Initialization::cold_restore)return;
        ObjectPositionRegistry empty(world,prepared,rooms,registry_services());
        require(empty.finish_initial_load({},[]{return true;}),"empty complete source lot before real construction");
        state.position_registry=empty.snapshot();state.lighting.emplace();state.lighting_committed.emplace();
        root=add(1,light,slots,portal);registry_owner=&*state.position_registry;lighting_owner=&*state.lighting;
    }
    void word(std::int16_t id,std::int16_t field,std::int16_t value){VmFrame f;f.context.caller=id;
        require(world.write(f,3,field,value),"actual constructor field write");}
    ObjectPositionIdentity add(std::int16_t id,std::int16_t light,std::size_t slots=0,bool portal=false){
        std::vector<EntryPoint> entries(31);entries[9].action=500;if(portal)entries[15].action=600;
        require(world.create_runtime_object(id,1001,{0,0,slots},entries),"real native constructor inventory");
        const ObjectPositionIdentity ref{id,world.find(id)->generation};
        for(const auto field:std::vector<std::pair<std::int16_t,std::int16_t>>{
                {4,1},{13,128},{23,2},{27,5},{28,6},{42,3},{40,0x400},{16,1},{51,light},{7,20}})
            word(id,field.first,field.second);
        require(record_object_footprint_absent(*state.position_registry,world,ref,[]{return true;})&&
            record_object_moved_self(*state.position_registry,world,ref,false,[]{return true;}),
            "constructor appends exact footprint and MovedSelf provenance to existing owner");
        return ref;
    }
    ObjectPositionRegistryServices registry_services(){ObjectPositionRegistryServices services;services.definitions=&definitions;
        services.group=[&](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return std::vector<ObjectPositionIdentity>{id};};
        services.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};
        services.moved_self=[&](auto id){return state.position_registry?known_object_moved_self(*state.position_registry,id):std::nullopt;};return services;
    }
    RoomLightingServices light_services(){RoomLightingServices services;services.definitions=&definitions;
        services.group=[&](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return std::vector<ObjectPositionIdentity>{id};};
        services.object_state=[&](auto id)->std::optional<RoomLightingObjectState>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return RoomLightingObjectState{};};return services;
    }
    bool registry_current()const{return state.position_registry&&registry_owner==&*state.position_registry&&registry_owner->complete;}
    bool light_current()const{return registry_current()&&state.lighting&&lighting_owner==&*state.lighting;}
    std::optional<bool> profile(ObjectPositionIdentity id,bool allow_light){
        const auto* e=world.find(id.id);if(!source_live||!e||e->generation!=id.generation||!e->definition||!e->entry_points)return {};
        const auto* def=definitions.unique(e->definition->guid);if(!def)return {};
        return e->kind==EntityKind::object&&!e->ui_ghost&&!def->definition.master_id&&!def->definition.is_multitile&&
            !e->definition->master_guid&&e->entry_points->size()>15&&!e->entry_points->at(15).action&&
            !(static_cast<std::uint16_t>(e->object_words.at(40))&0xc000U)&&
            (e->object_words.at(51)==0||allow_light);
    }
    ObjectPositionChangeResult run(bool existing=false,EntityPosition position={24,24,1,0},bool supply_constructed=true){
        ObjectPositionValidServices valid;
        valid.architecture=[&](auto,const auto& p)->std::optional<ObjectPositionArchitecture>{
            events.push_back("check");ObjectPositionArchitecture out;
            out.level_offset=0;out.precise_floor=1;out.out_of_bounds=p.x<0||p.y<0||p.x>=64||p.y>=64||p.level<1||p.level>2;return out;};
        valid.intersections=[&](auto id,const auto& p,auto)->std::optional<ObjectPositionIntersections>{
            const auto shape=object_collision_shape(definition(),p,0,false);if(shape.state==CollisionShapeState::unsupported||!registry_current())return {};
            ObjectPositionIntersections out;out.footprint_present=shape.state==CollisionShapeState::present;
            ObjectPositionRegistry registry(world,prepared,rooms,registry_services(),*state.position_registry);
            out.query=registry.query(1,shape.box,id.id);if(check_effect)check_effect();return out;};
        valid.same_group=[](auto,auto)->std::optional<bool>{return false;};valid.ignore_intersection=valid.same_group;
        valid.ghost_image=[](auto)->std::optional<bool>{return false;};
        ObjectPositionChangeServices change;change.story_count=[]()->std::optional<std::int16_t>{return 5;};
        change.room_at=[&](const auto& p)->std::optional<std::uint16_t>{
            if(p.x<0||p.y<0||p.x>=64||p.y>=64||p.level<1||p.level>2)return 0;
            return geometric_room_at_runtime(rooms,p.x,p.y,p.level);};
        change.admit_single_ordinary=[&](auto id){++ordinary_calls;return profile(id,false);};
        if(supply_constructed)change.admit_single_constructed=[&](auto id)->std::optional<bool>{
            ++constructed_calls;if(!constructed_proof)return false;if(!light_current())return {};
            if(id.id!=root.id||id.generation!=root.generation)return false;return profile(id,true);};
        change.unregister_position=[&](auto id,bool room_change){events.push_back("pre");if(!registry_current())return false;
            ObjectPositionRegistry registry(world,prepared,rooms,registry_services(),*state.position_registry);
            const bool accepted=registry.unregister_position(id,room_change,[&]{return source_live;});
            *state.position_registry=registry.snapshot();return accepted;};
        change.register_position=[&](auto id,std::uint16_t room,bool room_change){events.push_back("post");if(!light_current())return false;
            ObjectPositionRegistry registry(world,prepared,rooms,registry_services(),*state.position_registry);
            const bool accepted=registry.register_position(id,room,room_change,[&]{return source_live;});
            *state.position_registry=registry.snapshot();return accepted;};
        change.wall_use=[&](auto,bool set,bool exclusive){require(set&&!exclusive,"fresh ordinary wall lifecycle unchanged");events.push_back("wall");return true;};
        change.entry_point9=[&](auto){events.push_back("ep9");return true;}; // Completed callback; authored false is tested in real-VM companion matrix.
        ObjectPositionValid validator(world,valid);ObjectPositionChange lifecycle(world,validator,change);
        const ObjectPositionValidRequest request{root,position,position.direction,0,[&]{return source_live;}};
        return existing?lifecycle.apply_existing(request):lifecycle.apply(request);
    }
};
void linked_placement_save_reload(std::int16_t contribution){
    Fixture f(contribution);const auto result=f.run();
    require(result.status==ObjectPositionChangeStatus::success&&result.position_published&&f.constructed_calls==2&&f.ordinary_calls==0,
        "same fresh-root policy authorizes capture and after-check without promoting ordinary movement");
    require(f.events==std::vector<std::string>{"pre","check","post","wall","ep9"},"source Pre/check/publication/Post/callback order");
    const auto& registry=*f.state.position_registry;const auto* object=f.world.find(1);
    require(object->position&&*object->position==EntityPosition{24,24,1,0}&&object->object_words.at(29)==0&&object->object_words.at(51)==contribution,
        "actual scalar pose and Room29 preserve authored contribution");
    require(registry.complete&&registry.rooms[1].entities.size()==1&&registry.rooms[1].entities[0].generation==f.root.generation&&
        registry.tiles.size()==1&&registry.tiles[0].offset==5&&registry.tiles[0].entities[0].id==1&&
        registry.footprints.size()==1&&registry.footprints[0].present&&registry.footprints[0].room==std::optional<std::uint16_t>{1},
        "real event registry owns exact room/tile/footprint membership");
    require(registry.deferred_lighting==std::vector<std::uint16_t>{0,1},"Pre then Post retain source dirty-room insertion order");
    const auto world_bytes=encode_world_snapshot(f.world.snapshot());const auto state_bytes=encode_architecture_object_state(f.state);
    Fixture restored(Fixture::Initialization::cold_restore);
    const auto pristine=restored.world.snapshot();
    require(pristine.entities.empty()&&pristine.retained_entities.empty()&&pristine.word_count==0&&
        pristine.generation_sequence==0&&pristine.spatial_revision==0,
        "cold consumer starts with pristine World, without constructing then deleting an object");
    require(restored.world.restore_snapshot(decode_world_snapshot(world_bytes)),"cold owned World restores without Init replay");
    restored.state=decode_architecture_object_state(state_bytes);restored.root=f.root;
    restored.registry_owner=&*restored.state.position_registry;restored.lighting_owner=&*restored.state.lighting;
    require(encode_architecture_object_state(restored.state)==state_bytes&&encode_world_snapshot(restored.world.snapshot())==world_bytes,
        "RPOS, queued lighting and original body generation survive byte-stable native codecs");
    odh_host::SessionRoomLighting lighting(restored.world,[&]{return &restored.state;},[&]{return &restored.rooms;},
        []{return static_cast<ObjectPositionRegistry*>(nullptr);},[&]{return restored.light_services();});
    lighting.prepare_capture();
    require(lighting.visual_room(1,f.root.generation)==std::optional<std::uint16_t>{65535}&&
        restored.state.position_registry->deferred_lighting==std::vector<std::uint16_t>{0,1},
        "cold first-frame preparation classifies new emitter without draining pending simulation work");
    lighting.process();
    require(restored.state.position_registry->deferred_lighting.empty()&&restored.state.lighting_committed&&
        restored.state.lighting_committed->by_base_room.count(1)&&
        restored.state.lighting_committed->by_base_room.at(1).ambient_light==static_cast<std::uint16_t>(contribution)&&
        restored.state.lighting_committed->by_base_room.at(1).lights.size()==1&&
        restored.state.lighting_committed->by_base_room.at(1).lights[0].component->generation==f.root.generation,
        "actual lighting safe point consumes real requests and commits generation-bound light source");
    require(restored.state.position_registry->rooms[1].room_score==
        std::optional<std::int16_t>{restored.state.lighting->by_base_room.at(1).room_score},
        "lighting and room-score consumers share the actual registry score owner");
    const auto committed=encode_architecture_object_state(restored.state);
    require(encode_architecture_object_state(decode_architecture_object_state(committed))==committed,
        "committed simulation lighting, visual cache and RPOS survive subsequent archive roundtrip");
}
void strict_admission_matrix(){
    for(unsigned mode=0;mode<10;++mode){Fixture f(25,0,mode==4);
        ObjectPositionRegistrySnapshot other_registry;RoomLightingSnapshot other_lighting;
        if(mode==0)f.constructed_proof=false;
        if(mode==1)f.state.position_registry->complete=false;
        if(mode==2)f.lighting_owner=nullptr;
        if(mode==3)f.word(1,40,0x4400);
        if(mode==5)++f.root.generation;
        if(mode==6)require(f.world.set_ui_ghost(1,true),"actual GhostImage declaration");
        if(mode==8)f.registry_owner=&other_registry;
        if(mode==9)f.lighting_owner=&other_lighting;
        const auto result=f.run(false,{24,24,1,0},mode!=7);
        require(result.status==ObjectPositionChangeStatus::unsupported&&!result.effects_started&&!result.position_published&&f.events.empty()&&!f.world.find(1)->position,
            "false proof, missing/incomplete owner, architecture/portal, stale generation, ghost and absent fresh service refuse before effects");
    }
    {Fixture f;require(f.world.set_position(1,{24,24,1,0}),"existing light already positioned");
        const auto result=f.run(true,{40,24,1,0});require(result.status==ObjectPositionChangeStatus::unsupported&&f.constructed_calls==0&&f.events.empty(),
            "constructed-root proof cannot authorize an existing light movement");}
    {Fixture f(25,1);const auto child=f.add(2,25);require(f.world.attach(1,0,2),"actual contained light child");
        const auto result=f.run();require(result.status==ObjectPositionChangeStatus::unsupported&&!result.effects_started&&f.constructed_calls==1&&f.ordinary_calls==1&&
            f.world.find(child.id)->attachment&&f.events.empty(),"fresh-root policy never grants new profile to contained children");}
    for(unsigned mode=0;mode<3;++mode){Fixture f;f.check_effect=[&]{
            if(mode==0)f.state.position_registry->complete=false;
            if(mode==1)f.lighting_owner=nullptr;
            if(mode==2)f.word(1,40,0x4400);
        };
        const auto result=f.run();require(result.status==ObjectPositionChangeStatus::unsupported&&result.effects_started&&!result.position_published&&!f.world.find(1)->position&&
            f.constructed_calls==2&&f.events==std::vector<std::string>{"pre","check"}&&
            f.state.position_registry->deferred_lighting==std::vector<std::uint16_t>{0},
            "after-check reproof refuses changed owner/profile while retaining completed source Pre effects");}
    {Fixture f;const auto result=f.run(false,{-16,24,1,0});require(result.status==ObjectPositionChangeStatus::blocked&&!result.position_published&&!f.world.find(1)->position&&
        f.world.find(1)->object_words.at(51)==25&&f.state.position_registry->complete,
        "fresh light admission never bypasses actual negative-coordinate placement rejection");}
    {Fixture f(0);const auto result=f.run(false,{24,24,1,0},false);require(result.status==ObjectPositionChangeStatus::success&&f.constructed_calls==0&&f.ordinary_calls==2,
        "legacy non-light singleton retains ordinary constructor behavior without fresh proof");}
}
}
int main(){try{for(const std::int16_t light:{25,73})linked_placement_save_reload(light);strict_admission_matrix();std::cout<<checks<<" checks passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
