#include "odh/source_walk_plan.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Fixture {
    odh::World world;
    odh::SourceWalkPlanRequest request;
    odh::SourceWalkPlanServices services;
    odh::SourceWalkDomain domain;
    unsigned room_calls=0,domain_calls=0;
    bool current=true;
    std::optional<std::uint16_t> room=1;
    explicit Fixture(bool effectful=false){
        require(world.create(1,odh::EntityKind::avatar,{0,88,0,0})&&world.set_position(1,{24,24,1,0})&&
            world.declare_object_word(1,8,0),"create actor");
        require(world.define_type(1,0x10000001),"define actor metadata identity");
        std::vector<odh::EntryPoint> entries(27);entries[5].action=effectful?4096:0;
        require(world.define_entry_points(1,entries),"known source entry metadata");
        request.actor={1,world.find(1)->generation};request.destination={56,24};
        domain.state=odh::LiveAttemptWalkState::ready;
        auto& r=domain.request;r.actor=request.actor;r.room=1;r.level=1;r.room_bounds={0,0,64,64};
        r.architecture_complete=true;r.entities_complete=true;r.avatars_to_consider_complete=true;
        r.entities={request.actor};
        r.architectural_and_bounds={{-32,-32,1056,-16},{-32,1040,1056,1056},
            {-32,-32,-16,1056},{1040,-32,1056,1056}};
        domain.services.inventory_still_matches=[this](const auto&){return current;};
        domain.services.footprint=[](const auto&){return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};};
        services.context_still_matches=[this]{return current;};
        services.room_at=[this](auto){++room_calls;return room;};
        services.domain=[this](auto){++domain_calls;return domain;};
    }
    auto run(const odh::SourceWalkPlanLimits& limits={}){return odh::plan_source_walk(world,request,services,limits);}
};
void lazy_source_order(){
    Fixture f;f.request.destination={24,24};f.room.reset();f.services.domain={};
    require(f.run().state==odh::SourceWalkPlanState::turn_only&&f.room_calls==0,
        "same XY turns before unknown room or missing domain work");
    f.request.destination={56,24};f.room=0;
    require(f.run().state==odh::SourceWalkPlanState::no_path&&f.domain_calls==0,
        "known room zero fails before domain provider");
    f.room.reset();require(f.run().state==odh::SourceWalkPlanState::unsupported,"unknown room is not no path");
    f.current=false;f.request.destination={24,24};
    require(f.run().state==odh::SourceWalkPlanState::unsupported,"turn-only does not bypass source context");
}
void complete_room_curve(){
    Fixture f;f.request.destination={900,24};
    const auto before=*f.world.find(1)->position;
    const auto result=f.run();
    require(result.state==odh::SourceWalkPlanState::ready&&!result.segments.empty()&&result.obstacles==4,
        "complete-room route reaches beyond former actor-centered 256-unit window");
    require(result.segments.front().kind==odh::WalkSegmentKind::cubic&&
        result.segments.front().a.x==24*odh::walk_path_hip&&result.segments.back().d.x==900*odh::walk_path_hip,
        "pipeline preserves HiP cubic endpoints");
    require(*f.world.find(1)->position==before,"planning never moves World");
    f.request.bezier=false;
    const auto line=f.run();
    require(line.state==odh::SourceWalkPlanState::ready&&line.segments.front().kind==odh::WalkSegmentKind::line,
        "explicit source feature setting selects line generation");
    f.domain.request.architectural_and_bounds.insert(f.domain.request.architectural_and_bounds.begin(),{400,-16,410,1040});
    f.domain.request.architectural_count=1;
    require(f.run().state==odh::SourceWalkPlanState::no_path,"complete wall splitting padded room yields authored no path");
}
void incomplete_and_budget(){
    Fixture f;f.domain.request.architecture_complete=false;
    require(f.run().state==odh::SourceWalkPlanState::unsupported,"incomplete architecture is not authored false");
    f.domain.request.architecture_complete=true;f.domain.request.entities_complete=false;
    require(f.run().state==odh::SourceWalkPlanState::unsupported,"unknown membership is not a free path");
    f.domain.request.entities_complete=true;odh::SourceWalkPlanLimits limits;limits.router.obstacles=1;
    const auto budget=f.run(limits);
    require(budget.state==odh::SourceWalkPlanState::budget_exhausted&&budget.segments.empty(),"router limit exposes no partial path");
    limits={};limits.path.work_units=0;
    const auto segments=f.run(limits);
    require(segments.state==odh::SourceWalkPlanState::budget_exhausted&&segments.segments.empty()&&
        segments.diagnostic.find("budget_exhausted(3) start=24,24 destination=56,24")!=std::string::npos&&
        segments.diagnostic.find("bezier=1 incoming=none outgoing=none route=")!=std::string::npos&&
        segments.diagnostic.find("hip=")!=std::string::npos,
        "failed generator reports bounded reproducible source geometry without changing failure semantics");
    f.domain.state=odh::LiveAttemptWalkState::budget_exhausted;
    require(f.run().state==odh::SourceWalkPlanState::budget_exhausted,"domain work limit remains explicit");
    f.domain.state=odh::LiveAttemptWalkState::ready;f.domain.request.actor.generation++;
    require(f.run().state==odh::SourceWalkPlanState::unsupported,"domain belongs to exact actor generation");
    f.domain.request.actor=f.request.actor;
    f.services.domain=[&](auto){f.current=false;return f.domain;};
    require(f.run().state==odh::SourceWalkPlanState::unsupported,"callback scope mutation rejects before planning");
}
void current_continuous_heading_and_effects(){
    Fixture f(true);
    require(f.world.set_radian_direction(1,0.9f),"set between-notch angle");
    require(f.run().initial_heading==1,"quarter heading derives from continuous angle, not saved notch division");
    require(f.world.set_radian_direction(1,-0.9f),"set negative angle");
    require(f.run().initial_heading==3,"source quarter heading uses positive modulo");
    require(f.world.create(2,odh::EntityKind::object)&&f.world.set_position(2,{40,24,1,0})&&
        f.world.declare_object_word(2,8,0)&&f.world.define_type(2,0x10000002),"create live obstacle");
    std::vector<odh::EntryPoint> entries(27);entries[5].action=4096;
    require(f.world.define_entry_points(2,entries),"install effectful blocker check");
    f.domain.request.entities.push_back({2,f.world.find(2)->generation});
    f.domain.services.footprint=[](auto id){return id.id==1?
        odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}}:
        odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::present,odh::RectRouteObstacle{39,23,41,25}};};
    unsigned calls=0;
    f.domain.services.entry_point5=[&](const auto&){++calls;
        const odh::VmFrame frame{odh::routine_key(4096),0,{2,2,2,0},{},{}};
        require(f.world.write(frame,3,8,16),"effectful check writes actual World flags");
        return odh::LiveAttemptWalkEntryPoint5Result{odh::LiveAttemptWalkEntryPoint5State::returned_true,{}};
    };
    const auto result=f.run();
    const odh::VmFrame frame{odh::routine_key(4096),0,{2,2,2,0},{},{}};
    require(result.state==odh::SourceWalkPlanState::ready&&result.entry_point5_calls==1&&calls==1&&
        f.world.read(frame,3,8)==16,"live EP5 effects survive planning and source short circuit avoids blocker check");
}
}
int main(){try{lazy_source_order();complete_room_curve();incomplete_and_budget();current_continuous_heading_and_effects();
    std::cout<<checks<<" source walk plan checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
