#include "odh/inventory_find.hpp"

namespace odh {
namespace {
std::int16_t word(std::uint64_t value) {
    const auto low=static_cast<std::uint32_t>(value&0xffffU);
    return static_cast<std::int16_t>(low<32768U?static_cast<std::int32_t>(low):static_cast<std::int32_t>(low)-65536);
}
}
VmPrimitive inventory_find_primitive(const Ngbh& source,const World& world,
    std::function<void(const InventoryFind&)> observed) {
    return [&source,&world,observed=std::move(observed)](const VmFrame& frame,
        const Instruction& i,const auto& temps,const VmDispatch&)->PrimitiveOutcome {
        if(i.opcode!=51||i.operands[0]!=3)return {};
        const auto target_id=(i.operands[3]&32U)?temps[4]:frame.context.caller;
        const auto* target=world.find(target_id);
        if(!target||target->kind!=EntityKind::avatar||target->person_data.size()<=31)
            return {PrimitiveDisposition::unsupported,"inventory target is not an initialized avatar"};
        const auto neighbor=target->person_data[31];
        auto guid=std::uint32_t{i.operands[4]}|(std::uint32_t{i.operands[5]}<<8)|
            (std::uint32_t{i.operands[6]}<<16)|(std::uint32_t{i.operands[7]}<<24);
        if(guid==0){
            const auto* stack=world.frame_entity(frame,VmEntityRole::stack_object);
            if(!stack||!stack->definition)
                return {PrimitiveDisposition::unsupported,"inventory StackObject GUID unavailable"};
            guid=stack->definition->guid;
        }
        const auto type=i.operands[1];
        const auto count_temp=static_cast<std::uint8_t>((i.operands[2]>>2)&3U);
        const auto index_temp=static_cast<std::uint8_t>(i.operands[3]&3U);
        const bool write_index=(i.operands[2]&16U)!=0;
        std::optional<std::size_t> found;
        std::optional<std::int16_t> count;
        const auto inventory=source.inventory_by_id.find(neighbor);
        if(inventory!=source.inventory_by_id.end()){
            const auto& items=inventory->second;
            for(std::size_t n=0;n<items.size();++n){
                if(items[n].guid==guid&&(type==0||items[n].type==type)){
                    found=n;count=word(items[n].count);break;
                }
            }
        }
        const bool success=count&&*count>0;
        PrimitiveOutcome result{success?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
        if(found){
            result.temp_write=VmTempWrite{count_temp,*count};
            if(write_index)result.second_temp_write=VmTempWrite{index_temp,word(*found)};
        }
        if(observed)observed(InventoryFind{frame.context,target_id,neighbor,guid,type,count_temp,index_temp,
            write_index,success,found,count});
        return result;
    };
}
}
