// SPDX-License-Identifier: MPL-2.0
// FreeSO VMRoutingFrame.AttemptWalk, revision
// 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once
#include "odh/live_attempt_walk.hpp"
#include "odh/walk_path.hpp"

namespace odh {
enum class SourceWalkPlanState { ready, turn_only, no_path, unsupported, budget_exhausted };
struct SourceWalkPlanRequest {
    LiveAttemptWalkIdentity actor;
    RectRoutePoint destination;
    bool bezier=true;
    std::optional<float> incoming_direction;
    std::optional<float> outgoing_direction;
};
struct SourceWalkDomain {
    LiveAttemptWalkState state=LiveAttemptWalkState::unsupported;
    LiveAttemptWalkRequest request;
    LiveAttemptWalkServices services;
    std::string diagnostic;
};
struct SourceWalkPlanServices {
    // Routing supplies a callback-scoped source execution proof through the
    // closures. The planner does not mint authorization for the EP5 service.
    std::function<bool()> context_still_matches;
    std::function<std::optional<std::uint16_t>(EntityPosition)> room_at;
    // Lazy: never called for same-XY turn-only or known room zero. Must supply
    // the COMPLETE room, not a radius/window around the actor or destination.
    std::function<SourceWalkDomain(std::uint16_t)> domain;
};
struct SourceWalkPlanLimits {
    LiveAttemptWalkLimits live;
    RectRouteLimits router;
    RectRouteLineLimits lines;
    WalkPathLimits path;
};
struct SourceWalkPlanResult {
    SourceWalkPlanState state=SourceWalkPlanState::unsupported;
    std::vector<WalkSegmentGeometry> segments;
    std::uint16_t room=0;
    std::uint8_t initial_heading=0;
    std::size_t entry_point5_calls=0,obstacles=0,route_work=0,line_work=0,path_work=0;
    std::string diagnostic;
};
// Complete AttemptWalk planning sequence, without World movement, animation,
// nested portal execution or save I/O. Only a complete domain's geometric
// rejection (or source room zero) can return no_path. Unknown host evidence,
// changed source context and bounded resource exhaustion remain distinct.
SourceWalkPlanResult plan_source_walk(const World&,const SourceWalkPlanRequest&,
    const SourceWalkPlanServices&,const SourceWalkPlanLimits& = {});
} // namespace odh
