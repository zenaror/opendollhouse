#include "odh/neighbor_context.hpp"
#include <algorithm>
namespace odh {
NeighborContext::NeighborContext(Nbrs source):source_(std::move(source)) {
    if(source_.entries.size()>4096)throw FormatError("neighbor runtime exceeds bounded4096 entries");
    std::size_t names=65536,relations=16384,values=65536;
    ordered_.reserve(source_.entries.size());
    for(std::size_t n=0;n<source_.entries.size();++n){const auto& entry=source_.entries[n];
        if(entry.marker==0){if(entry.record)throw FormatError("neighbor placeholder carries a record");ordered_.push_back({0,n});continue;}
        if(entry.marker!=1||!entry.record)throw FormatError("neighbor runtime record marker unsupported");
        const auto& record=*entry.record;
        if(record.version!=4&&record.version!=10)throw FormatError("neighbor runtime record version unsupported");
        if((record.person_mode>0)!=record.person_data.has_value()||
           (record.person_data&&record.person_data->size()!=(record.version==4?80U:256U)))
            throw FormatError("neighbor runtime person bank inconsistent with record profile");
        if(record.name.size()>256||record.name.size()>names||record.relationships.size()>relations)
            throw FormatError("neighbor runtime text/relationship budget exceeded");
        names-=record.name.size();relations-=record.relationships.size();
        for(const auto& rel:record.relationships){if(rel.values.size()>values)throw FormatError("neighbor runtime relationship value budget exceeded");values-=rel.values.size();}
        if(!active_.emplace(record.neighbor_id,n).second)throw FormatError("ambiguous active neighbor ID");
        guids_[record.guid]=record.neighbor_id;
        ordered_.push_back({record.neighbor_id,n});
    }
    std::stable_sort(ordered_.begin(),ordered_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(std::size_t n=0;n<ordered_.size();++n)if(source_.entries[ordered_[n].serialized_index].record)active_.at(ordered_[n].id)=n;
}
const NeighborRecord* NeighborContext::by_id(std::int16_t id) const {
    const auto active=active_.find(id);
    return active==active_.end()?nullptr:&*source_.entries[ordered_[active->second].serialized_index].record;
}
std::optional<std::int16_t> NeighborContext::id_for_guid(std::uint32_t guid) const {
    const auto found=guids_.find(guid);return found==guids_.end()?std::nullopt:std::optional<std::int16_t>{found->second};
}
std::int16_t NeighborContext::next(std::int16_t current) const {
    const auto active=active_.find(current);
    if(active!=active_.end())return active->second+1<ordered_.size()?ordered_[active->second+1].id:std::int16_t{-1};
    const auto next=std::upper_bound(ordered_.begin(),ordered_.end(),current,[](auto id,const auto& entry){return id<entry.id;});
    return next==ordered_.end()?std::int16_t{-1}:next->id;
}
std::optional<NeighborDataValue> NeighborContext::person_data(std::int16_t neighbor,std::int16_t index) const {
    const auto active=active_.find(neighbor);
    if(active==active_.end())return NeighborDataValue{neighbor,index,0,NeighborDataSource::missing_neighbor_zero};
    const auto& record=*source_.entries[ordered_[active->second].serialized_index].record;
    if(!record.person_data)return NeighborDataValue{neighbor,index,0,NeighborDataSource::absent_person_data_zero};
    // Reference stores exactly88 runtime words, zero-extending the old80-word
    // layout and retaining only the first88 of the new256-word serialized bank.
    if(index<0||index>=88)return {};
    if(static_cast<std::size_t>(index)>=record.person_data->size())
        return NeighborDataValue{neighbor,index,0,NeighborDataSource::legacy_zero_extension};
    return NeighborDataValue{neighbor,index,(*record.person_data)[static_cast<std::size_t>(index)],NeighborDataSource::serialized};
}
namespace {
bool valid(std::int16_t index,std::size_t size){return index>=0&&static_cast<std::size_t>(index)<size;}
std::optional<std::int16_t> target(const VmFrame& f,const std::array<std::int16_t,20>& temps,const VmMemory& memory,std::uint8_t scope,std::int16_t index){
    switch(scope){
    case 7:return index;
    case 8:return valid(index,temps.size())?std::optional<std::int16_t>{temps[static_cast<std::size_t>(index)]}:std::nullopt;
    case 9:return valid(index,f.arguments.size())?std::optional<std::int16_t>{f.arguments[static_cast<std::size_t>(index)]}:std::nullopt;
    case 10:return f.context.stack_object;
    case 25:return valid(index,f.locals.size())?std::optional<std::int16_t>{f.locals[static_cast<std::size_t>(index)]}:std::nullopt;
    default:
        if(const auto address=resolve_person_data_address(scope,index,temps);address&&memory.read)return memory.read(f,address->scope,address->index);
        return {};
    }
}
}
VmPrimitive NeighborContext::primitive(VmMemory memory,std::function<void(const NeighborNext&)> observed) const {
    return [this,memory=std::move(memory),observed=std::move(observed)](const VmFrame& frame,const Instruction& i,const auto& temps,const VmDispatch&)->PrimitiveOutcome {
        if(i.opcode!=31||(i.operands[4]&127)!=5)return {};
        const auto scope=static_cast<std::uint8_t>((i.operands[4]&128)?i.operands[5]:10);
        const auto index=static_cast<std::int16_t>((i.operands[4]&128)?i.operands[7]:0);
        const auto current=target(frame,temps,memory,scope,index);
        if(!current)return {PrimitiveDisposition::unsupported,"neighbor enumeration target unavailable"};
        const auto result=next(*current);
        PrimitiveOutcome outcome{result<0?PrimitiveDisposition::goto_false:PrimitiveDisposition::goto_true,{}};
        if(result>=0){switch(scope){
            case 7:return {PrimitiveDisposition::unsupported,"neighbor enumeration literal target is not writable"};
            case 8:outcome.temp_write=VmTempWrite{static_cast<std::size_t>(index),result};break;
            case 9:outcome.write=VmFrameWrite{VmFrameBank::arguments,static_cast<std::size_t>(index),result};break;
            case 10:outcome.stack_object_write=VmStackObjectWrite{result};break;
            case 25:outcome.write=VmFrameWrite{VmFrameBank::locals,static_cast<std::size_t>(index),result};break;
            default:{const auto address=resolve_person_data_address(scope,index,temps);
                if(!address||!memory.write||!memory.write(frame,address->scope,address->index,result))
                    return {PrimitiveDisposition::unsupported,"neighbor enumeration target rejected write"};}
        }}
        if(observed)observed(NeighborNext{*current,result,result>=0});
        return outcome;
    };
}
}
