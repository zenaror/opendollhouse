#pragma once
#include "odh/world.hpp"
namespace odh {
// Zero-offset contained Literal snap, including SLOT SnapToDirection.
// Containment is selected by SnapTargetSlot, not operand FootprintExtension.
class Snap {
public:
    Snap(World& world,std::int16_t object,ObjdBinding binding,const SlotTable& slots,std::uint16_t slot_id);
    Snap(const Snap&)=delete;
    Snap& operator=(const Snap&)=delete;
    bool bind(Vm& vm);
    VmPrimitive primitive();
private:
    World& world_;
    std::int16_t object_;
    std::uint64_t generation_;
    ObjdBinding binding_;
    const SlotTable& slots_;
    Vm* vm_=nullptr;
};
}
