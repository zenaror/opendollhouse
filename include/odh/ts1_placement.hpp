// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include <memory>
namespace odh {
struct Ts1PlacementIdentity {std::int16_t id=0;std::uint64_t generation=0;};
struct Ts1PlacementInvocation {
    const VmFrame& frame;
    const Instruction& instruction;
    const VmDispatch& dispatch;
    std::function<bool()> source_still_matches;
};
enum class Ts1PlacementStatus {success,blocked,unsupported};
struct Ts1PlacementResult {
    Ts1PlacementStatus status=Ts1PlacementStatus::unsupported;
    std::int16_t placement_error=-1;
    Ts1PlacementIdentity blocker;
    std::string diagnostic;
};
using Ts1PlacementTransfer=std::function<Ts1PlacementResult(const Ts1PlacementInvocation&,
    Ts1PlacementIdentity container,std::size_t slot,Ts1PlacementIdentity item)>;
using Ts1PlacementFloor=std::function<Ts1PlacementResult(const Ts1PlacementInvocation&,
    Ts1PlacementIdentity,std::optional<EntityPosition>,std::uint8_t flags)>;
enum class Ts1SlotLookupStatus {available,missing,unsupported};
struct Ts1SlotLookup {
    Ts1SlotLookupStatus status=Ts1SlotLookupStatus::unsupported;
    std::shared_ptr<const SlotTable> table;
    std::string diagnostic;
};
using Ts1PlacementSlots=std::function<Ts1SlotLookup(const Ts1PlacementInvocation&,Ts1PlacementIdentity,bool global)>;
struct Ts1PlacementGoal {
    EntityPosition position{};
    float radians=0;
    bool face_anywhere=false;
    double score=0;
    std::uint8_t entry_flags=1;
    Ts1PlacementIdentity chair;
};
struct Ts1PlacementLocations {
    bool supported=false;
    std::vector<Ts1PlacementGoal> goals;
    bool no_valid_goals=true;
    Ts1PlacementIdentity blocker;
    std::string diagnostic;
    std::int16_t failure_code=7; // VMRouteFailCode, NOT PrimitiveResult2.
};
struct Ts1PlacementServices {
    Ts1PlacementFloor place;
    Ts1PlacementTransfer transfer;
    Ts1PlacementSlots slots;
    // Shared VMSlotParser used by Snap and routing. Owns real room, RNG, EP5
    // and destination checks in source order, even for contained Snap targets.
    std::function<Ts1PlacementLocations(const Ts1PlacementInvocation&,const SlotRecord&,
        Ts1PlacementIdentity target,Ts1PlacementIdentity caller)> locations;
    std::function<std::optional<bool>(const Ts1PlacementInvocation&,const EntityPosition&)> tile_occupied;
    std::function<std::optional<std::pair<std::int16_t,std::int16_t>>(const Ts1PlacementInvocation&)> lot_size;
    std::function<std::optional<std::uint64_t>(const Ts1PlacementInvocation&,std::uint64_t)> random;
    // Source Snap shoo: inspect existing blocker queue, construct actual GOTO
    // object and enqueue interaction3 only when absent. False = unavailable.
    std::function<bool(const Ts1PlacementInvocation&,Ts1PlacementIdentity)> shoo;
    // Source caller==StackObject object Snap sets MovedSelf on every group part
    // before final placement, including a later placement rejection.
    std::function<bool(const Ts1PlacementInvocation&,Ts1PlacementIdentity)> moved_self;
};
// Shared source FindLocationFor radial search. Callers supply actual placement
// and source position on every radius, without manufacturing a VM invocation.
struct Ts1RadialPlacementServices {
    std::function<bool()> source_still_matches;
    std::function<std::optional<EntityPosition>()> reference_position;
    std::function<Ts1PlacementResult(EntityPosition)> place;
    std::function<std::optional<bool>(EntityPosition)> tile_occupied;
};
Ts1PlacementResult ts1_find_location_radial(const Ts1RadialPlacementServices&,
    bool prefer_non_empty=false);
// Source end-of-frame Delete authority is independent of a BHAV dispatch.
struct Ts1LifecyclePlacementInvocation {
    World::RetiredContainer container;
    std::function<bool()> source_still_matches;
    // Captured source field, before retirement releases its live motion record.
    // Absence is unknown, never an inferred false value.
    std::optional<bool> parent_moved_self;
};
// SLOT.HeightOffsets:0,2.5,4,4,0,0,7,4,0. Height5 uses Offset.Z.
std::optional<float> ts1_storage_slot_height(const SlotRecord&);
// No persistent state: all mutations belong to the actual World/Playback/queue.
class Ts1Placement {
public:
    Ts1Placement(World& world,Ts1PlacementServices services):world_(world),services_(std::move(services)){}
    VmPrimitive primitive();
    Ts1PlacementResult transfer(const Ts1PlacementInvocation&,Ts1PlacementIdentity,std::size_t,Ts1PlacementIdentity);
private:
    PrimitiveOutcome snap(const Ts1PlacementInvocation&);
    PrimitiveOutcome find_location(const Ts1PlacementInvocation&);
    PrimitiveOutcome drop(const Ts1PlacementInvocation&);
    World& world_;Ts1PlacementServices services_;bool executing_=false;
};
}
