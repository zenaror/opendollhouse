#include "../tools/household_runtime_arrival.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using Status = odh::HouseholdPositionAttemptStatus;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    odh::World world;
    odh::DefinitionIndex definitions;
    odh::PreparedArchitecture architecture;
    Fixture():definitions({{"synthetic","person",128,
        {138,0xaaaa,2,0,-1,false,162,0,0,0,16,0}},
        {"synthetic","mailbox",128,{138,0xbbbb,4,0,-1,false,162,0,0,0,16,0}}}) {
        architecture.width=architecture.height=16;
        architecture.buildable.assign(256,1);
        for(auto& level:architecture.levels){level.floors.assign(256,0);level.walls.resize(256);}
        require(world.create(1,odh::EntityKind::object)&&world.define_type(1,0xbbbb)&&
            world.declare_object_word(1,8,0)&&world.define_entry_points(1,std::vector<odh::EntryPoint>(33,{0,0}))&&
            world.set_position(1,{168,168,1,0}),"mailbox construction");
        require(world.create(2,odh::EntityKind::avatar,{0,101,3,16})&&world.define_type(2,0xaaaa)&&
            world.declare_object_word(2,8,256)&&world.declare_object_word(2,42,2)&&
            world.declare_object_word(2,7,0)&&world.declare_object_word(2,40,0)&&world.declare_object_word(2,51,0)&&
            world.declare_object_word(2,29,-1)&&world.define_entry_points(2,std::vector<odh::EntryPoint>(33,{0,0})),
            "avatar construction");
    }
    void reject(odh::EntityPosition position,Status expected) {
        const auto before=odh::encode_world_snapshot(world.snapshot());
        require(attempt_household_runtime_arrival(world,2,position,definitions,architecture)==expected,"unexpected arrival result");
        require(odh::encode_world_snapshot(world.snapshot())==before,"rejected attempt mutated World, counters or metadata");
    }
    void mailbox(std::int16_t id, odh::EntityPosition position,
                 std::optional<std::uint32_t> master_guid={}) {
        require(world.create(id,odh::EntityKind::object)&&world.define_type(id,0xbbbb,master_guid)&&
            world.declare_object_word(id,8,0)&&
            world.define_entry_points(id,std::vector<odh::EntryPoint>(33,{0,0}))&&
            world.set_position(id,position),"mailbox caller construction");
    }
};
}
int main() {
    try {
        Fixture f;
        f.reject({168,168,1,0},Status::blocked);
        f.reject({-8,168,1,0},Status::blocked);
        f.reject({256,168,1,0},Status::blocked);
        f.reject({152,152,0,0},Status::blocked);
        f.reject({152,152,2,0},Status::blocked); // unsupported air placement is a known blocker
        f.architecture.levels[0].floors[9*16+9]=65535;
        f.reject({152,152,1,0},Status::blocked);
        f.architecture.levels[0].floors[9*16+9]=65534;
        f.reject({152,152,1,0},Status::blocked);
        f.architecture.levels[0].floors[9*16+9]=0;
        f.architecture.diagnostics.unresolved_floor_alias_references=1;
        f.reject({152,152,1,0},Status::unsupported);
        f.architecture.diagnostics.unresolved_floor_alias_references=0;
        require(attempt_household_runtime_arrival(f.world,2,{152,152,1,0},f.definitions,f.architecture)==Status::placed,
            "known clear candidate was not placed");
        require(f.world.find(2)->position==std::optional<odh::EntityPosition>{{152,152,1,0}},"placed avatar lost position");
        const odh::VmFrame frame{odh::routine_key(4096),0,{2,2,2,2},{},{}};
        const auto rooms=odh::generate_prepared_room_map(f.architecture);
        const auto room=odh::geometric_room_at_runtime(rooms,152,152,1);
        require(room&&f.world.read(frame,3,29)==std::optional<std::int16_t>{static_cast<std::int16_t>(*room-1)},
            "PositionChange room data missing");
        f.reject({136,136,1,0},Status::unsupported); // profile is construction-only
        for(const auto which:{0,1}) {
            Fixture invalid;
            auto snapshot=invalid.world.snapshot();
            (*snapshot.entities[which].entry_points)[which?9:5].action=4097;
            odh::World replacement;require(replacement.restore_snapshot(snapshot),"negative fixture snapshot");
            const auto before=odh::encode_world_snapshot(replacement.snapshot());
            require(attempt_household_runtime_arrival(replacement,2,{168,168,1,0},invalid.definitions,invalid.architecture)==Status::unsupported,
                "EP5 or EP9 BHAV was silently skipped");
            require(odh::encode_world_snapshot(replacement.snapshot())==before,"unsupported BHAV mutated state");
        }
        Fixture occupied;
        require(occupied.world.create(3,odh::EntityKind::object,{0,0,1,0})&&occupied.world.attach(3,0,2),"attachment fixture");
        occupied.reject({152,152,1,0},Status::unsupported);
        Fixture normal_caller;
        normal_caller.mailbox(3,{152,152,1,0});
        normal_caller.reject({152,152,1,0},Status::blocked);
        Fixture ignored_caller;
        ignored_caller.mailbox(3,{152,152,1,0});
        require(attempt_household_runtime_arrival(ignored_caller.world,2,{152,152,1,0},
            ignored_caller.definitions,ignored_caller.architecture,3)==Status::placed,
            "explicitly ignored singleton Caller still blocked arrival");
        require(ignored_caller.world.find(3)&&
            ignored_caller.world.find(3)->position==std::optional<odh::EntityPosition>{{152,152,1,0}}&&
            ignored_caller.world.find(3)->definition&&ignored_caller.world.find(3)->definition->guid==0xbbbb&&
            !ignored_caller.world.find(3)->definition->master_guid,
            "ignored Caller was removed or changed during arrival");
        const auto ignored_rooms=odh::generate_prepared_room_map(ignored_caller.architecture);
        const odh::RuntimeRoomResolver ignored_room_for=[&ignored_rooms](odh::EntityPosition p){
            return odh::geometric_room_at_runtime(ignored_rooms,p.x,p.y,p.level);
        };
        const auto ignored_room=ignored_room_for({152,152,1,0});
        const auto ignored_obstacles=ignored_room?odh::snapshot_world_room_info(ignored_caller.world,
            ignored_caller.definitions,ignored_room_for,[](std::int16_t,std::uint64_t)->std::optional<std::int32_t>{
                return -1;
            },*ignored_room):odh::RoomInfoObstacleSnapshot{};
        require(ignored_room&&ignored_obstacles.status==odh::RoomInfoObstacleIndexStatus::complete&&
            std::any_of(ignored_obstacles.obstacles.begin(),ignored_obstacles.obstacles.end(),[](const auto& obstacle){
                return obstacle.owner_id==3&&obstacle.collection==odh::RoomInfoObstacleCollection::static_set;
            }),"ignored Caller lost its World RoomInfo registration");
        Fixture other_blocker;
        other_blocker.mailbox(3,{152,152,1,0});
        other_blocker.mailbox(4,{152,152,1,0});
        const auto other_blocker_before=odh::encode_world_snapshot(other_blocker.world.snapshot());
        require(attempt_household_runtime_arrival(other_blocker.world,2,{152,152,1,0},
            other_blocker.definitions,other_blocker.architecture,3)==Status::blocked,
            "ignoring Caller also ignored an independent blocker");
        require(odh::encode_world_snapshot(other_blocker.world.snapshot())==other_blocker_before,
            "other-blocker rejection mutated World, counters or metadata");
        Fixture unknown_caller;
        const auto unknown_before=odh::encode_world_snapshot(unknown_caller.world.snapshot());
        require(attempt_household_runtime_arrival(unknown_caller.world,2,{152,152,1,0},
            unknown_caller.definitions,unknown_caller.architecture,99)==Status::unsupported&&
            odh::encode_world_snapshot(unknown_caller.world.snapshot())==unknown_before,
            "unknown ignored Caller was accepted or mutated World");
        Fixture multitile_caller;
        multitile_caller.mailbox(3,{152,152,1,0});
        odh::DefinitionIndex multitile_definitions({
            {"synthetic","person",128,{138,0xaaaa,2,0,-1,false,162,0,0,0,16,0}},
            {"synthetic","mailbox",128,{138,0xbbbb,4,1,-1,true,162,0,0,0,16,0}}});
        const auto multitile_before=odh::encode_world_snapshot(multitile_caller.world.snapshot());
        require(attempt_household_runtime_arrival(multitile_caller.world,2,{152,152,1,0},
            multitile_definitions,multitile_caller.architecture,3)==Status::unsupported&&
            odh::encode_world_snapshot(multitile_caller.world.snapshot())==multitile_before,
            "multitile ignored Caller was accepted or mutated World");
        Fixture master_caller;
        master_caller.mailbox(3,{152,152,1,0},0xbbbb);
        const auto master_before=odh::encode_world_snapshot(master_caller.world.snapshot());
        require(attempt_household_runtime_arrival(master_caller.world,2,{152,152,1,0},
            master_caller.definitions,master_caller.architecture,3)==Status::unsupported&&
            odh::encode_world_snapshot(master_caller.world.snapshot())==master_before,
            "ignored Caller with a master GUID was accepted or mutated World");
        Fixture ghost;
        require(ghost.world.set_ui_ghost(2,true),"ghost fixture");
        ghost.reject({152,152,1,0},Status::unsupported);
        Fixture dynamic;
        require(dynamic.world.set_static_footprint(2,false),"dynamic fixture");
        require(attempt_household_runtime_arrival(dynamic.world,2,{152,152,1,0},dynamic.definitions,dynamic.architecture)==Status::placed,
            "dynamic-footprint avatar could not arrive");
        const auto dynamic_rooms=odh::generate_prepared_room_map(dynamic.architecture);
        const odh::RuntimeRoomResolver dynamic_room_for=[&dynamic_rooms](odh::EntityPosition p){
            return odh::geometric_room_at_runtime(dynamic_rooms,p.x,p.y,p.level);
        };
        const auto dynamic_room=dynamic_room_for({152,152,1,0});
        const auto dynamic_obstacles=dynamic_room?odh::snapshot_world_room_info(dynamic.world,dynamic.definitions,
            dynamic_room_for,[&dynamic](std::int16_t actor,std::uint64_t generation)->std::optional<std::int32_t>{
                const auto* entity=dynamic.world.find(actor);
                return entity&&entity->generation==generation?entity->kill_timeout:std::nullopt;
            },*dynamic_room):odh::RoomInfoObstacleSnapshot{};
        require(dynamic_room&&dynamic_obstacles.status==odh::RoomInfoObstacleIndexStatus::complete&&
            std::any_of(dynamic_obstacles.obstacles.begin(),dynamic_obstacles.obstacles.end(),[](const auto& obstacle){
                return obstacle.owner_id==2&&obstacle.collection==odh::RoomInfoObstacleCollection::dynamic_list;
            }),"dynamic-footprint avatar was not registered in the dynamic obstacle list");
        dynamic.reject({168,168,1,0},Status::unsupported);
        Fixture holding;
        require(holding.world.create(3,odh::EntityKind::object)&&holding.world.attach(2,0,3),"held object fixture");
        holding.reject({152,152,1,0},Status::unsupported);
        for(const auto field:std::vector<std::pair<std::int16_t,std::int16_t>>{{7,1},{51,1},{40,0x4000}}){
            Fixture effect;
            require(effect.world.declare_object_word(2,field.first,field.second),"registration effect fixture");
            effect.reject({152,152,1,0},Status::unsupported);
        }
        Fixture portal;
        auto portal_snapshot=portal.world.snapshot();
        (*portal_snapshot.entities[1].entry_points)[15].action=4097;
        odh::World portal_world;require(portal_world.restore_snapshot(portal_snapshot),"portal fixture");
        const auto before_portal=odh::encode_world_snapshot(portal_world.snapshot());
        require(attempt_household_runtime_arrival(portal_world,2,{152,152,1,0},portal.definitions,portal.architecture)==Status::unsupported,
            "portal inventory mutation was silently omitted");
        require(odh::encode_world_snapshot(portal_world.snapshot())==before_portal,"portal failure mutated World");
        std::cout<<"PASS runtime household arrival: terrain/pool/water/bounds/story, EP5/EP9, room and footprint publication, singleton Caller retry, byte-identical rejection\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
