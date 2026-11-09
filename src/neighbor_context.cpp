#include "odh/neighbor_context.hpp"
#include "odh/ts1_character_family.hpp"
#include <limits>
#include <sstream>
#include "odh/world.hpp"
#include "odh/native_personal_state.hpp"
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
    if(active!=active_.end()&&personal_)if(const auto* changed=personal_->by_id(id))return changed;
    if(const auto owned=created_.find(id);owned!=created_.end())return &owned->second;
    return active==active_.end()?nullptr:&*source_.entries[ordered_[active->second].serialized_index].record;
}
void NeighborContext::admit_created_character(const Ts1CreatedCharacter& character){
    validate_ts1_created_character(character);
    const auto& seed=character.seed;const auto existing=active_.find(seed.neighbor_id);
    const auto* old=existing==active_.end()?nullptr:(created_.count(seed.neighbor_id)?&created_.at(seed.neighbor_id):&*source_.entries[ordered_[existing->second].serialized_index].record);
    const auto same=[](const NeighborRecord& a,const NeighborRecord& b){return encode_nbrs(Nbrs{0,73,{{1,a}},0})==encode_nbrs(Nbrs{0,73,{{1,b}},0});};
    if(old&&!same(*old,seed))throw FormatError("created neighbor ID conflicts with immutable or owned seed");
    for(const auto& row:ordered_)if(row.id!=seed.neighbor_id){const auto* n=by_id(row.id);if(n&&n->guid==seed.guid)throw FormatError("created neighbor GUID already belongs to another record");}
    if(created_.count(seed.neighbor_id))return;
    if(created_.size()>=1024||(!old&&active_.size()>=4096))throw FormatError("owned neighbor creation budget exceeded");
    // Stage index storage before inserting the stable owned node.
    auto order=ordered_;if(!old)order.push_back({seed.neighbor_id,std::numeric_limits<std::size_t>::max()});
    std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    auto active=active_;for(std::size_t i=0;i<order.size();++i)active[order[i].id]=i;
    auto guids=guids_,owned_guids=created_guids_;guids[seed.guid]=seed.neighbor_id;owned_guids[seed.guid]=seed.neighbor_id;
    created_.emplace(seed.neighbor_id,seed);ordered_.swap(order);active_.swap(active);guids_.swap(guids);created_guids_.swap(owned_guids);
}
std::optional<std::int16_t> NeighborContext::instance_id(const World& world,std::int16_t neighbor) const {
    const auto* record=by_id(neighbor);
    if(!record)return std::int16_t{0};
    auto cursor=std::int16_t{-1};
    while(const auto id=world.next_entity(cursor,EntityKind::avatar)) {
        cursor=*id;
        const auto* avatar=world.find(*id);
        if(!avatar || !avatar->definition)return {};
        if(avatar->definition->guid==record->guid)return *id;
    }
    return std::int16_t{0};
}
void NeighborContext::bind_native_personal_catalog(const NativePersonalCatalog& catalog){
    validate_native_personal_catalog(catalog);std::map<std::uint32_t,std::int16_t> mapped;
    for(std::size_t i=0;i<catalog.definitions.size();++i){const auto& d=catalog.definitions[i];const auto& b=catalog.bindings[i];const auto* n=by_id(b.neighbor_id);
        if(!n||n->guid!=b.guid)throw FormatError("original GUID catalog differs from the hydrated neighbor source");
        if(d.object_type==2)mapped.emplace(b.guid,b.neighbor_id);
    }
    original_guids_=std::move(mapped);
}
std::optional<std::int16_t> NeighborContext::id_for_guid(std::uint32_t guid) const {
    if(original_guids_){const auto i=original_guids_->find(guid);if(i!=original_guids_->end())return i->second;
        const auto owned=created_guids_.find(guid);return owned==created_guids_.end()?std::nullopt:std::optional<std::int16_t>{owned->second};}
    const auto found=guids_.find(guid);return found==guids_.end()?std::nullopt:std::optional<std::int16_t>{found->second};
}
std::int16_t NeighborContext::next(std::int16_t current) const {
    const auto active=active_.find(current);
    if(active!=active_.end())return active->second+1<ordered_.size()?ordered_[active->second+1].id:std::int16_t{-1};
    const auto next=std::upper_bound(ordered_.begin(),ordered_.end(),current,[](auto id,const auto& entry){return id<entry.id;});
    return next==ordered_.end()?std::int16_t{-1}:next->id;
}
std::int16_t NeighborContext::next_of_guid(std::int16_t current,std::uint32_t guid) const {
    const auto next=std::upper_bound(ordered_.begin(),ordered_.end(),current,
        [](auto id,const auto& entry){return id<entry.id;});
    for(auto candidate=next;candidate!=ordered_.end();++candidate){
        const auto* record=by_id(candidate->id);
        if(record&&record->guid==guid)return candidate->id;
    }
    return -1;
}
std::optional<NeighborDataValue> NeighborContext::person_data(std::int16_t neighbor,std::int16_t index) const {
    const auto active=active_.find(neighbor);
    if(active==active_.end())return NeighborDataValue{neighbor,index,0,NeighborDataSource::missing_neighbor_zero};
    const auto& record=*by_id(neighbor);
    if(record.native_personal&&index>=0&&static_cast<std::size_t>(index)<record.native_personal->present.size()&&
       record.native_personal->present[static_cast<std::size_t>(index)])
        return NeighborDataValue{neighbor,index,record.native_personal->values[static_cast<std::size_t>(index)],NeighborDataSource::serialized};
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
    const auto resolved=resolve_ts1_variable_address(scope,index,f,temps);
    if(!resolved)return {};
    scope=resolved->scope;index=resolved->index;
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
        if(i.opcode!=31)return {};
        const auto mode=static_cast<std::uint8_t>(i.operands[4]&127);
        if(mode!=5&&mode!=7)return {};
        const auto scope=static_cast<std::uint8_t>((i.operands[4]&128)?i.operands[5]:10);
        const auto index=static_cast<std::int16_t>((i.operands[4]&128)?i.operands[7]:0);
        const auto current=target(frame,temps,memory,scope,index);
        if(!current)return {PrimitiveDisposition::unsupported,"neighbor enumeration target unavailable"};
        const auto guid=static_cast<std::uint32_t>(i.operands[0])|
            (static_cast<std::uint32_t>(i.operands[1])<<8)|
            (static_cast<std::uint32_t>(i.operands[2])<<16)|
            (static_cast<std::uint32_t>(i.operands[3])<<24);
        const auto result=mode==7?next_of_guid(*current,guid):next(*current);
        PrimitiveOutcome outcome{result<0?PrimitiveDisposition::goto_false:PrimitiveDisposition::goto_true,{}};
        if(result>=0){const auto output=resolve_ts1_variable_address(scope,index,frame,temps);
            if(!output)return {PrimitiveDisposition::unsupported,"enumeration indirect output unavailable"};
            switch(output->scope){
            case 7:return {PrimitiveDisposition::unsupported,"neighbor enumeration literal target is not writable"};
            case 8:outcome.temp_write=VmTempWrite{static_cast<std::size_t>(output->index),result};break;
            case 9:outcome.write=VmFrameWrite{VmFrameBank::arguments,static_cast<std::size_t>(output->index),result};break;
            case 10:outcome.stack_object_write=VmStackObjectWrite{result};break;
            case 25:outcome.write=VmFrameWrite{VmFrameBank::locals,static_cast<std::size_t>(output->index),result};break;
            default:{const auto address=resolve_ts1_variable_address(scope,index,frame,temps);
                if(!address||!memory.write||!memory.write(frame,address->scope,address->index,result))
                    return {PrimitiveDisposition::unsupported,"neighbor enumeration target rejected write"};}
        }}
        if(observed)observed(NeighborNext{*current,result,result>=0});
        return outcome;
    };
}
}
