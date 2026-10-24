#include "odh/motive_change.hpp"

#include <cstdint>

namespace odh {
namespace {
PrimitiveOutcome unsupported(const char* why) {
    return {PrimitiveDisposition::unsupported, why};
}

std::int16_t wrap_i16(std::int32_t value) {
    const auto bits=static_cast<std::uint16_t>(static_cast<std::uint32_t>(value));
    const auto signed_value=bits<0x8000U?static_cast<std::int32_t>(bits):
        static_cast<std::int32_t>(bits)-0x10000;
    return static_cast<std::int16_t>(signed_value);
}

std::int16_t read_i16(const std::array<std::uint8_t,8>& bytes,std::size_t offset) {
    const auto bits=static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset])|
        (static_cast<std::uint16_t>(bytes[offset+1])<<8));
    const auto value=bits<0x8000U?static_cast<std::int32_t>(bits):
        static_cast<std::int32_t>(bits)-0x10000;
    return static_cast<std::int16_t>(value);
}
}

PrimitiveOutcome ts1_motive_change(World& world,const VmMemory& memory,
                                  const VmFrame& frame,const Instruction& instruction) {
    if(instruction.opcode!=29)return unsupported("Set Motive Change helper received another opcode");

    const auto* actor=world.find(frame.context.caller);
    if(!actor||actor->kind!=EntityKind::avatar)
        return unsupported("Set Motive Change caller is not an avatar");
    const auto delta_owner=instruction.operands[0];
    const auto max_owner=instruction.operands[1];
    const auto motive=instruction.operands[2];
    const auto flags=instruction.operands[3];
    const auto delta_data=read_i16(instruction.operands,4);
    const auto max_data=read_i16(instruction.operands,6);

    constexpr std::uint8_t clear_all=1U;
    constexpr std::uint8_t once=2U;
    if((flags&clear_all)!=0){
        if(!world.clear_motive_changes(frame.context.caller))
            return unsupported("Set Motive Change could not clear caller records");
        return {PrimitiveDisposition::goto_true,{}};
    }
    if((flags&~(clear_all|once))!=0)return unsupported("Set Motive Change has unknown flag bits");
    if(motive>15)return unsupported("Set Motive Change motive index is outside the 16-word bank");
    if(!memory.read)return unsupported("Set Motive Change variable reader is unavailable");

    const auto delta=memory.read(frame,static_cast<std::uint8_t>(delta_owner),delta_data);
    const auto maximum=memory.read(frame,static_cast<std::uint8_t>(max_owner),max_data);
    if(!delta||!maximum)return unsupported("Set Motive Change input variable is unavailable");

    if(static_cast<std::size_t>(motive)>=actor->motives.size())
        return unsupported("Set Motive Change caller has no matching motive bank");

    auto rate=static_cast<std::int32_t>(*delta);
    // Simitone's TS1 ScaleRate uses 180 / (24 - 16) for positive Energy.
    if(motive==5&&rate>0)rate*=22;
    const auto scaled_rate=wrap_i16(rate);
    const auto limit=*maximum; // TS1 GetLimit is 100, so ScaleMax is identity.
    if((flags&once)==0){
        if(!world.set_motive_change(frame.context.caller,motive,scaled_rate,limit))
            return unsupported("Set Motive Change could not install caller record");
        return {PrimitiveDisposition::goto_true,{}};
    }
    const auto current=actor->motives[static_cast<std::size_t>(motive)];

    if((scaled_rate>0&&current>limit)||(scaled_rate<0&&current<limit))
        return {PrimitiveDisposition::goto_true,{}};

    auto next=wrap_i16(static_cast<std::int32_t>(current)+scaled_rate);
    // The reference uses short compound assignment before checking the limit.
    // In its default unchecked C# context this narrows with signed-16 wrap.
    if((scaled_rate>0&&next>limit)||(scaled_rate<0&&next<limit))next=limit;
    if(!world.set_motive(frame.context.caller,motive,next))
        return unsupported("Set Motive Change caller rejected the motive write");
    return {PrimitiveDisposition::goto_true,{}};
}

VmPrimitive ts1_motive_change_primitive(World& world,VmMemory memory) {
    return [&world,memory=std::move(memory)](const VmFrame& frame,const Instruction& instruction,
        const std::array<std::int16_t,20>&,const VmDispatch&) {
        return ts1_motive_change(world,memory,frame,instruction);
    };
}

} // namespace odh
