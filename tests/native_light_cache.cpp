// SPDX-License-Identifier: MPL-2.0
#include "native_light_cache.hpp"
#include "session_room_lighting.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
using Profile=odh_host::NativeLightRemovalProfile;
struct Fixture {
    odh::World world;odh::PreparedArchitecture prepared;odh::GeometricRoomMap rooms;
    odh::ArchitectureObjectState state;odh::DefinitionIndex definitions;
    bool authorized=true,owner_present=true;std::unique_ptr<odh_host::SessionRoomLighting> lighting;
    Fixture(int contribution):definitions([]{odh::ObjectDefinition d{};d.guid=1001;d.version=138;d.tile_width=16;d.footprint_mask=0;
        return std::vector<odh::DefinitionCandidate>{{"fixture","",128,d}};}()){
        prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        for(auto& level:prepared.levels){level.floors.assign(16,1);level.walls.resize(16);}
        rooms.width=rooms.height=4;rooms.floor_resources_resolved=rooms.light_base_rooms_resolved=true;
        rooms.rooms={{},{1,0,16,{0,0,4,4},false,false,{},1},{2,1,16,{0,0,4,4},false,false,{},2}};
        rooms.levels[0].assign(16,0x00010001U);rooms.levels[1].assign(16,0x00020002U);
        for(std::int16_t id=1;id<=2;++id){
            require(world.create_runtime_object(id,1001,{},std::vector<odh::EntryPoint>(27)),"create real emitter with complete entry table");
            require(world.set_position(id,{id==1?24:40,24,1,0}),"position real emitter");
            for(auto field:{7,8,16,29,34,40,51})require(world.declare_object_word(id,field,0),"declare source words");
            set(id,16,1);set(id,51,id==1?contribution:10);
        }
        odh::ObjectPositionRegistry actual(world,prepared,rooms,registry_services());
        for(std::int16_t id=1;id<=2;++id)
            require(actual.register_position(identity(id),1,true,[&]{return authorized;}),"real initial object registration");
        require(actual.finish_initial_load({identity(1),identity(2)},[&]{return authorized;}),"real whole-cohort registry completion");
        state.position_registry=actual.snapshot();
        lighting=std::make_unique<odh_host::SessionRoomLighting>(world,[&]{return owner_present?&state:nullptr;},[&]{return &rooms;},
            []{return static_cast<odh::ObjectPositionRegistry*>(nullptr);},[&]{return services();});
    }
    void set(std::int16_t id,std::int16_t field,std::int16_t value){odh::VmFrame f;f.context.caller=id;require(world.write(f,3,field,value),"source word write");}
    odh::ObjectPositionIdentity identity(std::int16_t id)const{return {id,world.find(id)->generation};}
    odh::WorldEntityReference ref()const{return {1,world.find(1)->generation};}
    odh::RoomLightingServices services(){odh::RoomLightingServices s;s.definitions=&definitions;
        s.group=[&](auto id)->std::optional<std::vector<odh::ObjectPositionIdentity>>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return std::vector<odh::ObjectPositionIdentity>{id};};
        s.object_state=[&](auto id)->std::optional<odh::RoomLightingObjectState>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return odh::RoomLightingObjectState{};};return s;}
    odh_host::BasicLightRemovalOwner owner(){return {world,*lighting,[&]{return owner_present?&state:nullptr;},[&]{return &rooms;},
        [&]{return owner_present&&state.position_registry?&*state.position_registry:nullptr;},[&]{return authorized;}};}
    odh::ObjectPositionRegistryServices registry_services(){
        odh::ObjectPositionRegistryServices s;s.definitions=&definitions;s.group=services().group;
        s.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};s.moved_self=[](auto)->std::optional<bool>{return false;};
        return s;
    }
    void unregister(){
        odh::ObjectPositionRegistry actual(world,prepared,rooms,registry_services(),*state.position_registry);
        require(actual.unregister_position(identity(1),true,[&]{return authorized;}),"real registry physical unregister");
        state.position_registry=actual.snapshot();
        require(world.publish_native_recovery_position(ref(),{},false),"actual World publishes OOW");
    }
};
void cells(){
    odh_host::NativeLightOffsetCell c;require(c.count()==0,"native empty sentinel");
    c.insert(5,0);c.insert(1,0);require(c.offsets==std::array<std::int8_t,4>{{5,0,1,0}},"second insertion deliberately unsorted");
    c.insert(2,0);require(c.offsets==std::array<std::int8_t,4>{{2,0,1,0}},"closer first replacement does not shift prior first");
    c.insert(-2,0);require(c.offsets[0]==2,"equal first distance retained");
    const auto sources=c.sources(10,20);require(sources[0]&&*sources[0]==std::array<int,2>{{10,18}}&&
        sources[1]&&*sources[1]==std::array<int,2>{{10,19}},"native Y,X offsets subtract to actual XY source");
    c.remove(2,0);require(c.count()==1&&c.offsets[0]==1,"first removal promotes actual second");
    c.remove(257,0);require(c.count()==1,"removal compares int arguments without byte truncation");
    c.remove(1,0);require(c.count()==0,"last removal clears all bytes");
    c.insert(-3,4);c.insert(-3,4);c.remove(-3,4);require(c.count()==1,"coordinate duplicates remove once as original");
    c.insert(1,1);c.remove(1,1);require(c.count()==1&&c.offsets[0]==-3,"second removal preserves first");
}
void lifecycle(int contribution){
    Fixture f(contribution);auto owner=f.owner();std::string why;
    require(!owner.supports(f.ref(),Profile::basic_room_color,why)&&!f.state.lighting,"read-only admission cannot fabricate a cache");
    f.lighting->finish_loading();f.lighting->process();
    const auto before=f.state.lighting->by_base_room.at(1).ambient_light;
    require(before==contribution+10,"actual lighting reflects both physical emitters");
    const auto view_before=odh_host::make_room_lighting_view(*f.state.lighting_committed,f.rooms,0,0,0);
    require(!owner.supports(f.ref(),Profile::unknown,why)&&!owner.supports(f.ref(),Profile::native_directional,why),"unproven profiles refused");
    require(!owner.supports({1,f.ref().generation+1},Profile::basic_room_color,why),"stale generation refused");
    f.authorized=false;require(!owner.supports(f.ref(),Profile::basic_room_color,why),"expired original authority refused");f.authorized=true;
    f.owner_present=false;require(!owner.supports(f.ref(),Profile::basic_room_color,why),"missing actual owner refused");f.owner_present=true;
    f.state.position_registry->complete=false;require(!owner.supports(f.ref(),Profile::basic_room_color,why),"incomplete real registry refused");f.state.position_registry->complete=true;
    require(owner.supports(f.ref(),Profile::basic_room_color,why),"actual basic renderer owner admitted");
    const auto old=*f.world.find(1)->position;
    const auto receipt=owner.remove_before_oow(f.ref(),old,Profile::basic_room_color,why);
    require(receipt&&receipt->room==1&&!receipt->already_queued&&f.world.find(1)->position==std::optional<odh::EntityPosition>{old},
        "publication queues real old room while emitter is still physically present");
    require(f.state.position_registry->deferred_lighting==std::vector<std::uint16_t>{1}&&
        f.state.lighting->by_base_room.at(1).ambient_light==before,"queued removal has not recalculated early");
    require(owner.remove_before_oow(f.ref(),old,Profile::basic_room_color,why)->already_queued,"actual deferred queue deduplicates");
    f.unregister();require(!f.world.find(1)->position,"actual removal happens before lighting drain");
    f.lighting->process();
    const auto& light=f.state.lighting->by_base_room.at(1);
    require(f.state.position_registry->deferred_lighting.empty()&&light.ambient_light==10&&light.lights.size()==1&&
        light.lights[0].component&&light.lights[0].component->id==2,"real consumer drains and retains only remaining emitter");
    require(std::none_of(light.components.begin(),light.components.end(),[](auto id){return id.id==1;}),"off emitter also leaves actual component cache");
    const auto view_after=odh_host::make_room_lighting_view(*f.state.lighting_committed,f.rooms,0,0,0);
    require((view_before.room_colors!=view_after.room_colors)==(contribution>0),"actual SavedLotRenderer color input follows emission independently of Light16");
}
}
int main(){try{cells();lifecycle(25);lifecycle(0);std::cout<<"native light kernels and basic owner lifecycle passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
