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
VmPrimitive entity_next_primitive(const World& world,VmMemory memory,std::function<void(const EntityNext&)> observed,
    std::vector<std::vector<std::int16_t>> multitile_groups){
    return [&world,memory=std::move(memory),observed=std::move(observed),
        multitile_groups=std::move(multitile_groups)](const VmFrame& frame,
        const Instruction& i,const auto& temps,const VmDispatch&)->PrimitiveOutcome {
        const auto mode=static_cast<std::uint8_t>(i.operands[4]&127);
        if(i.opcode!=31||(mode!=0&&mode!=1&&mode!=2&&mode!=3&&mode!=4&&mode!=8&&mode!=12))return {};
        const auto guid=std::uint32_t{i.operands[0]}|(std::uint32_t{i.operands[1]}<<8)|
            (std::uint32_t{i.operands[2]}<<16)|(std::uint32_t{i.operands[3]}<<24);
        const auto scope=static_cast<std::uint8_t>((i.operands[4]&128)?i.operands[5]:10);
        const auto index=static_cast<std::int16_t>((i.operands[4]&128)?i.operands[7]:0);
        const auto current=read_target(frame,temps,memory,scope,index);
        if(!current)return {PrimitiveDisposition::unsupported,"entity enumeration target unavailable"};
        if(mode==12){
            const auto* cursor=world.find(*current);
            if(!cursor||cursor->kind!=EntityKind::avatar)
                return {PrimitiveDisposition::unsupported,"family-member cursor is not a live avatar"};
        } else if(mode==8){
            const auto* anchor=world.find(*current);
            if(!anchor||!anchor->position||anchor->position->x<0||anchor->position->y<0||
               anchor->position->level<1||anchor->position->level>2)
                return {PrimitiveDisposition::unsupported,"same-tile entity cursor is outside the positioned lot profile"};
        }
        std::optional<std::int16_t> result;
        if(mode==3){
            const auto group=std::find_if(multitile_groups.begin(),multitile_groups.end(),[&](const auto& members){
                return std::find(members.begin(),members.end(),*current)!=members.end();
            });
            if(group!=multitile_groups.end()){
                for(const auto member:*group)
                    if(member>*current&&(!result||member<*result))result=member;
                if(!result&&!group->empty())result=*std::min_element(group->begin(),group->end());
            }
        } else if(mode==8)result=world.next_entity_on_same_tile(*current);
        else if(mode==12)result=world.next_family_member(*current);
        else if(mode==4)result=world.next_entity_of_guid(*current,guid);
        else result=
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
