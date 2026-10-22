#include "odh/distance_to.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

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
        const bool caller_default=op==std::array<std::uint8_t,8>{0,0,0,0,0,0,0,0};
        // The saved Destination.iff Go Here action asks for the distance from
        // StackObject to MyObject[11] (the caller's own object ID). The final
        // two bytes are unused by this Simitone operand reader, but retaining
        // the exact observed encoding keeps this profile deliberately narrow.
        const bool saved_go_here=op==std::array<std::uint8_t,8>{0,0,255,3,11,0,255,255};
        // Destination.iff BHAV4107 (Move out of way) uses the same parsed
        // fields with its own serialized trailing bytes. Simitone's operand
        // reader ignores those bytes, so this exact resource encoding has the
        // same same-level distance meaning as Go Here's form above.
        const bool saved_shoo=op==std::array<std::uint8_t,8>{0,0,1,3,11,0,0,0};
        if(!caller_default&&!saved_go_here&&!saved_shoo)
            return {PrimitiveDisposition::unsupported,"GetDistanceTo outside Temp0/Caller or Destination Go Here/same-level profile"};
        const auto* stack_object=world.find(frame.context.stack_object);
        const auto* caller=world.find(frame.context.caller);
        if(!stack_object||!caller||!stack_object->position||!caller->position)
            return {PrimitiveDisposition::unsupported,"GetDistanceTo requires positioned Caller and StackObject"};
        const auto level_difference=static_cast<std::uint16_t>(
            std::abs(static_cast<int>(stack_object->position->level)-static_cast<int>(caller->position->level)));
        if(level_difference!=0&&!saved_go_here)
            return {PrimitiveDisposition::unsupported,"cross-level GetDistanceTo is supported only for saved Go Here"};
        const auto dx=static_cast<std::int64_t>(stack_object->position->x)-caller->position->x;
        const auto dy=static_cast<std::int64_t>(stack_object->position->y)-caller->position->y;
        const auto squared=static_cast<std::uint64_t>(dx*dx+dy*dy);
        const auto planar_distance=integer_sqrt(squared)/16U;
        // TS1's Simitone profile adds five tiles per floor and enforces a
        // twenty-tile minimum for cross-floor distance. Keep it to the exact
        // saved Go Here encoding; other callers retain the same-floor gate.
        const auto distance=static_cast<std::int16_t>(level_difference==0?planar_distance:
            std::max<std::uint32_t>(20U*level_difference,planar_distance+5U*level_difference));
        PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};
        result.temp_write=VmTempWrite{0,distance};
        return result;
    };
}
}
