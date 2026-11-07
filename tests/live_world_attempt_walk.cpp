#include "odh/live_world_attempt_walk.hpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
using State=odh::LiveAttemptWalkState;
using Ep=odh::LiveAttemptWalkEntryPoint5State;
struct Fixture {
    odh::World world;
    odh::DefinitionIndex definitions;
    odh::LiveWorldAttemptWalkRequest request;
    odh::LiveWorldAttemptWalkServices services;
    bool context=true;
    std::vector<std::pair<std::int16_t,std::int16_t>> calls;
    static odh::DefinitionIndex defs(){odh::ObjectDefinition d{};d.guid=100;d.tile_width=16;d.footprint_mask=0;
        return odh::DefinitionIndex({{"fixture","objects.iff",1,d}});}
    explicit Fixture(std::size_t history=4096):world([&]{odh::WorldLimits l;l.room_registration_events=history;return l;}()),definitions(defs()){
        for(std::int16_t id=1;id<=7;++id){
            const bool avatar=id==1||id==6;
            require(world.create(id,avatar?odh::EntityKind::avatar:odh::EntityKind::object,
                {1,avatar?88U:0U,1})&&world.define_type(id,100)&&world.declare_object_word(id,8,0)&&
                world.set_position(id,{id==7?96:16+id*4,24,1,0}),"create full World registration inventory");
            std::vector<odh::EntryPoint> entries(27);entries[5].action=4096;
            require(world.define_entry_points(id,entries),"define immutable source EP5 metadata after type");
        }
        require(world.declare_object_word(3,8,4)&&world.set_avatar_kill_timeout(6,0)&&
            world.place_in_slot(2,0,4),"create zero extent, retiring avatar and positioned-contained absent footprints");
        request.actor={1,world.find(1)->generation};request.room=1;request.level=1;request.room_bounds={0,0,4,4};
        request.architecture_complete=true;request.architectural_and_bounds={{-32,-32,96,-16},{-32,80,96,96},
            {-32,-32,-16,96},{80,-32,96,96}};
        request.avatars_to_consider={{6,world.find(6)->generation}};request.avatars_to_consider_complete=true;
        services.room_at=[](auto position)->std::optional<std::uint16_t>{return position.x<64?1:2;};
        services.context_still_matches=[&]{return context;};
        services.entry_point5=[&](const auto& call){calls.emplace_back(call.tested.id,call.other.id);
            return odh::LiveAttemptWalkEntryPoint5Result{Ep::returned_false,{}};};
    }
    auto domain(){return odh::build_live_world_attempt_walk_domain(world,definitions,request,services);}
    void flags(std::int16_t id,std::int16_t value){const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
        require(world.write(frame,3,8,value),"publish live flag effect");}
};
void complete_membership_and_sequential_flags(){
    Fixture f;
    f.services.entry_point5=[&](const auto& call){
        f.calls.emplace_back(call.tested.id,call.other.id);
        require(call.ghost==1&&call.arguments==std::array<std::int16_t,4>{call.other.id,1,0,0},"live domain preserves source ghost1 bilateral arguments");
        if(call.tested.id==1&&call.other.id==2)f.flags(5,16);
        return odh::LiveAttemptWalkEntryPoint5Result{Ep::returned_false,{}};
    };
    const auto domain=f.domain();
    require(domain.state==State::ready&&domain.request.entities.size()==6&&domain.request.entities[2].id==3&&
        domain.request.entities[3].id==4&&domain.request.entities[5].id==6,
        "RoomInfo inventory includes zero-extent, contained and retiring members with absent footprints");
    const auto before=f.world.spatial_revision();
    const auto result=odh::compose_live_attempt_walk_obstacles(f.world,domain.request,domain.services);
    require(result.state==State::ready&&result.entity_obstacle_ids==std::vector<std::int16_t>{2}&&
        f.calls==std::vector<std::pair<std::int16_t,std::int16_t>>{{1,2},{2,1}}&&f.world.spatial_revision()>before,
        "earlier EP5 flag writes remain live for later members without invalidating complete membership");
    auto tampered=domain.request;tampered.entities.pop_back();
    require(!domain.services.inventory_still_matches(tampered),"complete domain rejects a caller-trimmed obstacle-only entity subset");
}
void registration_events(){
    for(unsigned mode=0;mode<4;++mode){
        Fixture f;bool changed=false;
        f.services.entry_point5=[&](const auto& call){f.calls.emplace_back(call.tested.id,call.other.id);
            if(!changed){changed=true;
                if(mode==0)require(f.world.set_position(2,*f.world.find(2)->position),"same-room same-pose reinsertion");
                if(mode==1)require(f.world.set_position(7,{40,24,1,0})&&f.world.set_position(7,{96,24,1,0}),"outsider enters and leaves selected room");
                if(mode==2)require(f.world.set_position(7,{100,24,1,0})&&f.world.set_position(7,{96,24,1,0}),"unrelated-room remove/reinsert remains outside selected room");
                if(mode==3)require(f.world.remove(5)&&f.world.create(5,odh::EntityKind::object),"recycle selected member during source EP5");
            }
            return odh::LiveAttemptWalkEntryPoint5Result{Ep::returned_true,{}};
        };
        const auto domain=f.domain();require(domain.state==State::ready,"prepare complete room domain before live registration effects");
        const auto result=odh::compose_live_attempt_walk_obstacles(f.world,domain.request,domain.services);
        require(result.state==(mode==2?State::ready:State::unsupported),
            "selected-room foreach mutation rejects, outsider net-zero touch rejects, unrelated-room registration remains permitted");
        if(mode!=2)require(result.obstacles.empty()&&result.entry_point5_calls==1,"invalidated room membership publishes no partial path inputs and preserves prior effects");
    }
}
void unknown_and_history_gap(){
    Fixture gap(2);bool changed=false;
    gap.services.entry_point5=[&](const auto&){if(!changed){changed=true;
        require(gap.world.set_position(7,{100,24,1,0})&&gap.world.set_position(7,{104,24,1,0})&&
            gap.world.set_position(7,{96,24,1,0}),"overflow bounded registration history during one EP5");}
        return odh::LiveAttemptWalkEntryPoint5Result{Ep::returned_true,{}};};
    const auto domain=gap.domain();require(domain.state==State::ready,"capture current registration watermark despite earlier ring eviction");
    require(odh::compose_live_attempt_walk_obstacles(gap.world,domain.request,domain.services).state==State::unsupported,
        "history gaps do not pretend unrelated-room updates were proven safe");
    Fixture unknown;unknown.services.room_at=[](auto)->std::optional<std::uint16_t>{return {};};
    require(unknown.domain().state==State::unsupported,"unknown room of any positioned entity cannot yield a complete room subset");
    Fixture hidden;require(hidden.world.set_ui_ghost(5,true),"exclude actual UI-only ghost from source RoomInfo inventory");
    const auto filtered=hidden.domain();require(filtered.state==State::ready&&filtered.request.entities.size()==5,
        "source ghost images are excluded while normal absent-footprint members remain");
    Fixture normalized;require(normalized.world.detach(2,0)&&normalized.world.attach(2,0,4),"make normalized legacy attachment without propagation bit");
    require(normalized.domain().state==State::ready,"legacy positioned=false does not exclude a source-normalized contained member");
    require(normalized.world.detach(2,0)&&normalized.world.set_position(4,{52,24,1,0})&&normalized.world.attach(2,0,4),"make genuinely ambiguous legacy containment position");
    require(normalized.domain().state==State::unsupported,"unreconstructed contained registration position is explicit unknown, never an empty obstacle subset");
    Fixture unplaced;require(unplaced.world.detach(2,0)&&unplaced.world.create(8,odh::EntityKind::object)&&
        unplaced.world.attach(2,0,8),"create legacy OOW child of a positioned container");
    require(unplaced.domain().state==State::unsupported,
        "legacy absent child position cannot silently omit its possible contained RoomInfo membership");
    Fixture stale;auto current=stale.domain();stale.context=false;
    require(!current.services.inventory_still_matches(current.request),"expired source execution context invalidates live domain before callbacks");
}
}
int main(){try{complete_membership_and_sequential_flags();registration_events();unknown_and_history_gap();
    std::cout<<checks<<" live World AttemptWalk checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
