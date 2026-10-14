#include "odh/entity_next.hpp"
namespace odh {
namespace {
bool valid(std::int16_t index,std::size_t size){return index>=0&&static_cast<std::size_t>(index)<size;}
std::optional<std::int16_t> read_target(const VmFrame& f,const std::array<std::int16_t,20>& temps,
    const VmMemory& memory,std::uint8_t scope,std::int16_t index){
    switch(scope){
    case 7:return index;
    case 8:return valid(index,temps.size())?std::optional<std::int16_t>{temps[static_cast<std::size_t>(index)]}:std::nullopt;
    case 9:return valid(index,f.arguments.size())?std::optional<std::int16_t>{f.arguments[static_cast<std::size_t>(index)]}:std::nullopt;
    case 10:return f.context.stack_object;
    case 25:return valid(index,f.locals.size())?std::optional<std::int16_t>{f.locals[static_cast<std::size_t>(index)]}:std::nullopt;
    default:
        if(const auto address=resolve_person_data_address(scope,index,temps);address&&memory.read)
            return memory.read(f,address->scope,address->index);
        return {};
    }
}
}
VmPrimitive entity_next_primitive(const World& world,VmMemory memory,std::function<void(const EntityNext&)> observed){
    return [&world,memory=std::move(memory),observed=std::move(observed)](const VmFrame& frame,
        const Instruction& i,const auto& temps,const VmDispatch&)->PrimitiveOutcome {
        const auto mode=static_cast<std::uint8_t>(i.operands[4]&127);
        if(i.opcode!=31||(mode!=0&&mode!=1&&mode!=2&&mode!=4))return {};
        const auto guid=std::uint32_t{i.operands[0]}|(std::uint32_t{i.operands[1]}<<8)|
            (std::uint32_t{i.operands[2]}<<16)|(std::uint32_t{i.operands[3]}<<24);
        const auto scope=static_cast<std::uint8_t>((i.operands[4]&128)?i.operands[5]:10);
        const auto index=static_cast<std::int16_t>((i.operands[4]&128)?i.operands[7]:0);
        const auto current=read_target(frame,temps,memory,scope,index);
        if(!current)return {PrimitiveDisposition::unsupported,"entity enumeration target unavailable"};
        const auto result=mode==4?world.next_entity_of_guid(*current,guid):
            world.next_entity(*current,mode==1?std::optional<EntityKind>{EntityKind::avatar}:
                mode==2?std::optional<EntityKind>{EntityKind::object}:std::nullopt);
        PrimitiveOutcome outcome{result?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
        if(result){switch(scope){
        case 7:return {PrimitiveDisposition::unsupported,"entity enumeration literal target is not writable"};
        case 8:outcome.temp_write=VmTempWrite{static_cast<std::size_t>(index),*result};break;
        case 9:outcome.write=VmFrameWrite{VmFrameBank::arguments,static_cast<std::size_t>(index),*result};break;
        case 10:outcome.stack_object_write=VmStackObjectWrite{*result};break;
        case 25:outcome.write=VmFrameWrite{VmFrameBank::locals,static_cast<std::size_t>(index),*result};break;
        default:{const auto address=resolve_person_data_address(scope,index,temps);
            if(!address||!memory.write||!memory.write(frame,address->scope,address->index,*result))
                return {PrimitiveDisposition::unsupported,"entity enumeration target rejected write"};}
        }}
        if(observed)observed(EntityNext{frame.context,guid,*current,result,result.has_value(),scope,index,mode});
        return outcome;
    };
}
}
