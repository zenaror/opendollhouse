// SPDX-License-Identifier: MPL-2.0
#include "odh/walk_motion.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
using Status=odh::WalkMotionStatus;
using Phase=odh::WalkMotionPhase;
using Kind=odh::WalkSegmentKind;
constexpr auto h=odh::walk_path_hip;
constexpr double pi=3.141592653589793238462643383279502884;
std::uint32_t bits(float x){std::uint32_t value;std::memcpy(&value,&x,sizeof(value));return value;}
bool same(odh::WalkVector a,odh::WalkVector b){return bits(a.x)==bits(b.x)&&bits(a.y)==bits(b.y);}
odh::WalkSegmentGeometry line(int x,int y,int from_x=0,int from_y=0){return {Kind::line,{from_x*h,from_y*h},{},{},{x*h,y*h}};}
odh::WalkMotionSnapshot saved(const odh::WalkMotion& motion){const auto value=motion.snapshot();require(value.status==Status::success&&value.snapshot.has_value(),"capture source motion safe point");return *value.snapshot;}
odh::RouteFrameContext walking(){odh::RouteFrameContext value;value.state=odh::RouteFrameState::walking;return value;}
void durations(){
    const auto geometry=line(3,4);
    const std::vector<odh::WalkMotionProfile> profiles={{false,0,false,0},{true,253,false,0},
        {true,254,false,0},{false,0,true,1},{false,0,false,1},{true,253,false,1}};
    const std::array<int,6> expected={50,100,50,100,16,33};
    for(std::size_t n=0;n<profiles.size();++n){
        odh::WalkMotion motion;require(motion.initialize({geometry},{{0,0},2,0},profiles[n])==Status::success,"initialize source duration profile");
        const auto state=saved(motion);
        require(state.move_total_frames==expected[n]&&state.move_frames==0&&state.turn_frames==10&&
            state.walk_direction==0&&std::abs(state.target_direction-std::atan2(3.0,-4.0))<1e-15,
            "AdvanceWaypoint applies pet threshold, pool WalkStyle0, and truncating run divisor in source order");
    }
    odh::WalkMotion already;
    require(already.initialize({geometry},{{0,0},1,0.37f},{},true,8)==Status::success&&
        saved(already).target_direction==static_cast<double>(0.37f),"walking waypoint preserves actor heading rather than aiming at its endpoint");
    odh::WalkMotion small;
    require(small.initialize({line(0,-1)},{{0,0},1,0},{false,0,false,1})==Status::success&&saved(small).move_total_frames==3&&saved(small).turn_frames==3,
        "small running segment truncates 10/3 before updating frame and turn durations");
}
void source_ramp_and_tail(){
    odh::WalkMotion motion;auto frame=walking();
    require(motion.initialize({line(0,-10)},{{0,0},1,0},{})==Status::success,"initialize ten-unit north walk");
    const std::array<int,18> velocities={1,2,3,4,5,6,7,8,8,8,8,8,8,7,6,5,4,3};
    const std::array<int,18> frames={1,3,6,10,15,21,28,36,44,52,60,68,76,83,89,94,98,100};
    odh::WalkMotionPose actor{{0,0},1,0};
    for(std::size_t tick=0;tick<velocities.size();++tick){
        const auto prepared=motion.prepare_step(frame);
        require(prepared.status==Status::success&&prepared.step&&motion.can_commit(*prepared.step),"source tick yields an authenticated placement sample");
        const auto& step=prepared.step->data();
        require(step.velocity==velocities[tick]&&step.move_frames==frames[tick]&&step.level==1&&
            step.portal_turn_queried==(tick>=13),"source acceleration and last-segment remaining<=28 tail deceleration match fixed tick sequence");
        require(bits(step.animation_weights[0])==bits(static_cast<float>(8-step.velocity)/8.0f)&&
            bits(step.animation_weights[1])==bits((static_cast<float>(step.velocity)/8.0f)*0.66f)&&bits(step.animation_weights[2])==bits((static_cast<float>(step.velocity)/8.0f)*0.33f),
            "three animation weights use pre-feedback source movement velocity");
        actor.position=step.sample.position;
        const auto commit=motion.commit_step(*prepared.step);
        require(commit.effects&&commit.status==(tick+1==velocities.size()?Status::waypoint_required:Status::success)&&
            commit.effects->radian_direction==0&&commit.effects->turn_velocity==0&&commit.effects->visual_z==0,
            "accepted north movement publishes source heading and exact terminal phase");
        actor.radians=commit.effects->radian_direction;
        require(!motion.can_commit(*prepared.step)&&motion.commit_step(*prepared.step).status==Status::invalid_input,"placement token cannot be committed twice");
    }
    require(actor.position.x==0&&actor.position.y==-10&&motion.snapshot().status==Status::invalid_input,
        "source terminal point is exact and the same-tick waypoint gap is not a checkpoint");
    require(motion.advance_waypoint(actor,{})==Status::finished&&motion.phase()==Phase::finished&&saved(motion).move_total_frames==-1,
        "empty WalkTo advances to source MoveTotalFrames=-1 for external EndWalk");
    odh::WalkMotion resumed;
    require(resumed.restore(saved(motion))==Status::success&&resumed.prepare_step(frame).status==Status::finished,
        "finished source motion restores without sampling its reset current-segment cache");
}
void heading_feedback(){
    auto frame=walking();odh::WalkMotion east;
    require(east.initialize({line(100,0)},{{0,0},2,0},{},true,8)==Status::success,"initialize source east path with north-facing walking entry");
    const auto p=east.prepare_step(frame);require(p.step&&p.step->data().velocity==8,"first east step uses pre-feedback velocity8");
    const auto c=east.commit_step(*p.step);const auto state=saved(east);
    require(c.status==Status::success&&c.effects&&c.effects->radian_direction==0&&c.effects->turn_velocity==-0.25&&
        c.effects->visual_z==2.95f&&bits(c.effects->velocity.x)==bits(0.05f)&&c.effects->velocity.y==0&&
        state.velocity==4&&state.target_direction==0.25&&state.turn_frames==10,
        "source pi/2 feedback caps turn, reduces stored speed, keeps old heading and pre-braking visual velocity");
    const auto p2=east.prepare_step(frame);require(p2.step&&p2.step->data().velocity==5&&p2.step->data().move_frames==13,"next tick accelerates from the feedback-reduced velocity");
    const auto c2=east.commit_step(*p2.step);
    require(c2.effects&&c2.effects->radian_direction==0.25f&&bits(c2.effects->velocity.x)==bits(0.03125f)&&
        saved(east).target_direction==0.5&&saved(east).velocity==1,
        "next heading follows prior TargetDirection while velocity feedback affects only subsequent ticks");
    odh::WalkMotion south;
    require(south.initialize({line(0,100)},{{0,0},1,0},{},true,8)==Status::success,"initialize reversed source direction");
    const auto reverse=south.prepare_step(frame);require(reverse.step.has_value(),"sample reversed source direction");
    const auto reversed=south.commit_step(*reverse.step);
    require(reversed.effects&&reversed.effects->turn_velocity==0.25&&saved(south).velocity==0&&saved(south).target_direction==-0.25,
        "source PosMod chooses positive pi and >pi/2 feedback stops the stored walking speed");
}
void portal_turn_short_circuit(){
    auto frame=walking();odh::RouteFrameContext parent;parent.state=odh::RouteFrameState::room_portal;
    odh::WalkMotion motion;
    require(motion.initialize({line(0,-1)},{{0,0},1,0},{},false,8)==Status::success,"initialize short portal continuation");
    const auto first=motion.prepare_step(frame,&parent);
    require(first.step&&first.step->data().portal_turn_queried&&first.step->data().velocity==7&&parent.portal_turns==1,
        "first tail query consumes the parent ROOM_PORTAL turn and decelerates");
    require(motion.commit_step(*first.step).status==Status::success,"commit first short portal step");
    const auto second=motion.prepare_step(frame,&parent);
    require(second.step&&second.step->data().velocity==8&&parent.portal_turns==2,
        "later portal tail query still increments counter but denial follows the acceleration branch");
    require(motion.commit_step(*second.step).status==Status::waypoint_required,"short portal motion reaches its terminal frame");
    odh::WalkMotion not_last;parent.portal_turns=0;
    require(not_last.initialize({line(0,-1),line(0,-2,0,-1)},{{0,0},1,0},{})==Status::success,"initialize multiple source segments");
    const auto next=not_last.prepare_step(frame,&parent);
    require(next.step&&!next.step->data().portal_turn_queried&&parent.portal_turns==0,"CanPortalTurn is not called for a short segment when another segment remains");
    odh::WalkMotion saturated;parent.portal_turns=UINT32_MAX;
    require(saturated.initialize({line(0,-1)},{{0,0},1,0},{})==Status::success&&
        saturated.prepare_step(frame,&parent).status==Status::unsupported_arithmetic&&parent.portal_turns==UINT32_MAX&&saved(saturated).move_frames==0,
        "portal turn counter exhaustion stays explicit without mutating motion or parent");
}
void tokens_and_collision(){
    odh::WalkMotion first,other;auto frame=walking();
    require(first.initialize({line(0,-10)},{{0,0},1,0},{})==Status::success&&other.initialize({line(0,-10)},{{0,0},1,0},{})==Status::success,"initialize separate motion owners");
    const auto p=first.prepare_step(frame),q=other.prepare_step(frame);
    require(p.step&&q.step&&!first.can_commit(*q.step)&&!other.can_commit(*p.step),"foreign motion step cannot borrow another sampler's lease");
    auto modified=*p.step;
    // Deliberately corrupt a non-const token copy through its read-only view.
    // Public callers have no mutation API; the exact payload check also seals
    // this malformed-copy path before a host uses it for World placement.
    const_cast<odh::WalkMotionStepData&>(modified.data()).sample.position.x=123;
    require(!first.can_commit(modified)&&first.commit_step(modified).status==Status::invalid_input&&first.can_commit(*p.step),
        "altered sample payload is rejected without consuming the legitimate pending step");
    require(first.snapshot().status==Status::invalid_input&&first.prepare_step(frame).status==Status::invalid_input&&
        first.initialize({},{{0,0},1,0},{})==Status::invalid_input,"pending placement cannot snapshot, reenter preparation or replace the motion");
    require(first.reject_step(*p.step)==Status::collision_required&&!first.can_commit(*p.step)&&
        first.prepare_step(frame).status==Status::collision_required,"placement failure resets sampler and requires an external collision decision");
    const auto rejected=saved(first);
    require(rejected.move_frames==1&&rejected.velocity==1&&rejected.phase==Phase::collision_required,
        "source frame and speed changes survive placement failure before collision handling");
    odh::WalkMotion blocked;
    require(blocked.restore(rejected)==Status::success&&blocked.prepare_step(frame).status==Status::collision_required,
        "collision-paused component state restores without pretending wait/shoo or authored failure is implemented");
    require(blocked.set_collision_velocity(-1)==Status::invalid_input&&
        blocked.set_collision_velocity(0)==Status::success&&saved(blocked).move_frames==1&&
        blocked.resume_after_collision()==Status::success&&blocked.resume_after_collision()==Status::invalid_input,
        "source wait deceleration preserves rejected frame and resumes exactly once after external collision handling");
    const auto resumed=blocked.prepare_step(frame);
    require(resumed.step&&resumed.step->data().move_frames==2&&resumed.step->data().velocity==1&&
        blocked.set_collision_velocity(0)==Status::invalid_input&&!blocked.can_commit(*p.step)&&
        blocked.commit_step(*resumed.step).status==Status::success,
        "resumed source walk accelerates from waited velocity and advances rejected counters rather than committing the stale sample");
    const auto stale=*p.step;
    require(first.initialize({line(0,-10)},{{0,0},1,0},{})==Status::success&&!first.can_commit(stale),"replanned motion cannot revive a retained old token");
    const auto fresh=first.prepare_step(frame);
    require(fresh.step&&first.can_commit(*fresh.step)&&!first.can_commit(stale)&&first.reject_step(*fresh.step)==Status::collision_required,
        "a newly pending step on the same owner cannot revive a copied stale placement lease");
    require(other.reject_step(*q.step)==Status::collision_required,"independent owner's pending sample remains intact");
}
void segment_advance_and_restore(){
    odh::WalkMotion motion;auto frame=walking();
    require(motion.initialize({line(0,-1),line(10,-1,0,-1)},{{0,0},1,0},{})==Status::success,"initialize segment queue");
    odh::WalkMotionPose actor{{0,0},1,0};
    for(unsigned tick=0;tick<4;++tick){const auto p=motion.prepare_step(frame);require(p.step.has_value(),"sample first queued segment");actor.position=p.step->data().sample.position;
        const auto c=motion.commit_step(*p.step);require(c.effects.has_value(),"commit first queued segment");actor.radians=c.effects->radian_direction;}
    require(motion.phase()==Phase::waypoint_required&&motion.advance_waypoint(actor,{false,0,true,1})==Status::success,
        "next waypoint uses fresh pool/profile state in the same source tick");
    const auto advanced=saved(motion);
    require(advanced.previous_position.position.y==-1&&advanced.move_frames==0&&advanced.move_total_frames==200&&
        advanced.velocity==4&&advanced.target_direction==actor.radians,
        "AdvanceWaypoint resets counters/duration while preserving speed and current walking heading");
    const odh::WalkSegmentGeometry curve{Kind::cubic,{0,0},{0,0},{3*h,0},{3*h,0}};
    odh::WalkMotion live;
    require(live.initialize({curve},{{0,0},1,static_cast<float>(pi/2)},{})==Status::success,"initialize cubic motion continuation");
    for(unsigned n=0;n<5;++n){const auto p=live.prepare_step(frame);require(p.step.has_value(),"sample cubic before snapshot");require(live.commit_step(*p.step).status==Status::success,"commit cubic before snapshot");}
    const auto checkpoint=saved(live);odh::WalkMotion resumed;
    require(resumed.restore(checkpoint)==Status::success,"restore geometry/counters into a fresh motion owner");
    for(unsigned n=0;n<64&&live.phase()==Phase::walking;++n){
        const auto a=live.prepare_step(frame),b=resumed.prepare_step(frame);
        require(a.step&&b.step&&a.step->data().move_frames==b.step->data().move_frames&&a.step->data().sample.position.x==b.step->data().sample.position.x,
            "restored source cubic reaches the same physical point/frame");
        const auto x=live.commit_step(*a.step),y=resumed.commit_step(*b.step);
        require(x.status==y.status&&x.effects&&y.effects&&same(x.effects->visual,y.effects->visual)&&same(x.effects->velocity,y.effects->velocity)&&
            bits(x.effects->radian_direction)==bits(y.effects->radian_direction)&&x.effects->turn_velocity==y.effects->turn_velocity,
            "fresh derived sampler cache preserves nonterminal source visual/velocity/heading feedback");
    }
    require(live.phase()==Phase::waypoint_required&&resumed.phase()==Phase::waypoint_required,"both source continuations finish together");
}
void limits_and_malformed(){
    odh::WalkMotion zero({0}),raised({4097}),small({1});
    require(zero.initialize({},{{0,0},1,0},{})==Status::invalid_input&&raised.initialize({line(0,-1)},{{0,0},1,0},{})==Status::invalid_input,
        "callers cannot disable or silently raise the supported1..4096 segment cap");
    require(small.initialize({line(0,-1),line(0,-2,0,-1)},{{0,0},1,0},{})==Status::budget_exhausted,"lowered segment budget refuses the whole path");
    odh::WalkMotion motion;auto frame=walking();
    require(motion.initialize({line(0,-1)},{{0,0},1,0},{})==Status::success,"initialize malformed snapshot baseline");
    const auto baseline=saved(motion);auto bad=baseline;bad.phase=static_cast<Phase>(99);
    require(motion.restore(bad)==Status::invalid_input&&saved(motion).move_frames==0,"unknown snapshot phase cannot alter live motion");
    bad=baseline;bad.velocity=9;require(motion.restore(bad)==Status::invalid_input,"velocity above source8 is invalid");
    bad=baseline;bad.target_direction=std::numeric_limits<double>::max();require(motion.restore(bad)==Status::invalid_input,"heading that cannot survive source float assignment is rejected before placement");
    bad=baseline;bad.move_total_frames=INT32_MAX;bad.move_frames=INT32_MAX-1;bad.velocity=8;
    require(motion.restore(bad)==Status::success,"prepare bounded arithmetic overflow fixture without changing source geometry");
    odh::RouteFrameContext parent;parent.state=odh::RouteFrameState::room_portal;parent.portal_turns=1;
    require(motion.prepare_step(frame,&parent).status==Status::unsupported_arithmetic&&parent.portal_turns==1&&saved(motion).move_frames==INT32_MAX-1,
        "source signed frame-add overflow returns unsupported without publishing counters or parent changes");
    odh::WalkMotion empty;
    require(empty.initialize({},{{0,0},1,0},{})==Status::finished&&empty.prepare_step(frame).status==Status::finished,
        "empty queue means component-finished and never fabricates authored NoPath");
}
void stable_velocity_for_pending_alternative(){
    odh::WalkMotion motion;auto frame=walking();
    require(motion.set_velocity(0)==Status::invalid_input&&
        motion.initialize({line(0,-20),line(10,-20,0,-20)},{{0,0},1,0},{},false,8)==Status::success,
        "Velocity changes require an initialized stable source integrator");
    const auto step=motion.prepare_step(frame);
    require(step.step&&step.step->data().move_frames==8&&motion.set_velocity(0)==Status::invalid_input&&
        motion.can_commit(*step.step),"timeout cannot alter the speed of an outstanding placement lease");
    require(motion.reject_step(*step.step)==Status::collision_required,"retain old path for alternative selection");
    const auto before=saved(motion);
    require(motion.set_velocity(0)==Status::success,"timeout fallback sets source Velocity0 without resuming old path");
    const auto paused=saved(motion);
    require(paused.phase==Phase::collision_required&&paused.move_frames==8&&paused.move_total_frames==before.move_total_frames&&
        paused.current&&before.current&&paused.current->a.y==before.current->a.y&&paused.current->d.y==before.current->d.y&&
        paused.remaining.size()==1&&paused.remaining.front().d.x==10*h&&paused.previous_position.position.y==0&&
        paused.target_direction==before.target_direction&&paused.walk_direction==before.walk_direction&&paused.turn_frames==before.turn_frames,
        "pending AttemptRoute preserves old CurrentPath, remaining segments, directions and rejected frame counters");
    require(motion.prepare_step(frame).status==Status::collision_required&&motion.resume_after_collision()==Status::success&&
        motion.set_velocity(3)==Status::success&&motion.phase()==Phase::walking,
        "stable walking WaitTime deceleration changes velocity without requiring a rejected phase");
    odh::WalkMotion restored;require(restored.restore(saved(motion))==Status::success,"pending alternative velocity survives fresh sampler restore");
    const auto next=motion.prepare_step(frame),copy=restored.prepare_step(frame);
    require(next.step&&copy.step&&next.step->data().move_frames==12&&next.step->data().velocity==4&&
        next.step->data().move_frames==copy.step->data().move_frames&&same(next.step->data().sample.velocity,copy.step->data().sample.velocity),
        "retained path accelerates from adjusted source velocity without resetting its frame to zero");
    require(motion.commit_step(*next.step).status==Status::success&&motion.set_velocity(9)==Status::invalid_input&&
        motion.set_velocity(-1)==Status::invalid_input&&saved(motion).velocity==4,
        "out-of-range speed changes do not publish source state");
    odh::WalkMotion finished;require(finished.initialize({},{{0,0},1,0},{})==Status::finished&&
        finished.set_velocity(2)==Status::success&&saved(finished).phase==Phase::finished&&
        saved(finished).move_total_frames==-1&&finished.prepare_step(frame).status==Status::finished,
        "WaitTime may alter retained finished integrator Velocity without reviving a path");
}
}
int main(){try{durations();source_ramp_and_tail();heading_feedback();portal_turn_short_circuit();tokens_and_collision();segment_advance_and_restore();limits_and_malformed();stable_velocity_for_pending_alternative();
    std::cout<<checks<<" walk motion checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"walk motion: "<<error.what()<<'\n';return 1;}}
