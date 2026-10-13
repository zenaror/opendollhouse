#pragma once
#include "odh/world.hpp"
namespace odh {
// Initial contained Literal snap profile for one explicitly bound code owner.
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
