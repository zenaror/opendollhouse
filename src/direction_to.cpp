#include "odh/direction_to.hpp"
#include <array>
#include <cstdint>

namespace odh {
namespace {
bool valid(std::int16_t index,std::size_t size) {
    return index>=0&&static_cast<std::size_t>(index)<size;
}
std::optional<std::int16_t> read_value(const VmFrame& frame,
    const std::array<std::int16_t,20>& temps,const VmMemory& memory,std::uint8_t scope,
    std::int16_t index) {
    switch(scope) {
        case 7:return index;
        case 8:return valid(index,temps.size())?std::optional<std::int16_t>{temps[static_cast<std::size_t>(index)]}:std::nullopt;
        case 9:return valid(index,frame.arguments.size())?std::optional<std::int16_t>{frame.arguments[static_cast<std::size_t>(index)]}:std::nullopt;
        case 10:return frame.context.stack_object;
        case 25:return valid(index,frame.locals.size())?std::optional<std::int16_t>{frame.locals[static_cast<std::size_t>(index)]}:std::nullopt;
        default: {
            const auto address=resolve_person_data_address(scope,index,temps);
            return address&&memory.read?memory.read(frame,address->scope,address->index):std::nullopt;
        }
    }
}
std::uint8_t direction_notch(std::int64_t dx,std::int64_t dy) {
    const auto ax=static_cast<std::uint64_t>(dx<0?-dx:dx);
    const auto ay=static_cast<std::uint64_t>(dy<0?-dy:dy);
    if(ax==0&&ay==0)return 0;
    constexpr std::uint64_t scale=65536, tan22_5_q16=27146;
    const bool x_axis=ax>ay;
    const auto major=x_axis?ax:ay,minor=x_axis?ay:ax;
    // At the 22.5-degree boundary Math.Round chooses the even cardinal notch.
    const bool cardinal=minor*scale<=major*tan22_5_q16;
    if(cardinal) {
        if(x_axis)return dx>0?2:6;
        return dy<0?0:4;
    }
    if(dx>0)return dy<0?1:3;
    return dy>0?5:7;
}
}
VmPrimitive get_direction_to_primitive(const World& world,VmMemory memory) {
    return [&world,memory=std::move(memory)](const VmFrame& frame,const Instruction& i,
        const auto& temps,const VmDispatch&)->PrimitiveOutcome {
        if(i.opcode!=12)return {};
        const auto& op=i.operands;
        if(op[3]!=0||(op[4]&0xFEU)!=0)
            return {PrimitiveDisposition::unsupported,"GetDirectionTo fields outside the TS1 Caller/explicit-object profile"};
        std::int16_t source_id=frame.context.caller;
        if((op[4]&1U)!=0) {
            const auto index=static_cast<std::int16_t>(static_cast<std::uint16_t>(op[6])|
                (static_cast<std::uint16_t>(op[7])<<8));
            const auto value=read_value(frame,temps,memory,op[5],index);
            if(!value)return {PrimitiveDisposition::unsupported,"GetDirectionTo source selector unavailable"};
            source_id=*value;
        }
        const auto destination_index=static_cast<std::int16_t>(static_cast<std::uint16_t>(op[0])|
            (static_cast<std::uint16_t>(op[1])<<8));
        const auto destination_scope=op[2];
        if(destination_scope!=8||destination_index<0||destination_index>=20)
            return {PrimitiveDisposition::unsupported,"GetDirectionTo destination is outside the Temp register profile"};
        const auto* source=world.find(source_id);
        const auto* destination=world.find(frame.context.stack_object);
        if(!source||!destination||!source->position||!destination->position)
            return {PrimitiveDisposition::unsupported,"GetDirectionTo requires positioned source and StackObject"};
        const auto dx=static_cast<std::int64_t>(destination->position->x)-source->position->x;
        const auto dy=static_cast<std::int64_t>(destination->position->y)-source->position->y;
        PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};
        result.temp_write=VmTempWrite{static_cast<std::size_t>(destination_index),direction_notch(dx,dy)};
        return result;
    };
}
}
