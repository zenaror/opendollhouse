#include "odh/sound.hpp"
#include "odh/playback.hpp"
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

std::optional<SoundSourceBinding> SoundSourceBinding::bind(const World& world,
    const SoundRequest& request, std::size_t limit) {
    if (!request.source_variables) return std::nullopt;
    SoundSourceBinding binding(world,request.source,request.source_variables->generation,
                               request.source_variables->profile,limit);
    if (!binding.refresh()) return std::nullopt;
    return binding;
}
bool SoundSourceBinding::valid() const {
    const auto entity = world_->find(source_);
    return entity && entity->generation == generation_;
}
std::optional<SoundSourceVariables> SoundSourceBinding::refresh() const {
    const auto entity = world_->find(source_);
    if (!entity || entity->generation != generation_) return std::nullopt;
    auto values = sound_source_variables(*entity,limit_);
    if (!values || values->profile != profile_) return std::nullopt;
    return values;
}
std::optional<std::int32_t> SoundSourceBinding::read(std::int32_t source, std::int32_t field) const {
    if (source != source_ || field < 0 || static_cast<std::uint64_t>(field) >= limit_)
        return std::nullopt;
    const auto values = refresh();
    if (!values || static_cast<std::size_t>(field) >= values->fields.size()) return std::nullopt;
    return values->fields[static_cast<std::size_t>(field)];
}

void animation_sound_event(World& world,const PlaybackEvent& event,SoundSink sink,
    std::function<void(const SoundRequest&,const std::string&)> observed) {
    const std::string* name=nullptr;
    for(const auto& pair:event.properties)if(pair.first=="sound"){name=&pair.second;break;}
    if(!name)return;
    SoundRequest request{0,0,0,0,event.actor,{},true,true,false,{}};
    std::string status;
    if(name->empty()||name->size()>256||name->find('\0')!=std::string::npos)status="invalid_event";
    else {
        request.name=*name;
        const auto* actor=world.find(event.actor);
        if(!actor)status="missing_owner";
        else if(actor->generation!=event.actor_generation)status="stale_owner";
        else {
            if(actor->kind==EntityKind::avatar)
                request.source_variables=sound_source_variables(*actor,29);
            if(!request.source_variables)status="missing_source_variables";
            else status=!sink?"skipped_no_backend":sink(request)?"submitted":"backend_rejected";
        }
    }
    if(observed)observed(request,status);
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
