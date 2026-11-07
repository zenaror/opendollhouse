// SPDX-License-Identifier: MPL-2.0
#include "odh/source_walk_plan.hpp"
#include <cmath>
#include <exception>
#include <sstream>

namespace odh {
namespace {
bool same(LiveAttemptWalkIdentity a,LiveAttemptWalkIdentity b){return a.id==b.id&&a.generation==b.generation;}
// Source Math.Round(double) is ties-to-even, independent of the host FP
// rounding mode. The input is already a source binary32 angle promoted to
// double, divided by Math.PI/2f (a double expression in C#).
std::uint8_t quarter_heading(float radians){
    constexpr double pi=3.141592653589793238462643383279502884;
    const double value=static_cast<double>(radians)/(pi/2.0);
    const double base=std::floor(value),fraction=value-base;
    double rounded=base;
    if(fraction>0.5||(fraction==0.5&&std::fmod(base,2.0)!=0.0))rounded=base+1.0;
    double mod=std::fmod(rounded,4.0);if(mod<0)mod+=4.0;
    return static_cast<std::uint8_t>(mod);
}
}
SourceWalkPlanResult plan_source_walk(const World& world,const SourceWalkPlanRequest& request,
    const SourceWalkPlanServices& services,const SourceWalkPlanLimits& limits){
    SourceWalkPlanResult result;
    const auto fail=[&](const char* message,SourceWalkPlanState state=SourceWalkPlanState::unsupported){
        result.state=state;result.segments.clear();result.diagnostic=message;return result;
    };
    try {
        if(!services.context_still_matches||!services.context_still_matches())return fail("source routing context is not current");
        const auto* actor=world.find(request.actor.id);
        if(request.actor.id<=0||!request.actor.generation||!actor||actor->generation!=request.actor.generation||
           actor->kind!=EntityKind::avatar||!actor->position||actor->attachment)
            return fail("source walking actor is unavailable");
        const auto position=*actor->position;
        if(position.x==request.destination.x&&position.y==request.destination.y){
            result.state=SourceWalkPlanState::turn_only;return result;
        }
        if(!services.room_at)return fail("source room resolver is absent");
        const auto room=services.room_at(position);
        if(!services.context_still_matches())return fail("source context changed while resolving room");
        if(!room)return fail("source room is unknown");
        result.room=*room;
        if(!*room)return fail("source room zero cannot be walked",SourceWalkPlanState::no_path);
        if(!services.domain)return fail("complete source room domain provider is absent");
        auto domain=services.domain(*room);
        if(!services.context_still_matches())return fail("source context changed while preparing domain");
        if(domain.state!=LiveAttemptWalkState::ready){
            result.diagnostic=domain.diagnostic;
            result.state=domain.state==LiveAttemptWalkState::budget_exhausted?SourceWalkPlanState::budget_exhausted:SourceWalkPlanState::unsupported;
            return result;
        }
        if(!same(domain.request.actor,request.actor)||domain.request.room!=*room||
           position.level!=domain.request.level)return fail("complete source domain describes another route");
        auto obstacles=compose_live_attempt_walk_obstacles(world,domain.request,domain.services,limits.live);
        result.entry_point5_calls=obstacles.entry_point5_calls;
        if(!services.context_still_matches())return fail("source context changed during live obstacle checks");
        if(obstacles.state!=LiveAttemptWalkState::ready){
            result.diagnostic=obstacles.diagnostic;
            result.state=obstacles.state==LiveAttemptWalkState::budget_exhausted?SourceWalkPlanState::budget_exhausted:SourceWalkPlanState::unsupported;
            return result;
        }
        // Source reads Caller.RadianDirection after the effectful EP5 loop.
        // Do not derive it from an eight-notch direction or cached start angle.
        actor=world.find(request.actor.id);
        if(!actor||actor->generation!=request.actor.generation||!actor->position||
           !(*actor->position==position)||actor->attachment||!std::isfinite(actor->radian_direction))
            return fail("source actor changed during obstacle checks");
        result.initial_heading=quarter_heading(actor->radian_direction);
        result.obstacles=obstacles.obstacles.size();
        auto route=route_rectangles(obstacles.obstacles,{position.x,position.y},request.destination,result.initial_heading,limits.router);
        result.route_work=route.work_units;
        if(route.status==RectRouteStatus::no_path||route.status==RectRouteStatus::blocked_start)
            return fail("complete source obstacle domain rejects route",SourceWalkPlanState::no_path);
        if(route.status!=RectRouteStatus::found)return fail("source rectangle search is incomplete",
            route.status==RectRouteStatus::budget_exhausted?SourceWalkPlanState::budget_exhausted:SourceWalkPlanState::unsupported);
        const auto lines=optimize_rect_route_lines(route.corridor,request.destination,limits.lines,request.incoming_direction);
        result.line_work=lines.work_units;
        if(lines.status!=RectRouteStatus::found)return fail("source corridor optimization is incomplete",
            lines.status==RectRouteStatus::budget_exhausted?SourceWalkPlanState::budget_exhausted:SourceWalkPlanState::unsupported);
        auto path=generate_walk_path(lines,request.destination,request.bezier,request.incoming_direction,request.outgoing_direction,limits.path);
        result.path_work=path.work_units;
        if(path.status!=WalkPathStatus::success){
            // Keep enough bounded geometry to reproduce a shared generator
            // failure without turning a source arithmetic error into no-path.
            std::ostringstream detail;
            detail<<"source segment generation is incomplete: "<<walk_path_status_name(path.status)
                <<'('<<static_cast<unsigned>(path.status)<<") start="<<position.x<<','<<position.y
                <<" destination="<<request.destination.x<<','<<request.destination.y
                <<" bezier="<<request.bezier<<" incoming=";
            if(request.incoming_direction)detail<<*request.incoming_direction;else detail<<"none";
            detail<<" outgoing=";
            if(request.outgoing_direction)detail<<*request.outgoing_direction;else detail<<"none";
            detail<<" route="<<route.corridor.size()<<" corridor="<<lines.corridor.size();
            for(std::size_t i=0;i<lines.corridor.size();++i){
                if(i>=16&&i+16<lines.corridor.size())continue;
                const auto& e=lines.corridor[i];
                detail<<" ["<<i<<":"<<e.rectangle.x1<<','<<e.rectangle.y1<<','<<e.rectangle.x2<<','<<e.rectangle.y2
                    <<" entry="<<e.entry.x<<','<<e.entry.y
                    <<" hip="<<e.entry_hip.x<<','<<e.entry_hip.y<<" line="<<e.line_id<<']';
            }
            result.state=path.status==WalkPathStatus::budget_exhausted?SourceWalkPlanState::budget_exhausted:SourceWalkPlanState::unsupported;
            result.diagnostic=detail.str();return result;
        }
        if(path.segments.empty())return fail("nontrivial source route generated no walking segments");
        result.segments=std::move(path.segments);result.state=SourceWalkPlanState::ready;return result;
    }catch(const std::exception&){return fail("source walking service threw before a complete plan");}
}
} // namespace odh
