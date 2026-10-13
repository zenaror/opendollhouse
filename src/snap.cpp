#include "odh/snap.hpp"
#include <array>
#include <stdexcept>
namespace odh {
Snap::Snap(World& world,std::int16_t object,ObjdBinding binding,const SlotTable& slots,std::uint16_t slot_id)
    :world_(world),object_(object),generation_(0),binding_(binding),slots_(slots) {
    const auto* entity=world_.find(object);
    if(!entity || entity->kind!=EntityKind::object || !slot_id || binding.slot_table!=slot_id)
        throw std::invalid_argument("snap owner or SLOT binding mismatch");
    generation_=entity->generation;
}
bool Snap::bind(Vm& vm) {if(vm_ && vm_!=&vm)return false;vm_=&vm;return true;}
VmPrimitive Snap::primitive() {
    return [this](const VmFrame& frame,const Instruction& i,const auto&,const VmDispatch& dispatch) -> PrimitiveOutcome {
        if(i.opcode!=46) return {};
        const auto fail=[](const char* why){return PrimitiveOutcome{PrimitiveDisposition::unsupported,why};};
        if(!vm_ || dispatch.origin!=vm_) return fail("snap controller bound to another VM");
        const auto index=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
        const auto mode=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
        if(mode!=3 || (i.operands[4]!=0 && i.operands[4]!=4) || i.operands[5] || i.operands[6] || i.operands[7])
            return fail("snap operand profile not implemented");
        const auto* object=world_.find(object_);const auto* actor=world_.find(frame.context.caller);
        if(!object || object->generation!=generation_ || frame.context.stack_object!=object_ ||
           !object->position || object->attachment || !actor || !actor->position || actor->kind!=EntityKind::avatar)
            return fail("snap context or positioned entity binding missing");
        const auto* route=slot_of_type(slots_,3,index);
        if(!route) return {PrimitiveDisposition::goto_false,{}};
        if(slots_.version!=10 || route->offset_bits!=std::array<std::uint32_t,3>{0,0,0} ||
           (route->flags!=0 && route->flags!=4096) || route->standing || route->sitting || route->ground || route->snap_target_slot<0)
            return fail("snap route SLOT profile not implemented");
        const auto number=static_cast<std::size_t>(route->snap_target_slot);
        const auto* storage=slot_of_type(slots_,0,number);
        if(!storage) return {PrimitiveDisposition::goto_false,{}};
        if(storage->offset_bits!=std::array<std::uint32_t,3>{0,0,0} || storage->height!=7)
            return fail("snap storage offset/height profile not implemented");
        if(binding_.slot_table==0 || actor->position->level!=object->position->level)
            return fail("snap binding or level unsupported");
        std::optional<std::uint8_t> direction;
        if(route->flags==4096) {
            // VMSlotParser SnapToDirection with no compass bits: negative
            // Facing uses relative NORTH; nonnegative Facing is an offset.
            if(!route->facing || *route->facing<-3 || *route->facing>7) return fail("snap facing outside eight-direction profile");
            const auto relative=*route->facing<0?0:*route->facing;
            direction=static_cast<std::uint8_t>((object->position->direction+relative)%8);
        }
        return {world_.place_in_slot(object_,number,actor->id,direction) ? PrimitiveDisposition::goto_true : PrimitiveDisposition::goto_false,{}};
    };
}
}
