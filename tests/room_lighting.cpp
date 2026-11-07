#include "odh/room_lighting.hpp"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){try{f();}catch(const odh::FormatError&){return;}throw std::runtime_error(why);}
bool near(float a,float b){return std::fabs(a-b)<0.0001f;}
struct Fixture {
    odh::World world;
    odh::GeometricRoomMap map;
    odh::ObjectPositionRegistrySnapshot registry;
    std::vector<odh::DefinitionCandidate> candidates;
    std::unique_ptr<odh::DefinitionIndex> definitions;
    std::map<std::int16_t,std::vector<odh::ObjectPositionIdentity>> groups;
    std::map<std::int16_t,odh::RoomLightingObjectState> states;
    Fixture(){map.width=map.height=64;map.light_base_rooms_resolved=map.floor_resources_resolved=true;
        map.rooms={{0,0,0,{},false,false,{},0},{1,0,50,{0,0,5,10},false,false,{2},1},
            {2,0,50,{5,0,5,10},false,false,{1},1},{3,0,100,{10,0,10,10},false,false,{},3},
            {4,0,100,{20,0,10,10},true,false,{},4}};
        registry.width=registry.height=64;registry.complete=true;for(const auto&r:map.rooms){odh::ObjectPositionRegistryRoom room;room.id=r.id;registry.rooms.push_back(room);}}
    odh::ObjectPositionIdentity add(std::int16_t id,std::uint16_t room,std::int16_t contribution,std::int16_t impact=0,std::int32_t x=0,std::uint16_t flags2=0){
        require(world.create_runtime_object(id,1000U+id,{},{}),"create actual lighting World object");
        require(world.set_position(id,{x,16,1,0}),"place lighting object");
        set(id,29,static_cast<std::int16_t>(room-1));set(id,51,contribution);set(id,7,impact);set(id,40,static_cast<std::int16_t>(flags2));
        const odh::ObjectPositionIdentity owner{id,world.find(id)->generation};registry.rooms.at(room).entities.push_back(owner);
        auto& entities=registry.rooms.at(room).entities;std::sort(entities.begin(),entities.end(),[](auto a,auto b){return a.id<b.id;});
        registry.footprints.push_back({owner,true,room,{x-4,12,x+4,20},static_cast<std::uint64_t>(id)});
        odh::ObjectDefinition definition{};definition.guid=1000U+id;candidates.push_back({"fixture","",static_cast<std::uint16_t>(id),definition});
        groups[id]={owner};states[id]={};return owner;
    }
    void set(std::int16_t id,std::int16_t index,std::int16_t value){require(world.declare_object_word(id,index,value),"lighting source ObjectData available");}
    void group(std::initializer_list<std::int16_t> ids){std::vector<odh::ObjectPositionIdentity> list;for(auto id:ids)list.push_back({id,world.find(id)->generation});for(auto id:ids)groups[id]=list;}
    odh::RoomLightingServices services(){definitions=std::make_unique<odh::DefinitionIndex>(candidates);odh::RoomLightingServices s;s.definitions=definitions.get();
        s.group=[&](auto id)->std::optional<std::vector<odh::ObjectPositionIdentity>>{const auto i=groups.find(id.id);return i==groups.end()?std::nullopt:std::optional<std::vector<odh::ObjectPositionIdentity>>{i->second};};
        s.object_state=[&](auto id)->std::optional<odh::RoomLightingObjectState>{const auto i=states.find(id.id);return i==states.end()?std::nullopt:std::optional<odh::RoomLightingObjectState>{i->second};};return s;}
};
}
int main(){try{
    {odh::World world;require(!odh::recalculate_room_lighting(world,{}, {},0,{}),"dummy room is source no-op without services");}
    {Fixture f;auto a=f.add(1,1,40,30,0),b=f.add(2,2,60,30,64);f.group({2,1});const auto service=f.services();
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,2,service);
        require(light&&light->base_room==1&&light->ambient_light==100&&light->outside_light==0&&light->room_score==-3&&
            light->bounds.x==0&&light->bounds.width==10&&light->lights.size()==2,
            "support rooms share base, area, light totals and score while physical group order remains exact");
        odh::RoomLightingSnapshot snapshot;snapshot.by_base_room[1]=*light;
        require(odh::room_lighting_for_room(snapshot,f.map,2)==&snapshot.by_base_room.at(1)&&odh::room_lighting_score(snapshot,f.map,2)==-3&&
            odh::room_lighting_score(snapshot,f.map,999)==0,"GetRoomScore resolves light base and out-of-range zero");}
    {Fixture f;f.add(1,1,30,0,0);f.add(2,1,40,0,64);f.group({2,1});auto s=f.services();
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);
        require(light&&light->lights.size()==1&&near(light->lights[0].x,32)&&light->lights[0].component->id==2&&light->ambient_light==70,
            "fully lit non-window group creates single source averaged in physical order");}
    {Fixture f;f.add(1,1,5,0,0);f.add(2,1,0,0,64);f.add(3,1,9,0,128);f.group({3,1,2});auto s=f.services();
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);
        require(light&&light->lights.size()==2&&light->lights[0].component->id==3&&light->lights[1].component->id==1&&light->ambient_light==14,
            "partially lit group creates sources only from contributing pieces preserving source group order");}
    for(const auto architectural:{std::uint16_t{0x4000},std::uint16_t{0x8000}}){Fixture f;f.add(1,1,300,0,0,architectural);f.add(2,1,0,0,64);f.group({1,2});auto s=f.services();
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);
        require(light&&light->lights.size()==2&&light->outside_light==100&&light->ambient_light==0&&
            light->lights[1].outdoors_color&&near(light->lights[0].intensity,0.8f/1.5f),
            "windows and doors keep every piece separate including zero, and attenuate excessive exterior contribution");}
    {Fixture f;f.add(1,4,100,300,0);const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,4,f.services());
        require(light&&light->ambient_light==0&&light->outside_light==100&&light->room_score==-5,"outside base overrides levels and uses area-score scale30");}
    {Fixture f;const auto a=f.add(1,1,40,99,0);f.states[1].disabled=1;
        auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());require(light&&light->lights.empty()&&light->ambient_light==0&&light->room_score==-10,
            "disabled representative omits lighting and score for its physical group");
        f.states[1].disabled=0;f.set(1,51,0);light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());
        require(light&&light->object_footprints.size()==1&&light->components.size()==1,"unlit stationary object contributes footprint and component");
        f.set(1,8,16);light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());require(light->object_footprints.empty()&&light->components.size()==1,
            "AllowPersonIntersection removes light bounds without removing component");
        f.set(1,8,18);light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());require(light->object_footprints.size()==1,"DisallowPersonIntersection overrides allow flag for light bounds");
        f.states[1].moves_often=true;light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());require(light->object_footprints.empty()&&light->components.empty(),"moving object has no static lighting blocker");}
    {Fixture f;f.add(1,1,40,99);f.states[1].disabled=1;auto s=f.services();odh::RoomLightingSnapshot state;
        const auto full=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);state.by_base_room[1]=*full;
        require(full->room_score==-10&&full->ambient_light==0,"full lighting excludes a disabled group's impact");
        f.registry.deferred_lighting={3};
        require(odh::refresh_room_lighting_score(f.world,f.map,f.registry,state,1)&&state.by_base_room.at(1).room_score==1&&
            state.by_base_room.at(1).ambient_light==0&&state.by_base_room.at(1).lights.empty()&&f.registry.rooms[1].room_score==1&&
            f.registry.deferred_lighting==std::vector<std::uint16_t>{3},
            "immediate score includes disabled RoomImpact and changes only shared score, not levels or pending lighting");
        f.registry.deferred_lighting={1};odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s);
        require(state.by_base_room.at(1).room_score==-10&&f.registry.rooms[1].room_score==-10,
            "later full refresh wins on the same shared score using its own source contribution filter");
        require(odh::refresh_room_lighting_score(f.world,f.map,f.registry,state,3)&&state.by_base_room.at(3).room_score==-10&&
            state.by_base_room.at(3).ambient_light==0&&state.by_base_room.at(3).outside_light==0&&state.by_base_room.at(3).bounds.width==0,
            "immediate score can initialize source Light with default levels/bounds without calculating lighting");
        require(odh::refresh_room_lighting_score(f.world,{},f.registry,state,0),"room0 immediate refresh is unconditional no-op");}
    {Fixture f;auto a=f.add(1,1,0,0,0);f.registry.rooms[1].window_portals={{a,3},{a,4}};auto s=f.services();std::vector<std::uint16_t> affected;
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s,{},&affected);
        require(light&&light->lights.size()==1&&light->lights[0].window_room==3&&near(light->lights[0].size,100)&&affected==std::vector<std::uint16_t>{3},
            "window portal propagates interior target and skips exterior base");}
    {Fixture f;const auto a=f.add(1,1,0);f.map.rooms.push_back({5,0,100,{40,0,10,10},false,false,{},5});odh::ObjectPositionRegistryRoom extra;extra.id=5;f.registry.rooms.push_back(extra);
        f.registry.rooms[1].window_portals={{a,3},{a,5}};std::vector<std::uint16_t> affected;
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services(),{},&affected);
        require(light&&light->lights.size()==1&&light->lights[0].window_room==3&&affected==std::vector<std::uint16_t>({3,5}),
            "affected window bases survive clustering even when merged source keeps only first WindowRoom");}
    {Fixture f;f.add(1,1,20,0,16);f.states[1].particle=odh::RoomLightParticle{4,-2,3.0f};
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());
        require(light&&near(light->lights[0].x,20)&&near(light->lights[0].y,14)&&near(light->lights[0].height,3)&&light->lights[0].bounds.x==-144,
            "known particle component changes source position/height while preserving source constructor bounds before clustering");}
    {Fixture f;f.add(1,1,20,0,0);f.add(2,1,30,0,8);f.add(3,1,40,0,16);f.states[1].color={20,30,40,255};f.states[2].color={30,40,50,255};f.states[3].color={40,50,60,255};
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());
        require(light&&light->lights.size()==1&&near(light->lights[0].x,10)&&near(light->lights[0].weight,1)&&
            light->lights[0].color==std::array<std::uint8_t,4>{90,120,150,255}&&near(light->lights[0].size,211.2f)&&near(light->lights[0].intensity,1.25f),
            "source clustering preserves pairwise fixed-weight quirk, saturated RGBA and intensity cap");}
    {Fixture f;f.add(1,1,1,32767,0);f.add(2,1,-1,32767,64);f.group({1,2});auto s=f.services();
        auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);
        require(light&&light->ambient_light==0&&light->room_score==100&&light->lights.size()==2,
            "partial ushort contributions wrap at65536 while area100 scales impact before short cast and clamp");
        f.map.rooms[1].area=6;f.map.rooms[2].area=6;
        light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);
        require(light&&light->ambient_light==0&&light->room_score==-12&&light->lights.size()==2,
            "area12 uses scale1 so impact65534 casts to short-2 before subtracting10");}
    {Fixture f;f.map.rooms[1].area=6;f.map.rooms[2].area=6;f.add(1,1,0,32767);f.add(2,2,0,32767);
        odh::RoomLightingSnapshot state;
        require(odh::refresh_room_lighting_score(f.world,f.map,f.registry,state,2)&&state.by_base_room.at(1).room_score==-12&&f.registry.rooms[1].room_score==-12,
            "registry-backed immediate score shares large-impact short wrapping across support rooms");}
    {Fixture f;f.map.rooms[1].area=6;f.map.rooms[2].area=6;f.add(1,1,1,-32768);
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,f.services());
        require(light&&light->room_score==-100,"RoomScore subtraction remains int after source short cast, without a second wrap");}
    {Fixture f;const auto id=f.add(1,1,20,0,0,0x400);odh::RoomLightingSnapshot state;
        require(odh::refresh_room_lighting_visual(state,f.world,id,0)&&odh::room_lighting_cached_visual_room(state,id)==65535,"actual RefreshLight records emitter classification");
        f.set(1,51,0);require(odh::room_lighting_cached_visual_room(state,id)==65535,"raw field write never prematurely changes cached visual room");
        require(odh::refresh_room_lighting_visual(state,f.world,id,0)&&odh::room_lighting_cached_visual_room(state,id)==1,"explicit refresh reclassifies ordinary room");
        require(odh::refresh_room_lighting_visual(state,f.world,id,4)&&odh::room_lighting_cached_visual_room(state,id)==65533,"disabled threshold records grayscale classification");
        auto wrong=id;++wrong.generation;require(!odh::refresh_room_lighting_visual(state,f.world,wrong,0)&&!odh::room_lighting_cached_visual_room(state,wrong),"visual room update rejects reused identity");
        odh::erase_room_lighting_visual(state,id);require(state.visual_rooms.empty(),"retirement removes only exact visual identity");}
    {Fixture f;f.map.rooms.resize(3);f.registry.rooms.resize(3);f.add(1,1,10,0,0);f.add(2,1,20,0,64);auto s=f.services();
        odh::RoomLightingLimits limits;limits.cluster_comparisons=1;odh::RoomLightingSnapshot state;
        f.registry.deferred_lighting={2,1};const auto queue=f.registry.deferred_lighting;
        const auto full=odh::refresh_all_room_lighting(f.world,f.map,f.registry,state,s,limits);
        require(full.processed_rooms==std::vector<std::uint16_t>({0,1,2})&&full.refreshed_bases==std::vector<std::uint16_t>{1}&&
            full.visual_commit&&state.by_base_room.size()==1&&state.by_base_room.at(1).lights.size()==2&&f.registry.deferred_lighting==queue,
            "full refresh shares visited bases and final alias still commits without consuming pending queue");
        rejects([&]{odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s,limits);},
            "deferred aliases retain separate recalculation semantics unlike shared-visited bootstrap");
        require(f.registry.deferred_lighting==queue,"failed later drain cannot erase pending queue after full bootstrap");}
    {Fixture f;f.map.rooms.resize(1);f.registry.rooms.resize(1);f.registry.deferred_lighting={0};odh::RoomLightingSnapshot state;
        const auto full=odh::refresh_all_room_lighting(f.world,f.map,f.registry,state,{});
        require(full.processed_rooms==std::vector<std::uint16_t>{0}&&full.refreshed_bases.empty()&&!full.visual_commit&&
            state.by_base_room.empty()&&f.registry.deferred_lighting==std::vector<std::uint16_t>{0},
            "full refresh with room0 only is no-op and cannot fabricate a commit or consume dirty state");}
    {Fixture f;f.add(1,1,10,0,0);f.add(2,1,20,0,64);f.add(3,3,30,0,0);f.add(4,3,40,0,64);auto s=f.services();
        odh::RoomLightingSnapshot state;f.registry.deferred_lighting={3,1};odh::RoomLightingLimits limits;limits.cluster_comparisons=1;
        rejects([&]{odh::refresh_all_room_lighting(f.world,f.map,f.registry,state,s,limits);},"full refresh must bound cumulative work across distinct bases");
        require(state.by_base_room.empty()&&f.registry.deferred_lighting==std::vector<std::uint16_t>({3,1}),"failed full refresh preserves old state and exact dirty queue");
        limits.cluster_comparisons=2;const auto full=odh::refresh_all_room_lighting(f.world,f.map,f.registry,state,s,limits);
        require(full.visual_commit&&full.refreshed_bases==std::vector<std::uint16_t>({1,3,4})&&f.registry.deferred_lighting==std::vector<std::uint16_t>({3,1}),
            "successful full refresh visits all bases within one shared work allowance while preserving subsequent requests");}
    {Fixture f;f.add(1,1,20);auto s=f.services();odh::RoomLightingSnapshot state;f.registry.deferred_lighting={1,2};f.registry.score_dirty={1,2,3};
        const auto receipt=odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s);
        require(receipt.processed_rooms==std::vector<std::uint16_t>({1,2})&&receipt.refreshed_bases==std::vector<std::uint16_t>({1,1})&&receipt.visual_commit&&
            f.registry.deferred_lighting.empty()&&f.registry.score_dirty==std::vector<std::uint16_t>{3}&&f.registry.rooms[1].room_score==state.by_base_room.at(1).room_score,
            "each dirty item has a fresh visited set; processing updates shared score and acknowledges queue only after calculation");}
    {Fixture f;f.add(1,1,20);auto s=f.services();auto read=s.object_state;bool appended=false;s.object_state=[&](auto id){if(!appended){appended=true;f.registry.deferred_lighting.push_back(3);}return read(id);};
        odh::RoomLightingSnapshot state;f.registry.deferred_lighting={1};const auto receipt=odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s);
        require(receipt.processed_rooms==std::vector<std::uint16_t>({1,3})&&receipt.visual_commit&&state.by_base_room.count(3),"requests appended while processing are consumed in source order");}
    {Fixture f;f.add(1,1,20);auto s=f.services();odh::RoomLightingSnapshot state;f.registry.deferred_lighting={1,0};const auto receipt=odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s);
        require(!receipt.visual_commit&&state.by_base_room.count(1)&&!state.by_base_room.count(0),"last dirty room0 is source no-op and cannot fabricate a visual commit");
        f.registry.deferred_lighting={1,1};rejects([&]{odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s);},"duplicate raw queue accepted");require(f.registry.deferred_lighting.size()==2,"failure preserves unacknowledged queue");}
    {Fixture f;const auto a=f.add(1,1,20);f.add(2,3,30);auto s=f.services();f.states.erase(2);
        odh::RoomLightingSnapshot state;require(odh::refresh_room_lighting_visual(state,f.world,a,0),"existing visual authority before failed drain");
        f.registry.deferred_lighting={1,3};
        rejects([&]{odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s);},"unknown later room metadata must not acknowledge batch");
        require(state.by_base_room.empty()&&state.visual_rooms.size()==1&&f.registry.deferred_lighting==std::vector<std::uint16_t>({1,3}),
            "failure after a successful calculation preserves previous lighting/visual authority and all pending requests");}
    {Fixture f;f.add(1,1,10,0,0);f.add(2,1,20,0,64);f.add(3,3,30,0,0);f.add(4,3,40,0,64);auto s=f.services();
        odh::RoomLightingLimits limit;limit.cluster_comparisons=1;
        const auto first=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s,limit);
        const auto second=odh::recalculate_room_lighting(f.world,f.map,f.registry,3,s,limit);
        require(first&&second&&first->lights.size()==2&&second->lights.size()==2,
            "each individual room needs exactly one bounded cluster comparison");
        odh::RoomLightingSnapshot state;f.registry.deferred_lighting={1,3};
        rejects([&]{odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s,limit);},
            "drain must share work allowance instead of multiplying it by dirty rooms");
        require(state.by_base_room.empty()&&f.registry.deferred_lighting==std::vector<std::uint16_t>({1,3}),
            "aggregate work exhaustion preserves old lighting and complete pending batch");
        limit.cluster_comparisons=2;const auto receipt=odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s,limit);
        require(receipt.visual_commit&&f.registry.deferred_lighting.empty()&&state.by_base_room.at(1).ambient_light==first->ambient_light&&
            state.by_base_room.at(3).ambient_light==second->ambient_light&&state.by_base_room.at(1).room_score==first->room_score&&
            state.by_base_room.at(3).room_score==second->room_score&&state.by_base_room.at(1).lights.size()==first->lights.size()&&
            state.by_base_room.at(3).lights.size()==second->lights.size(),
            "sufficient cumulative work produces the same source room calculations");}
    {Fixture f;f.add(1,1,10);f.add(2,1,20,0,64);auto s=f.services();odh::RoomLightingLimits limit;limit.lights=1;
        rejects([&]{odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s,limit);},"source light bound ignored");limit={};limit.cluster_comparisons=0;
        rejects([&]{odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s,limit);},"cluster work bound ignored");
        odh::RoomLightingSnapshot state;f.registry.deferred_lighting={1};limit={};limit.deferred_requests=0;
        rejects([&]{odh::consume_deferred_lighting(f.world,f.map,f.registry,state,s,limit);},"deferred count bound ignored");require(state.by_base_room.empty()&&f.registry.deferred_lighting==std::vector<std::uint16_t>{1},"failed drain does not publish or clear");
        const auto light=odh::recalculate_room_lighting(f.world,f.map,f.registry,1,s);state.by_base_room[1]=*light;limit={};limit.lights=1;
        rejects([&]{odh::validate_room_lighting_snapshot(state,limit);},"aggregate structural light bound ignored");state.by_base_room[1].lights[0].x=std::numeric_limits<float>::quiet_NaN();
        rejects([&]{odh::validate_room_lighting_snapshot(state);},"nonfinite wire source accepted");}
    std::cout<<"room lighting source contract checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
