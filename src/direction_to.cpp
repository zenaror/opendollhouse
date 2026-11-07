// SPDX-License-Identifier: MPL-2.0
// Port of FreeSO 7e76c1960252ddd033d8937d5edb47d326f5b69c:
// TSOClient/tso.simantics/Primitives/VMGetDirectionTo.cs, with direction
// normalization from TSOClient/tso.common/Utils/DirectionUtils.cs and setter
// semantics from TSOClient/tso.simantics/Engine/VMMemory.cs.
#include "odh/direction_to.hpp"
#include "odh/ts1_variable.hpp"
#include <cmath>
#include <utility>

namespace odh {
namespace {
std::int16_t signed_word(const std::array<std::uint8_t,8>& operands,std::size_t offset) {
    const auto bits=static_cast<std::uint16_t>(operands[offset]|(std::uint16_t(operands[offset+1])<<8));
    return static_cast<std::int16_t>(bits<32768?std::int32_t(bits):std::int32_t(bits)-65536);
}
struct Position {std::int32_t x,y;};
std::optional<Position> source_position(const Entity& entity) {
    // LotTilePos.OUT_OF_WORLD is a real source coordinate, not a missing entity.
    if(!entity.position)return Position{-32768,-32768};
    const auto& p=*entity.position;
    if(p.x<INT16_MIN||p.x>INT16_MAX||p.y<INT16_MIN||p.y>INT16_MAX||p.level<INT8_MIN||p.level>INT8_MAX)return {};
    return Position{p.x,p.y};
}
std::int16_t source_direction(Position source,Position target) {
    constexpr double pi=3.141592653589793238462643383279502884;
    const auto pos_mod=[](double value,double modulus){return std::fmod(std::fmod(value,modulus)+modulus,modulus);};
    auto direction=pos_mod(std::atan2(double(target.x-source.x),double(source.y-target.y)),pi*2);
    if(direction>pi)direction-=pi*2;
    const double value=(pos_mod(direction,pi*2)/pi)*4;
    // Math.Round(double) is ties-to-even, independent of the caller's FP mode.
    const double lower=std::floor(value),fraction=value-lower;
    return static_cast<std::int16_t>(lower+(fraction>0.5||(fraction==0.5&&static_cast<int>(lower)%2!=0)?1:0));
}
bool source_ignored_setter(std::uint16_t scope) {
    // SetVariable returns false (ignored by this primitive), or the explicit
    // MoneyOverHead32Bit no-op returns true. No host callback is needed.
    switch(scope){case 7:case 21:case 26:case 35:case 38:case 43:case 44:case 45:
        case 48:case 51:case 52:case 53:case 59:return true;default:return false;}
}
bool source_writable_scope(std::uint16_t scope) {
    switch(scope){case 0:case 1:case 3:case 4:case 6:case 8:case 9:case 10:case 11:
        case 12:case 13:case 14:case 15:case 17:case 18:case 19:case 22:case 25:
        case 27:case 28:case 29:case 30:case 31:case 36:case 37:case 40:case 41:
        case 49:case 50:return true;default:return false;}
}
}
VmPrimitive get_direction_to_primitive(const World& world,VmMemory memory) {
    return [&world,memory=std::move(memory)](const VmFrame& frame,const Instruction& instruction,
        const Ts1Temps& temps,const VmDispatch&)->PrimitiveOutcome {
        if(instruction.opcode!=12)return {};
        const auto& op=instruction.operands;
        // Operand.Read rewrites every even Flags value to MyObject[11] and
        // sets bit0. Other flag bits have no effect on the execution.
        const auto source_scope=std::uint8_t((op[4]&1U)?op[5]:3);
        const auto source_data=(op[4]&1U)?signed_word(op,6):std::int16_t{11};
        const auto source_id=read_ts1_variable(world,memory,frame,temps,source_scope,source_data);
        if(!source_id)return {PrimitiveDisposition::unsupported,"GetDirectionTo source selector unavailable"};
        const auto* source=world.find(*source_id);
        const auto* target=world.frame_entity(frame,VmEntityRole::stack_object);
        if(!source||!target)return {PrimitiveDisposition::unsupported,"GetDirectionTo source or StackObject identity unavailable"};
        const auto from=source_position(*source),to=source_position(*target);
        if(!from||!to)return {PrimitiveDisposition::unsupported,"GetDirectionTo coordinates exceed source LotTilePos bounds"};
        const auto result=source_direction(*from,*to);
        const auto result_scope=static_cast<std::uint16_t>(op[2]|(std::uint16_t(op[3])<<8));
        if(source_ignored_setter(result_scope))return {PrimitiveDisposition::goto_true,{}};
        if(!source_writable_scope(result_scope))return {PrimitiveDisposition::unsupported,
            "GetDirectionTo result scope has no source-supported setter: "+std::to_string(result_scope)};
        const auto address=resolve_ts1_variable_address(static_cast<std::uint8_t>(result_scope),signed_word(op,0),frame,temps);
        if(!address)return {PrimitiveDisposition::unsupported,"GetDirectionTo result selector unavailable"};
        // The VM owns publication, including effective check memory and indirect
        // selectors. This primitive never calls a mutable memory backend itself.
        PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};
        outcome.variable_write=VmVariableWrite{address->scope,address->index,result};
        return outcome;
    };
}
}
