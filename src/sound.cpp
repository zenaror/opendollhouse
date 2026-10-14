#include "odh/sound.hpp"
#include <algorithm>
#include <utility>

namespace odh {
std::optional<SoundSourceVariables> sound_source_variables(const Entity& entity, std::size_t field_limit) {
    SoundSourceVariables result{SoundVariableProfile::simitone_avatar,entity.generation,{}};
    if(entity.kind==EntityKind::avatar) {
        if(entity.person_data.size()<=65 || field_limit<29) return std::nullopt;
        result.fields.resize(29);
        const auto& data=entity.person_data;
        const auto age=data[58];
        result.fields[0]=(age<18 && age!=0) ? 2 : data[65];
        const std::array<std::pair<std::size_t,std::size_t>,4> skills{{{2,10},{5,15},{7,12},{11,17}}};
        for(const auto& entry:skills) result.fields[entry.first]=std::min(100,data[entry.second]/10);
        result.fields[14]=age;
        for(std::size_t n=0;n<6;++n) result.fields[15+n]=data[2+n]/10;
    } else if(entity.kind==EntityKind::object) {
        if(field_limit==0 || entity.attributes.size()>=field_limit) return std::nullopt;
        result.profile=SoundVariableProfile::simitone_object;
        result.fields.reserve(entity.attributes.size()+1);
        result.fields.push_back(0);
        for(const auto value:entity.attributes) result.fields.push_back(value);
    } else return std::nullopt;
    return result;
}

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
            {},!(flags&8),!(flags&4),bool(flags&1),{}};
        std::string status;
        const auto owner=world.find(request.source);
        if(!owner) status="missing_owner";
        else {
            request.source_variables=sound_source_variables(*owner);
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
