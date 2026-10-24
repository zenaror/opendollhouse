#pragma once
#include "odh/world.hpp"
namespace odh {
// Zero-offset contained Literal snap and explicit cardinal chair floor exits,
// including SLOT SnapToDirection. Floor clearance is supplied by the caller;
// this profile does not implement generic room or wall-raycast geometry.
// Containment is selected by SnapTargetSlot, not operand FootprintExtension.
class Snap {
public:
    using PlacementValidator=std::function<bool(std::int16_t,const EntityPosition&)>;
    enum class StairPlacementStatus { accepted, blocked, unsupported };
    using StairPlacementValidator=std::function<StairPlacementStatus(
        std::int16_t,std::int16_t,const EntityPosition&)>;
    Snap(World& world,std::int16_t object,ObjdBinding binding,const SlotTable& slots,std::uint16_t slot_id,
         PlacementValidator placement_validator={},StairPlacementValidator stair_placement_validator={},
         bool global_slot_table=false);
    Snap(const Snap&)=delete;
    Snap& operator=(const Snap&)=delete;
    bool bind(const Vm& vm);
    VmPrimitive primitive();
private:
    World& world_;
    std::int16_t object_;
    std::uint64_t generation_;
    ObjdBinding binding_;
    const SlotTable& slots_;
    PlacementValidator placement_validator_;
    StairPlacementValidator stair_placement_validator_;
    bool global_slot_table_=false;
    bool executing_=false;
    const Vm* vm_=nullptr;
};
}
