#include "odh/object_type.hpp"
namespace odh {
namespace {
std::int16_t signed_word(std::uint16_t v){return static_cast<std::int16_t>(v<32768?static_cast<std::int32_t>(v):static_cast<std::int32_t>(v)-65536);}
}
VmPrimitive object_type_primitive(const World& world) {
    return [&world](const VmFrame& frame,const Instruction& instruction,const auto& temps,const auto&)->PrimitiveOutcome {
        if(instruction.opcode!=32)return {};
        const auto& b=instruction.operands;
        const auto guid=static_cast<std::uint32_t>(b[0])|(static_cast<std::uint32_t>(b[1])<<8)|
                        (static_cast<std::uint32_t>(b[2])<<16)|(static_cast<std::uint32_t>(b[3])<<24);
        const auto index=signed_word(static_cast<std::uint16_t>(b[4]|(b[5]<<8)));
        std::optional<std::int16_t> id;
        switch(b[6]) {
        case 7:id=index;break;
        case 8:if(index>=0&&static_cast<std::size_t>(index)<temps.size())id=temps[static_cast<std::size_t>(index)];break;
        case 9:if(index>=0&&static_cast<std::size_t>(index)<frame.arguments.size())id=frame.arguments[static_cast<std::size_t>(index)];break;
        case 10:id=frame.context.stack_object;break;
        case 25:if(index>=0&&static_cast<std::size_t>(index)<frame.locals.size())id=frame.locals[static_cast<std::size_t>(index)];break;
        default:id=world.read(frame,b[6],index);break;
        }
        if(!id)return {PrimitiveDisposition::unsupported,"object type source scope/index unavailable"};
        // StackObjectID is the frame's object pointer in the reference engine,
        // not another numeric-ID lookup. Preserve its generation and retained
        // body across Delete and later reuse of the same numeric ID.
        const auto* entity=b[6]==10?world.frame_entity(frame,VmEntityRole::stack_object):world.find(*id);
        const bool known_absence=vm_context_references_valid(frame.context)&&
            (b[6]!=10||!frame.context.entity_references_bound||frame.context.stack_object_generation==0);
        if(!entity)return {known_absence?PrimitiveDisposition::source_error:PrimitiveDisposition::unsupported,
            "object type selected entity absent (scope="+
            std::to_string(b[6])+", index="+std::to_string(index)+", selected="+std::to_string(*id)+
            ", bound="+std::to_string(frame.context.entity_references_bound)+
            ", stack_generation="+std::to_string(frame.context.stack_object_generation)+
            ", routine="+std::to_string(frame.key.id)+", pc="+std::to_string(frame.pc)+
            ", owner="+std::to_string(frame.context.code_owner)+")"};
        if(!entity->definition)return {PrimitiveDisposition::unsupported,"object type identity not supplied"};
        const auto match=entity->definition->guid==guid || (entity->definition->master_guid&&*entity->definition->master_guid==guid);
        return {match?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
    };
}
}
