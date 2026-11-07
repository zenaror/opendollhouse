#include "odh/live_attempt_walk.hpp"
#include "odh/random.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
using Identity=odh::LiveAttemptWalkIdentity;
using EpState=odh::LiveAttemptWalkEntryPoint5State;
using State=odh::LiveAttemptWalkState;
struct Fixture {
    odh::World world;
    odh::LiveAttemptWalkRequest request;
    odh::LiveAttemptWalkServices services;
    std::map<std::int16_t,odh::LiveAttemptWalkFootprint> footprints;
    std::vector<odh::LiveAttemptWalkEntryPoint5Request> calls;
    bool membership=true;
    explicit Fixture(bool native_entries=false){
        for(std::int16_t id=1;id<=4;++id){
            const bool avatar=id==1||id==4;
            require(world.create(id,avatar?odh::EntityKind::avatar:odh::EntityKind::object,
                {0,avatar?88U:0U,1,0})&&world.set_position(id,{id*8,16,1,0})&&
                world.define_type(id,0x10000000U+id)&&world.declare_object_word(id,8,0),"create known live RoomInfo member");
            std::vector<odh::EntryPoint> entries(27);entries[5].action=native_entries?0:4096;
            require(world.define_entry_points(id,entries),"define current member EP5 action");
            request.entities.push_back(identity(id));
            footprints[id]={odh::LiveAttemptWalkFootprintState::present,odh::RectRouteObstacle{id*8,16,id*8+2,18}};
        }
        require(world.set_radian_direction(1,0.37f),"set non-NORTH caller angle");
        request.actor=identity(1);request.room=2;request.level=1;request.room_bounds={0,0,4,4};
        request.architectural_and_bounds={{1,2,3,4},{-32,-32,96,-16},{-32,80,96,96},
            {-32,-32,-16,96},{80,-32,96,96}};
        request.architectural_count=1;request.architecture_complete=true;request.entities_complete=true;
        request.avatars_to_consider={identity(4)};request.avatars_to_consider_complete=true;
        services.inventory_still_matches=[this](const auto&){return membership;};
        services.footprint=[this](Identity id){return footprints.at(id.id);};
        services.entry_point5=[this](const auto& call){calls.push_back(call);return odh::LiveAttemptWalkEntryPoint5Result{EpState::returned_false,{}};};
    }
    Identity identity(std::int16_t id)const{return {id,world.find(id)->generation};}
    void set_flags(std::int16_t id,std::int16_t value){const odh::VmFrame frame{odh::routine_key(4096),0,{id,id,id,0},{},{}};
        require(world.write(frame,3,8,value),"write live Flags8");}
    auto run(const odh::LiveAttemptWalkLimits& limits={}){return odh::compose_live_attempt_walk_obstacles(world,request,services,limits);}
};
void ordered_and_cached(){
    Fixture f;
    const auto before=f.world.find(1)->radian_direction;
    f.services.entry_point5=[&](const auto& call){
        f.calls.push_back(call);
        require(call.ghost==1&&call.arguments==std::array<std::int16_t,4>{call.other.id,1,0,0}&&
            f.world.find(1)->radian_direction==before,"AttemptWalk uses ghost1 arguments without temporary NORTH heading");
        if(call.tested.id==1&&call.other.id==2){
            f.set_flags(3,16); // Must affect the NEXT entity's eligibility.
            f.set_flags(2,16); // Must NOT replace this entity's cached flags.
            f.footprints[2].rectangle=odh::RectRouteObstacle{100,100,110,110};
        }
        return odh::LiveAttemptWalkEntryPoint5Result{EpState::returned_false,{}};
    };
    const auto result=f.run();
    require(result.state==State::ready&&result.entity_obstacle_ids==std::vector<std::int16_t>{2,4}&&
        result.obstacles.size()==7&&result.obstacles[5].x1==13&&result.obstacles[5].x2==21,
        "sequential flags affect next entity while current footprint and flags remain cached through EP5");
    require(f.calls.size()==4&&f.calls[0].tested.id==1&&f.calls[0].other.id==2&&f.calls[1].tested.id==2&&
        f.calls[2].tested.id==1&&f.calls[2].other.id==4&&f.calls[2].action==4096&&f.calls[3].tested.id==4,
        "source sorted entity order, caller-first calls and live caller EP5 metadata are preserved");
    require(result.obstacles.front().x1==1&&result.obstacles[1].x1==-32&&
        f.world.find(1)->radian_direction==before,"architectural prefix and source bounds retain insertion order");
}
void source_short_circuits(){
    Fixture f;f.set_flags(1,16); // Cached caller policy skips all avatars.
    f.footprints[3]={odh::LiveAttemptWalkFootprintState::absent,{}};
    f.services.entry_point5=[&](const auto& call){f.calls.push_back(call);f.set_flags(1,0);
        return odh::LiveAttemptWalkEntryPoint5Result{EpState::returned_true,{}};};
    const auto result=f.run();
    require(result.state==State::ready&&result.entity_obstacle_ids.empty()&&f.calls.size()==1&&
        f.calls.front().tested.id==1&&f.calls.front().other.id==2,
        "caller true skips blocker EP5, absent footprints skip checks, and initial avatar policy remains cached");
    Fixture flags;flags.set_flags(2,16|4);flags.set_flags(3,16|2);
    flags.request.avatars_to_consider.clear();
    const auto blocked=flags.run();
    require(blocked.state==State::ready&&blocked.entity_obstacle_ids==std::vector<std::int16_t>{3}&&flags.calls.size()==2,
        "Flags16|2 blocks despite AllowPersonIntersection; Flags16|4 HasZeroExtent does not override allowance; unconsidered avatar skips");
    Fixture native(true);
    native.services.entry_point5={};
    const auto no_bhav=native.run();
    require(no_bhav.state==State::ready&&no_bhav.entry_point5_calls==0&&no_bhav.entity_obstacle_ids.size()==3,
        "known native/no-BHAV entries are source false without requiring a callback");
}
void failure_preserves_effects(){
    for(unsigned mode=0;mode<5;++mode){
        Fixture f;
        odh::Vm register_owner(odh::RoutineSet{});odh::RandomState random(0x12345678);
        const auto seed=random.seed();
        f.services.entry_point5=[&](const auto& call){f.calls.push_back(call);f.set_flags(3,16);
            register_owner.temps()[0]=73;(void)random.next(1024);
            if(mode==0)return odh::LiveAttemptWalkEntryPoint5Result{EpState::unsupported,"unported check"};
            if(mode==1)return odh::LiveAttemptWalkEntryPoint5Result{EpState::budget_exhausted,"check budget"};
            if(mode==2){require(f.world.remove(3)&&f.world.create(3,odh::EntityKind::object),"recycle later inventory member");}
            if(mode==3)f.membership=false;
            if(mode==4)throw std::runtime_error("live check exception");
            return odh::LiveAttemptWalkEntryPoint5Result{EpState::returned_true,{}};
        };
        const auto result=f.run();
        require(result.state==(mode==1?State::budget_exhausted:State::unsupported)&&
            result.obstacles.empty()&&result.entity_obstacle_ids.empty()&&result.entry_point5_calls==1,
            "unknown, budget, recycled membership, source collection mutation and exceptions expose no partial obstacles");
        require(register_owner.temps()[0]==73&&random.seed()!=seed,
            "actual callback register and shared RNG effects survive unsupported composition without rollback or cloning");
        if(mode!=2){const odh::VmFrame frame{odh::routine_key(4096),0,{3,3,3,0},{},{}};
            require(f.world.read(frame,3,8)==16,"effects preceding live failure remain in World");}
    }
    Fixture own;
    own.services.entry_point5=[&](const auto&){require(own.world.remove(1)&&own.world.create(1,odh::EntityKind::avatar),"recycle caller during EP5");
        return odh::LiveAttemptWalkEntryPoint5Result{EpState::returned_true,{}};};
    require(own.run().state==State::unsupported,"caller recycling cannot publish obstacles for stale identity");
}
void admission_and_budgets(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;
        if(mode==0)f.request.entities_complete=false;
        if(mode==1)std::swap(f.request.entities[1],f.request.entities[2]);
        if(mode==2)f.request.architectural_and_bounds[1].x1++;
        if(mode==3)f.request.avatars_to_consider_complete=false;
        if(mode==4)f.services.inventory_still_matches={};
        require(f.run().state==State::unsupported&&f.calls.empty(),"unknown inventory, order, bounds or proof rejects before live effects");
    }
    Fixture limited;odh::LiveAttemptWalkLimits limits;limits.entry_point5_calls=1;
    const auto result=limited.run(limits);
    require(result.state==State::budget_exhausted&&result.entry_point5_calls==1&&limited.calls.size()==1&&result.obstacles.empty(),
        "call budget refuses blocker callback and clears partial geometry after caller false");
    Fixture missing;missing.services.entry_point5={};
    require(missing.run().state==State::unsupported&&missing.calls.empty(),
        "missing live BHAV capability is unsupported rather than ordinary source false");
    Fixture overflow;overflow.footprints[2].rectangle=odh::RectRouteObstacle{INT32_MIN,0,4,4};
    require(overflow.run().state==State::unsupported,"footprint inflation must not overflow signed source coordinates");
}
}
int main(){try{ordered_and_cached();source_short_circuits();failure_preserves_effects();admission_and_budgets();
    std::cout<<checks<<" live AttemptWalk checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
