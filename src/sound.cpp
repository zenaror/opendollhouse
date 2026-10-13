#include "odh/sound.hpp"
#include <utility>

namespace odh {
SoundEvent read_fwav(const View& view, std::size_t text_limit) {
    std::string name;
    for(std::uint64_t offset=0; offset<view.size(); ++offset) {
        const auto byte=view.u8(offset);
        if(!byte) return {std::move(name),view.size()-offset-1};
        if(name.size()==text_limit) throw FormatError("FWAV event name exceeds text limit");
        name.push_back(static_cast<char>(byte));
    }
    throw FormatError("FWAV event name has no NUL terminator");
}

VmPrimitive sound_primitive(World& world, SoundResolver resolve, SoundSink sink,
    std::function<void(const SoundRequest&, const std::string&)> observed) {
    return [&world,resolve=std::move(resolve),sink=std::move(sink),observed=std::move(observed)]
        (const VmFrame& frame,const Instruction& i,const auto&,const VmDispatch&) -> PrimitiveOutcome {
        if(i.opcode!=23) return {};
        const auto flags=i.operands[4];
        if(flags&0xC0) return {PrimitiveDisposition::unsupported,"PlaySound unknown flag bits"};
        SoundRequest request{
            static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8)),
            static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8)),
            flags,i.operands[5],(flags&2) ? frame.context.stack_object : frame.context.caller,
            {},!(flags&8),!(flags&4),bool(flags&1)};
        std::string status;
        if(!world.find(request.source)) status="missing_owner";
        else {
            const auto event=resolve ? resolve(frame,request.event_id) : std::nullopt;
            if(!event) status="missing_resource";
            else {
                request.name=event->name;
                if(request.name=="piano_play") request.loop=true;
                status=!sink ? "skipped_no_backend" : sink(request) ? "submitted" : "backend_rejected";
            }
        }
        if(observed) observed(request,status);
        return {PrimitiveDisposition::goto_true,{}};
    };
}
}
