// SPDX-License-Identifier: MPL-2.0
// FreeSO VMContext.RegisterObjectPos / VMRoutingFrame.AttemptWalk,
// revision 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once
#include "odh/source_walk_plan.hpp"
#include "odh/world_room_info.hpp"

namespace odh {
struct LiveWorldAttemptWalkRequest {
    LiveAttemptWalkIdentity actor;
    std::uint16_t room=0;
    std::uint8_t level=0; // One-based World level.
    LiveAttemptWalkRoomBounds room_bounds;
    std::vector<RectRouteObstacle> architectural_and_bounds;
    std::size_t architectural_count=0;
    bool architecture_complete=false;
    std::vector<LiveAttemptWalkIdentity> avatars_to_consider;
    bool avatars_to_consider_complete=false;
};
struct LiveWorldAttemptWalkServices {
    // Exact source room map for this lease. Side-effect-free and immutable
    // throughout composition, including registration-event room classification.
    RuntimeRoomResolver room_at;
    std::function<bool()> context_still_matches;
    std::function<LiveAttemptWalkEntryPoint5Result(const LiveAttemptWalkEntryPoint5Request&)> entry_point5;
};
struct LiveWorldAttemptWalkLimits {
    std::size_t entities=4096,registration_events=4096;
};
// Enumerates complete RoomInfo.Entities, not the non-null obstacle subset.
// Footprints/Flags8 are read live at each source foreach turn. World and the
// immutable DefinitionIndex must outlive the returned callback closures.
// Changed-room registration (including net-zero remove/reinsert) invalidates
// the domain; genuinely unrelated-room registration remains admissible.
SourceWalkDomain build_live_world_attempt_walk_domain(const World&,const DefinitionIndex&,
    const LiveWorldAttemptWalkRequest&,LiveWorldAttemptWalkServices,
    const LiveWorldAttemptWalkLimits& = {});
} // namespace odh
