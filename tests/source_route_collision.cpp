// SPDX-License-Identifier: MPL-2.0
#include "odh/source_route_collision.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Policy=odh::SourceRouteCollision;
using Action=odh::SourceRouteCollisionAction;
using Phase=odh::SourceRouteCollisionPhase;
using Kind=odh::SourceRouteCollisionBlocker;
using Replan=odh::SourceRouteCollisionReplan;
using Input=odh::SourceRouteCollisionInput;
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
constexpr odh::SourceRouteCollisionIdentity caller{181,7},other{105,9},object{58,2};
Input hit(Kind kind){Input value;value.caller=caller;value.kind=kind;if(kind!=Kind::none)value.blocker=kind==Kind::avatar?other:object;return value;}
odh::SourceRouteCollisionSnapshot saved(const Policy& p){auto value=p.snapshot();require(value.has_value(),"policy safe point captures source counters");return *value;}
void object_retries(){
    Policy policy;auto input=hit(Kind::object);
    for(int n=0;n<4;++n){
        require(policy.begin_collision(input).action==Action::attempt_walk,"first four object collisions replan");
        require(!policy.snapshot(),"in-flight AttemptWalk cannot checkpoint");
        require(policy.finish_reroute(Replan::ready).action==Action::continue_replanned&&saved(policy).retries==4-n,
            "source object retry budget is not reset by a successful replan");
    }
    const auto failed=policy.begin_collision(input);
    require(failed.action==Action::soft_fail&&failed.failure_code==3&&failed.failure_blocker.id==181&&
        saved(policy).retries==0,"fifth object collision SoftFail NoPath names caller avatar, not obstacle");
    Policy no_path;require(no_path.begin_collision(input).action==Action::attempt_walk,"object begins reroute");
    const auto blocked=no_path.finish_reroute(Replan::no_path);
    require(blocked.action==Action::soft_fail&&blocked.failure_code==8&&blocked.failure_blocker.id==58,
        "failed object replan SoftFail DestTileOccupied retains collided object");
    Policy floor;require(floor.begin_collision(hit(Kind::none)).action==Action::attempt_walk,"null-blocker collision also replans");
    const auto no_object=floor.finish_reroute(Replan::no_path);
    require(no_object.failure_code==8&&no_object.failure_blocker.id==0&&no_object.failure_blocker.generation==0,
        "failed null-blocker replan preserves source null failure blocker");
}
void moving_avatar_wait(){
    Policy policy;auto input=hit(Kind::avatar);input.avatar_top_route=true;input.avatar_route_wait=0;
    require(policy.begin_collision(input).action==Action::wait_next_tick,"moving avatar waits rather than attempting to route around");
    require(saved(policy).wait_time==30&&saved(policy).retries==5&&saved(policy).considered.empty(),
        "moving avatar's first collision neither decrements retries nor adds consideration");
    int velocity=8;
    for(int tick=0;tick<30;++tick){
        const auto result=policy.tick_wait(velocity);velocity=result.velocity;
        require(result.action==Action::wait_next_tick&&result.wait_time==29-tick,
            "source always consumes the full thirty NEXT_TICK waits including final decrement to zero");
        if(tick==0)require(velocity==7&&result.animation_weights[0]==0.125f&&result.animation_weights[1]==0.57750004529953f,
            "first wait decelerates before publishing idle/full weights");
        if(tick==7)require(velocity==0&&result.animation_weights[0]==1&&result.animation_weights[1]==0&&result.animation_weights[2]==0,
            "wait reaches pure idle after eight ticks");
    }
    require(saved(policy).timeout==270&&policy.tick_wait(velocity).action==Action::resume_walking&&saved(policy).timeout==270,
        "tick after WaitTime becomes zero resumes without consuming timeout");
    auto state=saved(policy);state.considered.push_back(other);require(policy.restore(state),"restore inherited avatar consideration");
    require(policy.begin_collision(input).action==Action::wait_next_tick&&saved(policy).retries==4,
        "previously considered moving avatar decrements retry count once before waiting");
}
void stopped_avatar_and_shoo(){
    auto input=hit(Kind::avatar);input.avatar_top_route=false;
    Policy policy;require(policy.begin_collision(input).action==Action::attempt_walk,"stopped non-route avatar is considered and replanned");
    require(policy.finish_reroute(Replan::ready).action==Action::continue_replanned&&saved(policy).retries==4&&
        saved(policy).considered.size()==1,"first stopped avatar decrements retries once");
    require(policy.begin_collision(input).action==Action::attempt_walk&&policy.finish_reroute(Replan::ready).action==Action::continue_replanned&&
        saved(policy).retries==2,"already-considered stopped avatar decrements twice in source order");
    require(policy.begin_collision(input).action==Action::attempt_walk&&policy.finish_reroute(Replan::ready).action==Action::continue_replanned&&
        saved(policy).retries==0,"second non-route decrement reaching zero does NOT itself fail");
    const auto failed=policy.begin_collision(input);
    require(failed.action==Action::soft_fail&&failed.failure_code==3&&saved(policy).retries==-1,
        "next considered-avatar predecrement performs the actual exhaustion check");
    for(bool route:{false,true}){
        Policy p;input.avatar_top_route=route;input.avatar_route_wait=route?std::optional<std::int32_t>{4}:std::nullopt;
        require(p.begin_collision(input).action==Action::attempt_walk,"waiting route or stopped avatar replans");
        require(p.finish_reroute(Replan::no_path).action==Action::unsupported&&!p.snapshot(),
            "unknown CanShooAvatar never pretends clear, blocked, or failed route");
        require(p.finish_reroute(Replan::no_path,true).action==(route?Action::shoo_direct_route:Action::shoo_interaction),
            "exact TOP routing frame selects direct subroutine versus queued interaction");
        require(!p.snapshot()&&p.acknowledge_shoo(false).action==Action::unsupported,
            "unperformed shoo cannot become a successful wait/checkpoint");
        require(p.acknowledge_shoo(true).wait_time==60&&saved(p).considered.empty(),
            "successful source shoo removes consideration and interrupts for sixty ticks");
    }
    Policy cannot;input.avatar_top_route=false;input.avatar_route_wait.reset();
    require(cannot.begin_collision(input).action==Action::attempt_walk&&
        cannot.finish_reroute(Replan::no_path,false).wait_time==60&&saved(cannot).considered.size()==1,
        "already-shooed/queued avatar waits sixty while retaining consideration");
}
void timeout_and_restore(){
    Policy policy;auto input=hit(Kind::avatar);input.avatar_top_route=true;input.avatar_route_wait=0;
    require(policy.begin_collision(input).action==Action::wait_next_tick,"start moving-avatar wait");
    auto state=saved(policy);state.timeout=2;require(policy.restore(state),"restore source timeout near exhaustion");
    const auto first=policy.tick_wait(3);require(first.velocity==2&&first.action==Action::wait_next_tick,"first timeout tick still waits");
    const auto timeout=policy.tick_wait(2);
    require(timeout.action==Action::soft_fail&&timeout.failure_code==3&&timeout.failure_blocker.id==0&&timeout.velocity==1&&
        saved(policy).wait_time==28&&saved(policy).timeout==0,"source timeout SoftFail NoPath null keeps decremented wait/velocity");
    Policy fresh;require(fresh.restore(saved(policy))&&fresh.continue_after_soft_failure(),"fresh graph restores counter state; actual Routing chooses fallback");
    require(fresh.tick_wait(0).action==Action::soft_fail&&saved(fresh).timeout==-1,
        "successful fallback does not replenish source timeout");
    state=saved(policy);state.considered={other,other};require(!fresh.restore(state),"duplicate source HashSet member is malformed");
    state=saved(policy);state.phase=Phase::reroute_pending;require(!fresh.restore(state),"cannot restore private callback phase");
}
void unknown_and_shoo_facts(){
    Policy p;auto input=hit(Kind::avatar);
    require(p.begin_collision(input).action==Action::unsupported&&saved(p).retries==5,"unknown blocker Thread top cannot mutate retry state");
    input.avatar_top_route=true;require(p.begin_collision(input).action==Action::unsupported,"routing blocker wait state must be known");
    input.avatar_route_wait=0;input.blocker.generation=0;require(p.begin_collision(input).action==Action::unsupported,"unproven blocker identity cannot route around a fake absence");
    odh::SourceRouteShooFacts facts;
    require(!odh::source_can_shoo_avatar(facts),"unknown stack inventory is unsupported");
    facts.has_route=true;facts.oldest_route_shooed=true;
    require(odh::source_can_shoo_avatar(facts)==false,"source shooed route short-circuits unknown queue");
    facts.oldest_route_shooed=false;facts.parent_route_shooed=true;
    require(odh::source_can_shoo_avatar(facts)==false,"source parent shoo suppresses re-shoo");
    facts.parent_route_shooed=false;require(!odh::source_can_shoo_avatar(facts),"unknown queue cannot be assumed empty");
    facts.queued_destination_shoo=true;require(odh::source_can_shoo_avatar(facts)==false,"matching destination interaction3 suppresses shoo");
    facts.queued_destination_shoo=false;require(odh::source_can_shoo_avatar(facts)==true,"complete unshooed stack and queue permits shoo");
    facts.has_route=false;facts.oldest_route_shooed.reset();facts.parent_route_shooed.reset();
    require(odh::source_can_shoo_avatar(facts)==true,"known absence of routing frames skips route-state requirements");
}
}
int main(){try{object_retries();moving_avatar_wait();stopped_avatar_and_shoo();timeout_and_restore();unknown_and_shoo_facts();
    std::cout<<"source route collision checks: "<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
