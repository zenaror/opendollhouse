#include "odh/distance_to.hpp"
#include <cstdint>

namespace odh {
namespace {
std::uint32_t integer_sqrt(std::uint64_t value) {
    std::uint64_t low=0,high=131073;
    while(low+1<high) {
        const auto middle=low+(high-low)/2;
        if(middle*middle<=value)low=middle;
        else high=middle;
    }
    return static_cast<std::uint32_t>(low);
}
}
VmPrimitive get_distance_to_primitive(const World& world) {
    return [&world](const VmFrame& frame,const Instruction& instruction,
                    const std::array<std::int16_t,20>&,const VmDispatch&)->PrimitiveOutcome {
        if(instruction.opcode!=11)return {};
        const auto& op=instruction.operands;
        if(op!=std::array<std::uint8_t,8>{0,0,0,0,0,0,0,0})
            return {PrimitiveDisposition::unsupported,"GetDistanceTo outside Temp0/default Caller/same-level profile"};
        const auto* stack_object=world.find(frame.context.stack_object);
        const auto* caller=world.find(frame.context.caller);
        if(!stack_object||!caller||!stack_object->position||!caller->position)
            return {PrimitiveDisposition::unsupported,"GetDistanceTo requires positioned Caller and StackObject"};
        if(stack_object->position->level!=caller->position->level)
            return {PrimitiveDisposition::unsupported,"GetDistanceTo cross-level formula is outside the supported profile"};
        const auto dx=static_cast<std::int64_t>(stack_object->position->x)-caller->position->x;
        const auto dy=static_cast<std::int64_t>(stack_object->position->y)-caller->position->y;
        const auto squared=static_cast<std::uint64_t>(dx*dx+dy*dy);
        const auto distance=static_cast<std::int16_t>(integer_sqrt(squared)/16U);
        PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};
        result.temp_write=VmTempWrite{0,distance};
        return result;
    };
}
}
