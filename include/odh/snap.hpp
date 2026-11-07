#pragma once
#include "odh/world.hpp"
namespace odh {
// Zero-offset contained Literal snap, source SnapToDirection exact points,
// and existing explicit cardinal chair floor exits. Clearance is supplied by the caller;
// this profile does not implement generic room or wall-raycast geometry.
// Containment is selected by SnapTargetSlot, not operand FootprintExtension.
class Snap {
public:
    using PlacementValidator=std::function<bool(std::int16_t,const EntityPosition&)>;
    enum class StairPlacementStatus { accepted, blocked, unsupported };
    using StairPlacementValidator=std::function<StairPlacementStatus(
        std::int16_t,std::int16_t,const EntityPosition&)>;
    enum class SourcePlacementStatus { accepted, blocked_object, blocked_avatar, out_of_bounds, unsupported };
    struct SourcePlacementResult {
        SourcePlacementStatus status=SourcePlacementStatus::unsupported;
        // blocked_object with zero blocker represents a non-entity placement
        // error: candidate PositionValid ignores it, final SetPosition fails.
        std::int16_t blocker=0;
        std::uint64_t blocker_generation=0;
    };
    using DirectionalPlacementService=std::function<SourcePlacementResult(
        const VmFrame&,const Instruction&,const VmDispatch&,std::int16_t actor,
        std::int16_t target,const EntityPosition&)>;
    struct DirectionalServices {
        // Candidate uses NORTH, AcceptSlots|AllAvatarsSolid. A blocking avatar
        // only penalizes the single goal; an object excludes that goal.
        DirectionalPlacementService candidate;
        // Owns final default SetPosition, including source EP5 side effects.
        // Accepted must already publish quantized XYZ/direction. The primitive
        // applies the frozen continuous angle only after successful placement.
        DirectionalPlacementService place;
        // The host can require an authenticated, live portal invocation for
        // stair placement. Evaluated per execution, never cached by target.
        std::function<bool(const VmFrame&,const Instruction&,const VmDispatch&)> stair_authorized;
    };
    Snap(World& world,std::int16_t object,ObjdBinding binding,const SlotTable& slots,std::uint16_t slot_id,
         PlacementValidator placement_validator={},StairPlacementValidator stair_placement_validator={},
         bool global_slot_table=false,DirectionalServices directional_services={});
    Snap(const Snap&)=delete;
    Snap& operator=(const Snap&)=delete;
    bool bind(const Vm& vm);
    VmPrimitive primitive();
private:
    PrimitiveOutcome directional_snap(const VmFrame&,const Instruction&,const VmDispatch&,const SlotRecord&);
    World& world_;
    std::int16_t object_;
    std::uint64_t generation_;
    ObjdBinding binding_;
    const SlotTable& slots_;
    PlacementValidator placement_validator_;
    StairPlacementValidator stair_placement_validator_;
    bool global_slot_table_=false;
    DirectionalServices directional_services_;
    bool executing_=false;
    const Vm* vm_=nullptr;
};
}
