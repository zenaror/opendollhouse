// SPDX-License-Identifier: MPL-2.0
// Native adaptation of FreeSO TS1 inventory operations; see header for pinned provenance.
#include "odh/neighbor_inventory.hpp"
#include <algorithm>

namespace odh {
namespace {
std::int16_t word(std::uint64_t value) {
    const auto v=static_cast<std::uint32_t>(value&65535U);
    return static_cast<std::int16_t>(v<32768?v:static_cast<std::int32_t>(v)-65536);
}
}
NeighborInventory::NeighborInventory(const Ngbh& seed,NeighborInventoryLimits limits):limits_(limits) {
    restore({1,seed.inventory_by_id});changed_=false;
}
void NeighborInventory::validate(const NeighborInventorySnapshot& value) const {
    if(value.version!=1||value.inventory_by_id.size()>limits_.neighbors)
        throw FormatError("neighbor inventory version or neighbor bound exceeded");
    std::size_t total=0;
    for(const auto& [nid,items]:value.inventory_by_id){
        (void)nid;
        if(items.size()>limits_.items-total)throw FormatError("neighbor inventory item bound exceeded");
        total+=items.size();
    }
}
NeighborInventorySnapshot NeighborInventory::snapshot() const {return state_;}
void NeighborInventory::restore(const NeighborInventorySnapshot& value) {
    validate(value);auto candidate=value;
    std::size_t total=0;for(const auto& entry:candidate.inventory_by_id)total+=entry.second.size();
    state_=std::move(candidate);item_count_=total;changed_=true;
}
const std::vector<InventoryItem>* NeighborInventory::items(std::int16_t nid) const {
    auto found=state_.inventory_by_id.find(nid);return found==state_.inventory_by_id.end()?nullptr:&found->second;
}
std::vector<InventoryItem>& NeighborInventory::ensure(std::int16_t nid) {
    auto found=state_.inventory_by_id.find(nid);
    if(found!=state_.inventory_by_id.end())return found->second;
    if(state_.inventory_by_id.size()>=limits_.neighbors)throw FormatError("neighbor inventory neighbor bound exceeded");
    return state_.inventory_by_id.emplace(nid,std::vector<InventoryItem>{}).first->second;
}
void NeighborInventory::initialize(std::int16_t nid) {
    if(!items(nid)){ensure(nid);changed_=true;}
}
void NeighborInventory::check_append() const {
    if(item_count_>=limits_.items)throw FormatError("neighbor inventory item bound exceeded");
}
std::optional<std::uint16_t> NeighborInventory::get_data(std::int16_t nid,std::uint32_t key) const {
    if(auto list=items(nid))for(const auto& item:*list)if(item.type==2&&item.guid==key)return item.count;
    return {};
}
void NeighborInventory::set_data(std::int16_t nid,std::uint32_t key,std::uint16_t value) {
    if(auto list=items(nid))for(std::size_t i=0;i<list->size();++i)if((*list)[i].type==2&&(*list)[i].guid==key){
        state_.inventory_by_id.at(nid)[i].count=value;changed_=true;return;
    }
    check_append();auto& list=ensure(nid);list.push_back({2,key,value});++item_count_;changed_=true;
}
std::optional<std::uint16_t> NeighborInventory::consume_data(std::int16_t nid,std::uint32_t key) {
    if(!items(nid))return {};
    auto& list=state_.inventory_by_id.at(nid);
    for(auto it=list.begin();it!=list.end();++it)if(it->type==2&&it->guid==key){
        auto value=it->count;list.erase(it);--item_count_;changed_=true;return value;
    }
    return {};
}
PrimitiveOutcome NeighborInventory::execute(std::int16_t nid,const Instruction& i,const Ts1Temps& temps,std::uint32_t guid) {
    if(i.opcode!=51)return {};
    const auto mode=i.operands[0],type=i.operands[1],flags=i.operands[2];
    const auto index_temp=static_cast<std::uint8_t>(i.operands[3]&3U);
    const auto count_temp=static_cast<std::uint8_t>((flags>>2)&3U);
    const auto index=temps[index_temp];
    std::int32_t count=(flags&2U)?temps[0]:1;
    PrimitiveOutcome result{PrimitiveDisposition::goto_false,{}};
    if(mode>6)return {PrimitiveDisposition::unsupported,"TS1 inventory mode is outside the pinned implemented0..6 profile"};
    auto found=state_.inventory_by_id.find(nid);
    if(mode==0||mode==5||mode==6){
        // Check capacity before creating a new empty inventory, so failure is atomic.
        const auto key=mode==0?guid:static_cast<std::uint32_t>(mode-5);
        const auto match_type=mode==0?static_cast<std::int32_t>(type):2;
        if(found!=state_.inventory_by_id.end())for(auto& item:found->second)if(item.guid==key&&item.type==match_type){
            item.count=mode==0?static_cast<std::uint16_t>(item.count+static_cast<std::uint16_t>(count)):static_cast<std::uint16_t>(count);
            changed_=true;return {PrimitiveDisposition::goto_true,{}};
        }
        check_append();auto& list=ensure(nid);
        list.push_back({match_type,mode==0?guid:static_cast<std::uint32_t>(mode+5),
            static_cast<std::uint16_t>(mode==0?count:temps[0])});
        ++item_count_;changed_=true;return {PrimitiveDisposition::goto_true,{}};
    }
    if(found==state_.inventory_by_id.end())return result;
    auto& list=found->second;
    if(mode==4){
        std::uint64_t sum=0;std::optional<std::size_t> next;
        for(std::size_t n=0;n<list.size();++n)if(list[n].type==type){
            sum+=list[n].count;
            if(sum>2147483647U)return {PrimitiveDisposition::unsupported,"pinned inventory type sum exceeds signed32 LINQ bound"};
            if(!next&&static_cast<std::int64_t>(n)>index)next=n;
        }
        result.temp_write=VmTempWrite{count_temp,word(sum)};
        if(next){result.disposition=PrimitiveDisposition::goto_true;
            if(flags&128U)result.second_temp_write=VmTempWrite{index_temp,word(*next)};}
        return result;
    }
    std::size_t n=0;
    if(mode==2){if(index<0||static_cast<std::size_t>(index)>=list.size())return result;n=static_cast<std::size_t>(index);}
    else{
        for(;n<list.size();++n)if(list[n].guid==guid&&(type==0||list[n].type==type))break;
        if(n==list.size())return result;
    }
    auto& item=list[n];
    if(mode==3){
        const auto value=word(item.count);result.temp_write=VmTempWrite{count_temp,value};
        if(flags&16U)result.second_temp_write=VmTempWrite{index_temp,word(n)};
        if(value>0)result.disposition=PrimitiveDisposition::goto_true;
        return result;
    }
    if(mode==1&&static_cast<std::int32_t>(item.count)<count)return result;
    if(count==-1)count=item.count;
    item.count=static_cast<std::uint16_t>(item.count-static_cast<std::uint16_t>(count));
    if(item.count==0){list.erase(list.begin()+static_cast<std::ptrdiff_t>(n));--item_count_;}
    changed_=true;result.disposition=PrimitiveDisposition::goto_true;
    if(mode==2&&(flags&128U))result.temp_write=VmTempWrite{index_temp,word(static_cast<std::int32_t>(index)-1)};
    return result;
}
VmPrimitive neighbor_inventory_primitive(NeighborInventory& source,const World& world,
    std::function<void(const InventoryFind&)> observed) {
    return [&source,&world,observed=std::move(observed)](const VmFrame& frame,const Instruction& i,const Ts1Temps& temps,const VmDispatch&)->PrimitiveOutcome {
        if(i.opcode!=51)return {};
        if(i.operands[0]>6)return {PrimitiveDisposition::unsupported,"TS1 inventory mode is outside the pinned implemented0..6 profile"};
        const auto target_id=(i.operands[3]&32U)?temps[4]:frame.context.caller;
        const auto* target=world.find(target_id);
        if(!target||target->kind!=EntityKind::avatar||target->person_data.size()<=31)
            return {PrimitiveDisposition::unsupported,"inventory target is not an initialized avatar"};
        const auto nid=target->person_data[31];
        auto guid=std::uint32_t{i.operands[4]}|(std::uint32_t{i.operands[5]}<<8)|
            (std::uint32_t{i.operands[6]}<<16)|(std::uint32_t{i.operands[7]}<<24);
        if(!guid){const auto* stack=world.frame_entity(frame,VmEntityRole::stack_object);
            if(!stack||!stack->definition)return {PrimitiveDisposition::unsupported,"inventory StackObject GUID unavailable"};
            guid=stack->definition->guid;
        }
        try {
            auto result=source.execute(nid,i,temps,guid);
            if(observed&&i.operands[0]==3){
                std::optional<std::size_t> index;std::optional<std::int16_t> count;
                if(auto list=source.items(nid))for(std::size_t n=0;n<list->size();++n)
                    if((*list)[n].guid==guid&&(i.operands[1]==0||(*list)[n].type==i.operands[1])){index=n;count=word((*list)[n].count);break;}
                observed({frame.context,target_id,nid,guid,i.operands[1],static_cast<std::uint8_t>((i.operands[2]>>2)&3U),
                    static_cast<std::uint8_t>(i.operands[3]&3U),(i.operands[2]&16U)!=0,
                    result.disposition==PrimitiveDisposition::goto_true,index,count});
            }
            return result;
        }catch(const FormatError& e){return {PrimitiveDisposition::unsupported,e.what()};}
    };
}
}
