#include "../tools/runtime_attempt_walk_probe.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void put(std::string& bytes,std::size_t at,std::uint32_t value,unsigned count=4,bool big=false){
    for(unsigned n=0;n<count;++n)bytes.at(at+n)=static_cast<char>((value>>(8*(big?count-1-n:n)))&255);
}
std::string chunk(const std::string& type,std::uint16_t id,const std::string& raw){
    std::string value(76,'\0');value.replace(0,4,type);put(value,4,raw.size()+76,4,true);put(value,8,id,2,true);return value+raw;
}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("odh-room-graph-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){require(std::filesystem::create_directory(path),"create isolated resource fixture");}
    ~Temp(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Fixture {
    Temp temporary;
    std::string resource;
    odh::ObjectDefinition door{},blocker{};
    std::unique_ptr<odh::DefinitionIndex> definitions;
    odh::SavedWorld saved;
    odh::World live;
    SavedArchitectureResource architecture;
    odh::PreparedArchitecture prepared;
    Fixture(){
        std::string objd(216,'\0');put(objd,0,138);put(objd,28,123);
        put(objd,34,4105,2);put(objd,92,3,2);put(objd,162,164,2);
        std::string iff="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";iff.resize(64,'\0');
        iff+=chunk("OBJD",1,objd);resource=(temporary.path/"door.iff").string();
        {std::ofstream out(resource,std::ios::binary);out.write(iff.data(),iff.size());require(bool(out),"write exact door resource");}
        door.guid=123;door.tile_width=16;door.footprint_mask=0;
        blocker=door;blocker.guid=456;
        definitions=std::make_unique<odh::DefinitionIndex>(std::vector<odh::DefinitionCandidate>{
            {resource,"",1,door},{resource,"",2,blocker}});
        saved.world=std::make_unique<odh::World>();
        for(const auto id:{std::int16_t{10},std::int16_t{11},std::int16_t{12}}){
            const odh::EntityPosition position{static_cast<std::int16_t>(id==10?24:id==11?8:40),24,1,0};
            for(auto* world:{saved.world.get(),&live}){
                require(world->create(id,odh::EntityKind::object,{17,0,1})&&world->define_type(id,123)&&
                    world->declare_object_word(id,40,id==10?static_cast<std::int16_t>(0x8000U):0)&&
                    world->declare_object_word(id,13,1)&&world->declare_object_word(id,8,16)&&
                    world->set_position(id,position),"materialize exact saved/live portal member");
                std::vector<odh::EntryPoint> entries(16,{0,0});entries[15].action=4105;
                require(world->define_entry_points(id,entries),"declare exact portal entry");
            }
            odh::SavedWorldEntity record{};record.saved.object_id=id;record.saved.guid=123;
            record.definition=door;record.imported_generation=live.find(id)->generation;
            saved.records.push_back(record);
        }
        saved.multitile_groups.push_back({10,{}, {10,11,12}});
        architecture.architecture.width=prepared.width=4;architecture.architecture.height=prepared.height=4;
        prepared.buildable.assign(16,1);
        for(unsigned level=0;level<2;++level){
            auto& source=architecture.architecture.levels[level];source.floors.assign(16,0);source.walls.resize(16);
            if(level==0)for(unsigned y=0;y<4;++y)for(unsigned x:{0U,2U}){
                auto& wall=source.walls[y*4+x];wall.segments=odh::wall_top_left;wall.top_left_style=1;
            }
            prepared.levels[level].floors=source.floors;prepared.levels[level].walls=source.walls;
        }
        // These non-architectural live participants need no saved wall lease.
        require(live.create(1,odh::EntityKind::avatar,{17,88,1})&&live.define_type(1,456)&&
                live.set_position(1,{24,8,1,0})&&live.declare_object_word(1,8,0),"create actual walking avatar");
        require(live.create(2,odh::EntityKind::object,{17,0,1})&&live.define_type(2,456)&&
                live.set_position(2,{8,8,1,0})&&live.declare_object_word(2,8,0)&&
                live.declare_object_word(2,40,0)&&live.declare_object_word(2,13,0),"create actual unknown-EP5 blocker");
        for(const auto id:{1,2}){std::vector<odh::EntryPoint> entries(16,{0,0});entries[5].action=4108;
            require(live.define_entry_points(id,entries),"declare unknown collision entry point");}
    }
    std::uint16_t source_room()const{
        auto rooms=odh::generate_prepared_room_map(prepared);
        auto room=odh::geometric_room_at(rooms,24,8,1);require(room.has_value()&&*room,"actual source room exists");return *room;
    }
    RuntimeRoomPortalGraph graph()const{return build_runtime_room_portal_graph(saved,live,*definitions,
        architecture,prepared,source_room(),0);}
};
}
int main(){try{
    Fixture fixture;unsigned ep5_calls=0;
    auto graph=fixture.graph();
    require(graph.complete&&graph.inventory.complete&&!graph.inventory.edges.empty(),
        "actual producer authenticates a nonempty portal graph without a collision callback");
    const auto target_room=odh::geometric_room_at(graph.rooms,40,8,1);
    require(target_room&&*target_room!=fixture.source_room(),"door links distinct actual rooms");
    const auto route=odh::find_room_portal_route(graph.inventory,fixture.source_room(),*target_room,
        *fixture.live.find(1)->position,{40,8,1,0});
    require(route.status==odh::RoomPortalRouteStatus::found&&!route.portals.empty()&&!ep5_calls,
        "DoRoomRoute plans an actual portal before any entity-footprint/EP5 evaluation");
    const auto walk=build_runtime_attempt_walk_probe(fixture.saved,fixture.live,*fixture.definitions,
        fixture.architecture,fixture.prepared,fixture.source_room(),0,1,true,{},
        [&](std::int16_t,std::int16_t)->std::optional<bool>{++ep5_calls;return {};});
    require(walk.portal_inventory_complete&&walk.architecture_complete&&
        walk.snapshot.state==odh::AttemptWalkObstacleState::unsupported&&
        walk.snapshot.reason==odh::AttemptWalkObstacleReason::entry_point5_result_unknown&&ep5_calls>0,
        "later actual AttemptWalk still refuses unknown EP5 rather than inventing empty obstacles");
    require(fixture.live.set_position(11,{8,8,1,0}),"move actual portal after saved admission");
    const auto moved=fixture.graph();
    require(!moved.complete&&moved.diagnostic.failure==RuntimeGeometryFailure::portal_member_identity,
        "stale portal pose fails authenticated graph admission");
    require(fixture.live.set_position(11,{8,24,1,0}),"restore exact portal pose");
    require(fixture.live.create(20,odh::EntityKind::object,{17,0,1})&&fixture.live.define_type(20,123)&&
        fixture.live.set_position(20,{56,8,1,0})&&
        fixture.live.declare_object_word(20,40,static_cast<std::int16_t>(0x8000U)),"create unadmitted architectural object");
    const auto unadmitted=fixture.graph();
    require(!unadmitted.complete&&unadmitted.diagnostic.failure==RuntimeGeometryFailure::new_architectural_entity,
        "unknown live architectural member is not silently omitted or interpreted as no path");
    std::cout<<"Actual portal graph and later collision phase fixture passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
