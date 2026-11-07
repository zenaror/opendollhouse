// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_placement.hpp"
namespace odh {
enum class Ts1SlotDiagnosticReason : std::uint8_t {
    bounds,room,direction,wall,position_blocked,position_unsupported,object,
    chair_candidate,chair_angle,chair_required,avatar_penalty,door_penalty,destination_conflict
};
const char* ts1_slot_diagnostic_reason_name(Ts1SlotDiagnosticReason) noexcept;
struct Ts1SlotDiagnosticSample {
    Ts1SlotDiagnosticReason reason=Ts1SlotDiagnosticReason::bounds;
    std::optional<EntityPosition> position;
    std::int16_t placement_error=-1;
    Ts1PlacementIdentity blocker;
    std::uint64_t occurrences=0;
};
struct Ts1SlotDiagnostics {
    std::uint64_t enumerated=0,proximity_rejected=0,room_rejected=0,direction_rejected=0,
        verified=0,bounds_rejected=0,wall_rejected=0,position_valid_calls=0,
        position_blocked=0,position_unsupported=0,object_rejected=0,chair_candidates=0,
        chair_angle_rejected=0,chair_required_rejected=0,avatar_penalties=0,
        door_penalties=0,destination_conflicts=0,accepted=0;
    bool counters_saturated=false,profile_decoded=false,supported=false,no_valid_goals=true;
    std::uint32_t effective_flags=0;
    std::int32_t minimum=0,maximum=0,desired=0,resolution=0,facing=0;
    std::int16_t level=0,failure_code=7;
    float center_x=0,center_y=0;
    std::optional<EntityPosition> target_start;
    Ts1PlacementIdentity blocker;
    std::array<Ts1SlotDiagnosticSample,16> samples{};
    std::size_t sample_count=0;
    std::uint64_t samples_dropped=0;
    std::string diagnostic; // Unsupported detail, bounded to256 bytes.
};
// Observational only: called once after the result is fixed. References are
// callback-scoped; exceptions from diagnostics never change simulation results.
using Ts1SlotDiagnosticsObserver=std::function<void(const Ts1PlacementInvocation&,const SlotRecord&,
    Ts1PlacementIdentity target,Ts1PlacementIdentity caller,const Ts1SlotDiagnostics&)>;
struct Ts1SlotLocationsServices {
    // Complete physical group, in source order. Used only for AverageObjectLocation.
    std::function<std::optional<std::vector<Ts1PlacementIdentity>>(Ts1PlacementIdentity)> group;
    std::function<std::optional<bool>(const EntityPosition&)> out_of_bounds;
    std::function<std::optional<std::uint16_t>(const EntityPosition&)> room_at;
    // Source integer TILE endpoints, not subtile coordinates.
    std::function<std::optional<bool>(std::int32_t,std::int32_t,std::int32_t,std::int32_t,std::int16_t)> raycast_wall;
    std::function<Ts1PlacementResult(const Ts1PlacementInvocation&,Ts1PlacementIdentity,const EntityPosition&,
        std::uint8_t direction,std::uint8_t flags)> position_valid;
    std::function<std::optional<bool>(Ts1PlacementIdentity)> chair_entry_point26;
    std::function<std::optional<bool>(const EntityPosition&)> architectural_door_at;
    // Checks other real avatars' active AND suspended routing destinations.
    std::function<std::optional<bool>(const Ts1PlacementInvocation&,Ts1PlacementIdentity,const Ts1PlacementGoal&)> destination_conflict;
    std::function<std::optional<std::uint64_t>(const Ts1PlacementInvocation&,std::uint64_t)> random;
    Ts1SlotDiagnosticsObserver diagnostics;
};
struct Ts1SlotLocationsLimits {std::size_t candidates=65536,group_members=4096;};
// Complete shared VMSlotParser geometry/scoring branch, including containment,
// exact point, circle/range, offsets, average center, chair and live checks.
// Callback effects and RNG survive rejection. No World or resource is cloned.
Ts1PlacementLocations ts1_find_slot_locations(const World&,const Ts1PlacementInvocation&,const SlotRecord&,
    Ts1PlacementIdentity target,Ts1PlacementIdentity caller,const Ts1SlotLocationsServices&,
    Ts1SlotLocationsLimits limits={});
}
