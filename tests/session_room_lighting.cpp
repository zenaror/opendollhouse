// SPDX-License-Identifier: MPL-2.0
#include "session_room_lighting.hpp"
#include "session_refresh.hpp"
#include "odh/room_lighting_codec.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void word(odh::World& world,std::int16_t field,std::int16_t value){odh::VmFrame frame;frame.context.caller=1;require(world.write(frame,3,field,value),"write actual lamp field");}
}
int main(){try{
    odh::World world;require(world.create_runtime_object(1,1001,{},{}),"create lamp");
    require(world.set_position(1,{24,24,1,0}),"position lamp");
    word(world,29,0);word(world,40,0x400);word(world,51,200);word(world,7,20);
    const odh::ObjectPositionIdentity identity{1,world.find(1)->generation};
    odh::ObjectDefinition definition{};definition.guid=1001;
    odh::ObjectDefinition resident_definition{};resident_definition.guid=1002;
    odh::DefinitionIndex definitions({{"fixture","",128,definition},{"fixture","",129,resident_definition}});
    odh::GeometricRoomMap map;map.width=map.height=4;map.floor_resources_resolved=map.light_base_rooms_resolved=true;
    map.rooms={{0,0,0,{},false,false,{},0},{1,0,16,{0,0,4,4},false,false,{},1},
        {2,1,16,{0,0,4,4},false,false,{},2}};
    for(auto& grid:map.levels)grid.assign(16,0x00010001U);
    map.levels[1].assign(16,0x00020002U);
    odh::ArchitectureObjectState state;state.object_support=odh::make_architecture_object_support(4,4);
    for(auto& grid:state.occupied_walls)grid.assign(16,0);
    state.position_registry.emplace();auto& registry=*state.position_registry;
    registry.width=registry.height=4;registry.complete=true;registry.rooms.resize(3);registry.rooms[1].id=1;registry.rooms[2].id=2;
    require(world.create_saved_entity(2,odh::EntityKind::avatar,1002,{},{},{},{},{},0)&&
        world.set_position(2,{40,24,1,0}),"import positioned resident");
    const odh::ObjectPositionIdentity resident{2,world.find(2)->generation};
    registry.rooms[1].entities={identity,resident};registry.footprints.push_back({identity,false,{},{},0});
    const auto services=[&]{odh::RoomLightingServices result;result.definitions=&definitions;
        result.group=[&](auto id)->std::optional<std::vector<odh::ObjectPositionIdentity>>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return std::vector<odh::ObjectPositionIdentity>{id};};
        result.object_state=[&](auto id)->std::optional<odh::RoomLightingObjectState>{
            if(!world.is_live_reference({id.id,id.generation}))return {};return odh::RoomLightingObjectState{};};return result;};
    odh_host::SessionRoomLighting lighting(world,[&]{return &state;},[&]{return &map;},[]{return static_cast<odh::ObjectPositionRegistry*>(nullptr);},services);
    require(!lighting.current(),"legacy absence is not fabricated initial darkness");
    lighting.prepare_capture();require(!state.lighting,"zero-tick capture preserves legacy absence");
    lighting.refresh(identity);require(lighting.visual_room(1,identity.generation)==std::optional<std::uint16_t>{65535},"Refresh updates visual emitter classification immediately");
    require(registry.deferred_lighting==std::vector<std::uint16_t>{1},
        "bootstrap does not invent deferred requests for other rooms");
    require(!lighting.visual_room(2,resident.generation),"object Refresh alone does not publish the resident");
    lighting.finish_loading();
    require(lighting.visual_room(2,resident.generation)==std::optional<std::uint16_t>{1}&&state.lighting_committed,
        "load completion publishes the actual resident room before first frame");
    require(registry.deferred_lighting==std::vector<std::uint16_t>{1},"first frame does not consume a tick's pending work");
    // Original House05 SIMI globals: 06:30 with 44 in Global[6]. At tick
    // zero the activator exposes 2640 seconds; daylight samples 07:14.
    odh::Ts1Clock fresh_clock;
    require(fresh_clock.initialize_simi(6,16,30,44,6,1997),"House05 clock import");
    const auto initial=lighting.view(fresh_clock);
    require(initial&&initial->room_colors==odh_host::make_room_lighting_view(*state.lighting_committed,map,7,14,0).room_colors&&
        fresh_clock.snapshot().ticks==0&&fresh_clock.snapshot().minute_fraction==1320,"first frame uses the actual carry without ticking");
    odh::Ts1Clock restored_clock;
    require(restored_clock.restore(fresh_clock.snapshot())&&lighting.view(restored_clock)->room_colors==initial->room_colors,
        "tick-zero restored clock gives identical room colors");
    require(restored_clock.tick(true)&&lighting.view(restored_clock)->room_colors==
        odh_host::make_room_lighting_view(*state.lighting_committed,map,6,31,0).room_colors,
        "first actual tick consumes the SIMI carry once");
    lighting.process();require(state.lighting&&state.lighting_committed,"real drain publishes initial lighting");
    require(state.lighting->by_base_room.count(2)==1,"initial full pass calculates rooms with no pending request");
    const auto before=state.lighting_committed->by_base_room.at(1).ambient_light;require(before>0,"lamp really illuminates room");
    word(world,51,0);lighting.refresh(identity);registry.deferred_lighting.push_back(0);
    lighting.process();
    require(state.lighting->by_base_room.at(1).ambient_light==0&&
        state.lighting_committed->by_base_room.at(1).ambient_light==before,
        "final room0 request preserves prior visual publication while simulation updates");
    require(lighting.visual_room(1,identity.generation)==std::optional<std::uint16_t>{1},"non-emitting visual class updates independently of room-color commit");
    lighting.prepare_capture();const auto bytes=odh::encode_architecture_object_state(state);
    auto reopened=odh::decode_architecture_object_state(bytes);
    odh_host::SessionRoomLighting resumed(world,[&]{return &reopened;},[&]{return &map;},[]{return static_cast<odh::ObjectPositionRegistry*>(nullptr);},services);
    resumed.prepare_capture();require(odh::encode_architecture_object_state(reopened)==bytes,"fresh-process lighting capture remains byte identical");
    resumed.refresh(identity);resumed.process();require(reopened.lighting_committed->by_base_room.at(1).ambient_light==0,"next real commit publishes switched-off room");
    const auto ambient=reopened.lighting->by_base_room.at(1).ambient_light;
    word(world,7,40);resumed.refresh_score(identity);
    require(resumed.score_at({24,24,1,0})->score==20&&reopened.position_registry->rooms[1].room_score==20&&
        reopened.lighting->by_base_room.at(1).room_score==20&&reopened.position_registry->deferred_lighting.empty()&&
        reopened.lighting->by_base_room.at(1).ambient_light==ambient,
        "immediate score updates one authority without recalculating or publishing light");
    // A newly purchased object may not yet have participated in the next
    // scheduler lighting pass when its committed transaction is reopened.
    // Restore must draw its true room before process(), without consuming any
    // event receipts or replacing the previously committed color publication.
    {
        odh::World purchased_world;require(purchased_world.restore_snapshot(world.snapshot()),"purchase restore source World");
        require(purchased_world.create_runtime_object(3,1003,{},{})&&purchased_world.set_position(3,{24,40,1,0}),
            "position new purchased singleton");
        odh::VmFrame purchased;purchased.context.caller=3;
        require(purchased_world.write(purchased,3,29,0),"purchase event publishes actual room1");
        const odh::ObjectPositionIdentity purchased_id{3,purchased_world.find(3)->generation};
        auto candidate=reopened;candidate.position_registry->rooms[1].entities.push_back(purchased_id);
        candidate.position_registry->deferred_lighting={1,0};
        require(!odh::room_lighting_cached_visual_room(*candidate.lighting,purchased_id),"new purchase begins without a visual cache entry");
        const auto original_lighting=odh::encode_room_lighting_snapshot(*candidate.lighting);
        const auto original_commit=odh::encode_room_lighting_snapshot(*candidate.lighting_committed);
        const auto original_state=odh::encode_architecture_object_state(candidate);
        auto transaction_checkpoint=odh::decode_architecture_object_state(odh::encode_architecture_object_state(candidate));
        unsigned recalculations=0;
        odh_host::SessionRoomLighting restored_purchase(purchased_world,[&]{return &transaction_checkpoint;},[&]{return &map;},
            []{return static_cast<odh::ObjectPositionRegistry*>(nullptr);},[&]{++recalculations;return services();});
        require(restored_purchase.visual_room(3,purchased_id.generation)==std::optional<std::uint16_t>{1},
            "fresh purchase has its authored room before first frame or process");
        auto original_cohort=transaction_checkpoint;odh::erase_room_lighting_visual(*original_cohort.lighting,purchased_id);
        require(odh::encode_room_lighting_snapshot(*original_cohort.lighting)==original_lighting&&
            odh::encode_room_lighting_snapshot(*transaction_checkpoint.lighting_committed)==original_commit&&
            odh::encode_architecture_object_state(original_cohort)==original_state&&recalculations==0,
            "restore appends only missing visual identity; prior light/colors/registry/requests stay exact");
    }
    // A restored lot can acquire a new adult/child household after the
    // lighting owner is created, before its first capture and without Refresh.
    // The first saved state must already be canonical for a new process.
    {
        odh::World incoming_world;require(incoming_world.restore_snapshot(world.snapshot()),"incoming household source World");
        auto incoming=reopened;incoming.position_registry->deferred_lighting={1,0};
        unsigned recalculations=0;
        odh_host::SessionRoomLighting before_birth(incoming_world,[&]{return &incoming;},[&]{return &map;},
            []{return static_cast<odh::ObjectPositionRegistry*>(nullptr);},[&]()->odh::RoomLightingServices{
                ++recalculations;throw odh::FormatError("capture must not calculate room lighting");});
        std::vector<odh::ObjectPositionIdentity> new_members;
        for(std::int16_t id:{3,4}){
            require(incoming_world.create_saved_entity(id,odh::EntityKind::avatar,1000+id,{},{},{},{},{},0)&&
                incoming_world.set_position(id,{24+16*(id-3),40,1,0}),"actual late household construction and placement");
            odh::VmFrame actor;actor.context.caller=id;
            require(incoming_world.write(actor,3,29,0)&&incoming_world.write(actor,3,40,0)&&incoming_world.write(actor,3,51,0),
                "late actor has known actual room and visual classification fields");
            new_members.push_back({id,incoming_world.find(id)->generation});
            incoming.position_registry->rooms[1].entities.push_back(new_members.back());
            require(!before_birth.visual_room(id,new_members.back().generation),"late birth has not been captured or processed");
        }
        const auto before_capture=odh::encode_architecture_object_state(incoming);
        const auto committed=odh::encode_room_lighting_snapshot(*incoming.lighting_committed);
        before_birth.prepare_capture();
        for(const auto member:new_members)
            require(before_birth.visual_room(member.id,member.generation)==std::optional<std::uint16_t>{1},
                "first capture includes each late household visual identity");
        auto only_previous=incoming;
        for(const auto member:new_members)odh::erase_room_lighting_visual(*only_previous.lighting,member);
        require(odh::encode_architecture_object_state(only_previous)==before_capture&&
            odh::encode_room_lighting_snapshot(*incoming.lighting_committed)==committed&&
            incoming.position_registry->deferred_lighting==std::vector<std::uint16_t>{1,0}&&recalculations==0,
            "first capture only reconciles identity cache; colors, scores, registry and pending work remain exact");
        const auto first_save=odh::encode_architecture_object_state(incoming);
        auto restored=odh::decode_architecture_object_state(first_save);
        odh_host::SessionRoomLighting next_process(incoming_world,[&]{return &restored;},[&]{return &map;},
            []{return static_cast<odh::ObjectPositionRegistry*>(nullptr);},[&]()->odh::RoomLightingServices{
                ++recalculations;throw odh::FormatError("zero-tick restore must not calculate lighting");});
        next_process.prepare_capture();
        require(odh::encode_architecture_object_state(restored)==first_save&&recalculations==0,
            "late-household first save and fresh-owner zero-tick capture are byte identical");
    }
    odh::VmFrame old_frame;old_frame.context.caller=old_frame.context.callee=old_frame.context.stack_object=1;
    require(world.bind_context_live(old_frame.context),"bind original caller and stack object generations");
    require(world.remove(1)&&world.create_runtime_object(1,2002,{},{}),"reuse numeric id with a new entity");
    unsigned calls=0;odh_host::SessionRefreshServices hooks;
    hooks.lighting=hooks.area=[&](auto)->odh::PrimitiveOutcome{++calls;return {odh::PrimitiveDisposition::goto_true,{}};};
    const auto primitive=odh_host::session_refresh_primitive(world,&map,hooks);
    for(const auto selector:{0,1})for(const auto type:{1,2}){
        odh::Instruction instruction{};instruction.opcode=7;instruction.operands[0]=selector;instruction.operands[2]=type;
        require(primitive(old_frame,instruction,{},{}).disposition==odh::PrimitiveDisposition::unsupported&&calls==0,
            "bound stale Refresh cannot target replacement caller or stack object");
    }
    odh::VmFrame new_frame;new_frame.context.caller=new_frame.context.callee=new_frame.context.stack_object=1;
    require(world.bind_context_live(new_frame.context),"bind replacement identity explicitly");
    odh::Instruction instruction{};instruction.opcode=7;instruction.operands[2]=1;
    require(primitive(new_frame,instruction,{},{}).disposition==odh::PrimitiveDisposition::goto_true&&calls==1,
        "Refresh still accepts the explicitly bound current generation");
    std::cout<<"session lighting lifecycle matrix passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
