// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_object_state.hpp"
#include "odh/room_lighting_codec.hpp"
#include <algorithm>
#include "odh/formats.hpp"
#include <iostream>
#include <stdexcept>
using namespace odh;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message){bool rejected=false;try{action();}catch(const FormatError&){rejected=true;}require(rejected,message);}
ArchitectureObjectState fixture(){
    ArchitectureObjectState state;state.object_support=make_architecture_object_support(2,2);
    state.object_support.levels[0][1]=1;state.object_support.levels[1][3]=1;
    for(auto& grid:state.occupied_walls)grid.assign(4,0);
    state.occupied_walls[0][0]=3;state.occupied_walls[1][3]=15;
    WallAppearanceStyle global{1,0,{}};
    WallAppearanceStyle dynamic{22,0x12345678U,{{65533,65534,65535,0,1,2}}};
    state.wall_appearance.faces.push_back({0,0,1,wall_top_left,global,1,1,0x0102030405060708ULL,0x0102030405060708ULL,true,true});
    state.wall_appearance.faces.push_back({1,1,1,wall_top_right,dynamic,2,1,20,0x0102030405060708ULL,true,false});
    state.wall_appearance.applications={{1,9,6},{2,1,2},{1,0,6}};state.wall_appearance.overwritten_faces=1;
    return state;
}
void u32(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes.at(at+i)=std::uint8_t(value>>(i*8));}
}
int main(){
    auto state=fixture();const auto encoded=encode_architecture_object_state(state);
    const auto decoded=decode_architecture_object_state(encoded);
    require(encode_architecture_object_state(decoded)==encoded,"complete object-produced architecture preserves exact canonical bytes");
    require(encoded.size()==23+4*4+2*45+3*4&&encoded[0]==1&&encoded[1]==0&&encoded[2]==2&&encoded[6]==2,"explicit v1 little-endian layout independent of C++ ABI");
    require(decoded.object_support==state.object_support&&decoded.occupied_walls==state.occupied_walls,"both floors support and occupied walls survive");
    require(decoded.wall_appearance.faces[1].style.resource_guid==0x12345678U&&decoded.wall_appearance.faces[1].style.sprites[2]==65535&&decoded.wall_appearance.faces[1].style.sprites[3]==0,
        "dynamic style resource identity and wrapping sprite IDs survive without provider allocation");
    require(decoded.wall_appearance.faces[1].owner==2&&decoded.wall_appearance.faces[1].door_owner==1&&decoded.wall_appearance.faces[1].door_generation==0x0102030405060708ULL&&
        decoded.wall_appearance.faces[1].door&&!decoded.wall_appearance.faces[1].segment_present,"event style owner and preserved earlier door owner remain distinct");
    require(decoded.wall_appearance.applications[2].id==1&&decoded.wall_appearance.overwritten_faces==1,"repeated source application order and diagnostics survive");
    for(std::size_t count=0;count<encoded.size();++count){const std::vector<std::uint8_t> prefix(encoded.begin(),encoded.begin()+count);
        rejects([&]{decode_architecture_object_state(prefix);},"every truncated wire prefix rejects");}
    auto malformed=encoded;malformed.push_back(0);rejects([&]{decode_architecture_object_state(malformed);},"trailing bytes rejected");
    malformed=encoded;malformed[0]=3;rejects([&]{decode_architecture_object_state(malformed);},"unknown version rejected");
    malformed=encoded;u32(malformed,2,0xffffffffU);rejects([&]{decode_architecture_object_state(malformed);},"huge extent rejected before allocation");
    malformed=encoded;malformed[10]=2;rejects([&]{decode_architecture_object_state(malformed);},"nonboolean support rejected");
    malformed=encoded;malformed[18]=16;rejects([&]{decode_architecture_object_state(malformed);},"invalid occupied mask rejected");
    malformed=encoded;u32(malformed,26,0xffffffffU);rejects([&]{decode_architecture_object_state(malformed);},"huge face count rejected before reserve");
    malformed=encoded;malformed[30+44]=4;rejects([&]{decode_architecture_object_state(malformed);},"unknown face flag bits rejected");
    malformed=encoded;u32(malformed,30+2*45,0xffffffffU);rejects([&]{decode_architecture_object_state(malformed);},"huge application count rejected before reserve");
    const auto invalid=[&](auto mutation,const char* message){auto copy=state;mutation(copy);rejects([&]{encode_architecture_object_state(copy);},message);};
    invalid([](auto& s){s.object_support.width=3;},"mismatched support extent rejected");
    invalid([](auto& s){s.occupied_walls[0].pop_back();},"occupied extent must match support");
    invalid([](auto& s){s.wall_appearance.faces[0].x=2;},"out of bounds wall face rejected");
    invalid([](auto& s){s.wall_appearance.faces[0].segment=4;},"non-source wall overlay side rejected");
    invalid([](auto& s){std::swap(s.wall_appearance.faces[0],s.wall_appearance.faces[1]);},"unsorted wall faces rejected");
    invalid([](auto& s){s.wall_appearance.faces[1]=s.wall_appearance.faces[0];},"duplicate wall face rejected");
    invalid([](auto& s){s.wall_appearance.faces[0].generation=0;},"unknown source face generation rejected");
    invalid([](auto& s){s.wall_appearance.faces[0].door_owner=0;},"door owner and generation must agree");
    invalid([](auto& s){s.wall_appearance.faces[0].style.resource_guid=3;},"global style cannot carry hidden object binding");
    invalid([](auto& s){s.wall_appearance.faces[1].style.resource_guid=0;},"dynamic style must retain object resource");
    invalid([](auto& s){s.wall_appearance.faces[1].style.sprites[4]=99;},"dynamic sprite sequence must retain source identity");
    invalid([](auto& s){s.wall_appearance.faces[1].style.raw_style=256;},"transient process style IDs rejected");
    invalid([](auto& s){s.wall_appearance.applications[0].applied_sides=2;},"applied source style sides are only top left/right");
    invalid([](auto& s){s.wall_appearance.applications[0].ignored_sides=1;},"ignored source style sides cannot hide a rendered side");
    auto cleared=state;auto& clear=cleared.wall_appearance.faces[0];
    clear.owner=0;clear.generation=0;clear.style={0,0,{}};clear.door=false;clear.door_owner=0;clear.door_generation=0;
    const auto clear_bytes=encode_architecture_object_state(cleared);
    require(encode_architecture_object_state(decode_architecture_object_state(clear_bytes))==clear_bytes,
        "authenticated style0 clear survives byte-exact codec without a retired owner");
    auto corrupt=cleared;corrupt.wall_appearance.faces[0].style.raw_style=1;
    rejects([&]{encode_architecture_object_state(corrupt);},"ownerless nonzero style remains invalid");
    corrupt=cleared;corrupt.wall_appearance.faces[0].generation=1;
    rejects([&]{encode_architecture_object_state(corrupt);},"ownerless clear cannot carry hidden generation");
    corrupt=cleared;corrupt.wall_appearance.faces[0].style.sprites[0]=1;
    rejects([&]{encode_architecture_object_state(corrupt);},"ownerless clear cannot hide sprite data");
    ArchitectureObjectStateLimits limits;limits.bytes=encoded.size()-1;
    rejects([&]{encode_architecture_object_state(state,limits);},"encode honors exact byte budget");
    rejects([&]{decode_architecture_object_state(encoded,limits);},"decode honors exact byte budget");
    limits={};limits.faces=1;rejects([&]{decode_architecture_object_state(encoded,limits);},"caller face budget checked before allocation");
    limits={};limits.applications=2;rejects([&]{decode_architecture_object_state(encoded,limits);},"caller application budget checked before allocation");
    limits={};limits.dimension=1;rejects([&]{decode_architecture_object_state(encoded,limits);},"caller dimension budget checked before allocation");
    auto full=state;ObjectPositionRegistrySnapshot registry;registry.width=registry.height=2;registry.complete=true;
    registry.rooms.resize(2);registry.rooms[0].id=0;registry.rooms[1].id=1;
    const ObjectPositionIdentity first{1,0x0102030405060708ULL},second{2,20};
    registry.rooms[0].entities={first};registry.rooms[1].entities={second};
    registry.rooms[0].static_obstacles.push_back({first,{-3,-2,18,19},1});
    registry.rooms[1].dynamic_obstacles={{second,{2,3,9,11},2},{second,{2,3,9,11},2}};
    registry.rooms[0].portals.push_back({first,1});registry.rooms[0].window_portals.push_back({first,1});
    registry.rooms[1].boundaries_known[0]=true;registry.rooms[1].boundaries[0].push_back({-3,-3,35,35});
    registry.rooms[1].room_score=-37;registry.tiles={{-1,{first}},{3,{second}}};
    registry.deferred_lighting={1,0};registry.score_dirty={0,1};registry.motion={{first,true},{second,false},{{3,30},false}};
    registry.next_footprint_instance=3;
    registry.footprints={{first,true,std::nullopt,{4,5,12,15},3},{second,true,std::uint16_t{1},{2,3,9,11},2},{{3,30},false,std::nullopt,{},0}};
    full.position_registry=registry;const auto full_bytes=encode_architecture_object_state(full);
    const auto restored=decode_architecture_object_state(full_bytes);
    require(restored.position_registry&&encode_architecture_object_state(restored)==full_bytes,"complete event-owned room registry survives exact native state codec");
    auto illuminated=full;RoomLightingSnapshot lighting;RoomLighting light;
    light.base_room=1;light.ambient_light=30;light.outside_light=10;light.bounds={0,0,2,2};
    light.room_score=registry.rooms[1].room_score.value_or(0);light.components.push_back(second);
    lighting.by_base_room.emplace(1,light);lighting.visual_rooms.push_back({second,65535});illuminated.lighting=lighting;
    const auto lit_bytes=encode_architecture_object_state(illuminated);
    require(lit_bytes[0]==2&&full_bytes[0]==1,"lighting opts into AOBJ2 while legacy state stays AOBJ1");
    const auto lit_restored=decode_architecture_object_state(lit_bytes);
    require(lit_restored.lighting&&encode_architecture_object_state(lit_restored)==lit_bytes&&
        lit_restored.position_registry->deferred_lighting==registry.deferred_lighting,
        "processed lighting and still-pending requests round trip together");
    require(!decode_architecture_object_state(full_bytes).lighting&&
        encode_architecture_object_state(decode_architecture_object_state(full_bytes))==full_bytes,
        "legacy absence remains absence without a fabricated black room");
    illuminated.lighting_committed=lighting;
    illuminated.lighting->by_base_room.at(1).ambient_light=80;
    const auto delayed_bytes=encode_architecture_object_state(illuminated);
    const auto delayed=decode_architecture_object_state(delayed_bytes);
    require(delayed.lighting->by_base_room.at(1).ambient_light==80&&
        delayed.lighting_committed->by_base_room.at(1).ambient_light==30&&
        encode_architecture_object_state(delayed)==delayed_bytes,
        "simulation and last committed visual lighting can differ across reopen");
    auto orphan=illuminated;orphan.position_registry.reset();
    rejects([&]{encode_architecture_object_state(orphan);},"lighting requires its registry authority");
    auto foreign_light=illuminated;
    auto foreign_room=foreign_light.lighting_committed->by_base_room.at(1);foreign_room.base_room=2;
    foreign_light.lighting_committed->by_base_room.emplace(2,foreign_room);
    rejects([&]{encode_architecture_object_state(foreign_light);},"committed room cannot exceed actual registry");
    const auto committed_blob=encode_room_lighting_snapshot(*illuminated.lighting_committed);
    auto foreign_payload=delayed_bytes;
    const auto committed_at=std::search(foreign_payload.begin(),foreign_payload.end(),committed_blob.begin(),committed_blob.end());
    require(committed_at!=foreign_payload.end(),"locate exact official committed-lighting payload");
    committed_at[8]=2;committed_at[9]=0;
    rejects([&]{decode_architecture_object_state(foreign_payload);},"decoder rejects committed room outside registry too");
    const auto& loaded=*restored.position_registry;
    require(loaded.complete&&loaded.rooms.size()==2&&loaded.rooms[0].entities[0].generation==first.generation&&
        loaded.rooms[1].dynamic_obstacles.size()==2&&loaded.rooms[1].dynamic_obstacles[1].box==CollisionBox{2,3,9,11},"source memberships and repeated dynamic footprint history retained");
    require(loaded.rooms[0].portals[0].target_room==1&&loaded.rooms[0].window_portals.size()==1&&
        loaded.rooms[1].boundaries_known[0]&&!loaded.rooms[1].boundaries_known[1]&&loaded.rooms[1].boundaries[0][0].x1==-3&&loaded.rooms[1].room_score==std::optional<std::int16_t>{-37},"portal, window, known boundary and signed room score remain distinct");
    require(loaded.tiles[0].offset==-1&&loaded.deferred_lighting==std::vector<std::uint16_t>{1,0}&&loaded.score_dirty==std::vector<std::uint16_t>{0,1}&&
        loaded.motion.size()==3&&loaded.motion[0].moved_self&&!loaded.motion[1].moved_self,"out-of-world queries, dirty queue order and actual motion flags survive");
    require(loaded.next_footprint_instance==3&&loaded.rooms[0].static_obstacles[0].instance==1&&loaded.footprints[0].instance==3&&
        loaded.footprints[0].owner.id==1&&!loaded.footprints[0].room&&loaded.footprints[0].box==CollisionBox{4,5,12,15},"old orphan obstacle and distinct current same-owner footprint pointer survive");
    require(loaded.footprints[1].room==std::optional<std::uint16_t>{1}&&loaded.footprints[1].instance==loaded.rooms[1].dynamic_obstacles[0].instance&&
        !loaded.footprints[2].present&&loaded.footprints[2].instance==0&&!loaded.footprints[2].room,"actual dynamic collection binding and known absent footprint survive distinctly");
    for(std::size_t count=encoded.size()-1;count<full_bytes.size();++count){const std::vector<std::uint8_t> prefix(full_bytes.begin(),full_bytes.begin()+count);
        rejects([&]{decode_architecture_object_state(prefix);},"every truncated registry prefix rejects");}
    auto invalid_full=full;invalid_full.position_registry->rooms[1].boundaries_known[0]=false;
    rejects([&]{encode_architecture_object_state(invalid_full);},"unknown boundary cannot carry invented geometry");
    invalid_full=full;invalid_full.position_registry->height=3;rejects([&]{encode_architecture_object_state(invalid_full);},"registry extent must match support");
    limits={};limits.registry_entries=1;rejects([&]{decode_architecture_object_state(full_bytes,limits);},"registry collection budget enforced before allocation");
    // Final motion byte precedes counter(8), binding count(4), records(36+38+36) and retired count(4).
    malformed=full_bytes;malformed[malformed.size()-(8+4+36+38+36+4)-1]=2;
    rejects([&]{decode_architecture_object_state(malformed);},"nonboolean saved movement history rejected");
    invalid_full=full;invalid_full.position_registry->rooms[1].dynamic_obstacles[0].instance=1;
    rejects([&]{encode_architecture_object_state(invalid_full);},"same source obstacle instance cannot name distinct owners");
    invalid_full=full;invalid_full.position_registry->footprints[0].instance=1;
    rejects([&]{encode_architecture_object_state(invalid_full);},"same source obstacle instance cannot carry conflicting box copies");
    invalid_full=full;invalid_full.position_registry->footprints[2].room=0;
    rejects([&]{encode_architecture_object_state(invalid_full);},"known absent footprint cannot retain a collection binding");
    invalid_full=full;invalid_full.position_registry->next_footprint_instance=1;
    rejects([&]{encode_architecture_object_state(invalid_full);},"footprint counter cannot precede existing instance identities");
    auto historical=full;historical.position_registry->complete=false;
    historical.position_registry->retired_owners={first,{first.id,first.generation+1},{8,80}};
    const auto historical_bytes=encode_architecture_object_state(historical);
    const auto historical_restore=decode_architecture_object_state(historical_bytes);
    require(historical_restore.position_registry&&!historical_restore.position_registry->complete&&
        historical_restore.position_registry->retired_owners.size()==3&&historical_restore.position_registry->retired_owners[0].id==1&&
        historical_restore.position_registry->retired_owners[1].generation==first.generation+1&&encode_architecture_object_state(historical_restore)==historical_bytes,
        "incomplete registry preserves explicit retired generations in sorted source order");
    require(historical_restore.position_registry->rooms[0].entities.size()==1&&historical_restore.position_registry->rooms[0].static_obstacles[0].instance==1&&
        historical_restore.position_registry->footprints[0].instance==3,"retired tags do not prune source historical references or invent a clean registry");
    invalid_full=historical;invalid_full.position_registry->complete=true;
    rejects([&]{encode_architecture_object_state(invalid_full);},"retired references cannot certify complete query authority");
    invalid_full=historical;invalid_full.position_registry->retired_owners[1]=first;
    rejects([&]{encode_architecture_object_state(invalid_full);},"duplicate retired identity cannot hide competing history");
    invalid_full=historical;std::swap(invalid_full.position_registry->retired_owners[0],invalid_full.position_registry->retired_owners[1]);
    rejects([&]{encode_architecture_object_state(invalid_full);},"retired identities must retain strict ID/generation order");
    invalid_full=historical;invalid_full.position_registry->retired_owners[0].id=0;
    rejects([&]{encode_architecture_object_state(invalid_full);},"retired source owner must be positive");
    invalid_full=historical;invalid_full.position_registry->retired_owners[0].generation=0;
    rejects([&]{encode_architecture_object_state(invalid_full);},"retired source generation must be known");
    malformed=full_bytes;u32(malformed,malformed.size()-4,0xffffffffU);
    rejects([&]{decode_architecture_object_state(malformed);},"huge retired identity count rejects before allocation");
    require(!decoded.position_registry,"absent registry remains explicitly unknown without source replay");
    std::cout<<"native object-produced architecture codec passed\n";
}
