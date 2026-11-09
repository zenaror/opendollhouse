// SPDX-License-Identifier: MPL-2.0
// Source: FreeSO VMEntity.PositionValid/WallChangeValid/FloorChangeValid and
// VMContext.GetObjPlace, revision7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once
#include "odh/room_info_obstacles.hpp"
#include "odh/world.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace odh {
struct ObjectPositionIdentity {std::int16_t id=0;std::uint64_t generation=0;};
enum class ObjectPositionValidStatus {success,blocked,unsupported};
struct ObjectPositionValidResult {
    ObjectPositionValidStatus status=ObjectPositionValidStatus::unsupported;
    std::int16_t placement_error=-1;
    // Source VMPlacementResult.Object, retained on a rejection too. A success
    // with support_slot asks the owning placement service to use object.slot0.
    ObjectPositionIdentity object;
    bool support_slot=false;
    std::string diagnostic;
};
struct ObjectPositionArchitecture {
    bool out_of_bounds=false,user_out_of_bounds=false,sloped=false;
    std::uint8_t wall_segments=0,occupied_walls=0;
    std::uint16_t precise_floor=0;
    std::optional<std::int16_t> level_offset;
};
struct ObjectPositionIntersections {
    bool footprint_present=false;
    RoomInfoObstacleQuery query;
};
struct ObjectPositionValidRequest {
    ObjectPositionIdentity target;
    EntityPosition candidate{};
    std::uint8_t direction=0,flags=0;
    // Mandatory host capability bound to the REAL creation/placement service,
    // checked before and after callbacks. It must tolerate this service's
    // temporary target word1/position.direction and certify actual source VM/ticket/run/actor
    // ownership. Never fabricate opcode42/45 or clone a VM/World for this.
    std::function<bool()> source_still_matches;
    // Native group rotation has already published every member's heading.
    // Validate that state; do not quantize, temporarily set or restore it.
    // Authored check effects remain owned by the enclosing native operation.
    bool heading_already_published=false;
};
struct ObjectPositionEntryPoint5Request {
    ObjectPositionIdentity tested,other;
    std::uint16_t action=0;
    std::int16_t ghost=0;
    std::array<std::int16_t,4> arguments{};
    std::function<bool()> source_still_matches;
};
struct ObjectPositionValidServices {
    std::function<std::optional<ObjectPositionArchitecture>(ObjectPositionIdentity,const EntityPosition&)> architecture;
    // Real candidate GetObstacle(pos,dir,true) and complete combined source
    // RoomInfo static/dynamic intersections. An absent footprint is known;
    // missing capability is not an empty room. This module sorts owners as source.
    std::function<std::optional<ObjectPositionIntersections>(ObjectPositionIdentity,const EntityPosition&,std::uint8_t)> intersections;
    std::function<std::optional<bool>(ObjectPositionIdentity,ObjectPositionIdentity)> same_group;
    std::function<std::optional<bool>(ObjectPositionIdentity,ObjectPositionIdentity)> ignore_intersection;
    // Source GhostImage, distinct from the host's UI-only Destination marker.
    std::function<std::optional<bool>(ObjectPositionIdentity)> ghost_image;
    // Actual source GetSlotHeight(0): effective SLOT runtime height for objects,
    //5 for avatars, and-1 for a known zero physical extent. Unknown resource or
    // inconsistent bank/format is nullopt, never an assumed height0.
    std::function<std::optional<std::int32_t>(ObjectPositionIdentity)> slot0_height;
    // Real tested-entity register bank, Caller=Callee=tested, SO=other. Preserve
    // RNG/Temp/World side effects even on false/unsupported; no diagnostic clone.
    std::function<std::optional<bool>(const ObjectPositionEntryPoint5Request&)> entry_point5;
    // Optional source publication boundary for the temporary Direction setter
    // and its mandatory restoration only. The supplied heading is already
    // quantized by live RotationNotches. Execute fixedsetter exactly once;
    // architecture, RoomInfo and actual EP5 callbacks stay outside this scope.
    std::function<bool(ObjectPositionIdentity,std::uint8_t,
        const std::function<bool()>& fixedsetter)> publish_heading;
};
class ObjectPositionValid {
public:
    ObjectPositionValid(World& world,ObjectPositionValidServices services):world_(world),services_(std::move(services)){}
    // PositionValid ONLY: wall/floor/slope, AllowIntersection, temporary heading,
    // GetObjPlace/support and AcceptSlots override. Never publishes position or
    // attachment, runs Init/Main, removes an object or rolls back check effects.
    // Object direction is source discrete word1 plus live RotationNotches23;
    // the avatar-only continuous Entity.radian_direction field is not used.
    ObjectPositionValidResult evaluate(const ObjectPositionValidRequest&);
private:
    World& world_;ObjectPositionValidServices services_;bool executing_=false;
};
} // namespace odh
