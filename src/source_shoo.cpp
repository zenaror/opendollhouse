// SPDX-License-Identifier: MPL-2.0
// FreeSO VMRoutingFrame shoo and VMEntity.GetAction, revision7e76c196.
#include "odh/source_shoo.hpp"
#include <algorithm>
#include <set>

namespace odh {
namespace {
SourceShooPlanResult unsupported(const char* reason){return {SourceShooPlanStatus::unsupported,{},reason};}
SourceShooPlanResult absent(const char* reason){return {SourceShooPlanStatus::no_action,{},reason};}
bool valid_routine(const Bhav& routine){
    if(routine.version==0x8002)return routine.metadata.has_value();
    return (routine.version==0x8000||routine.version==0x8001)&&!routine.metadata;
}
}
std::optional<bool> source_queue_has_shoo(const std::vector<SourceShooQueueEntry>& entries){
    if(entries.size()>4096)return {};
    bool unknown=false;
    for(const auto& entry:entries){
        if(!entry.callee_known){unknown=true;continue;}
        if(!entry.callee_guid||*entry.callee_guid!=0x7c4)continue;
        if(!entry.interaction_number){unknown=true;continue;}
        if(*entry.interaction_number==3)return true;
    }
    return unknown?std::optional<bool>{}:std::optional<bool>{false};
}
SourceShooPlanResult plan_source_shoo(const SourceShooPlanInput& input){
    if(input.actor<=0||input.destination<=0||input.actor==input.destination||!input.owner)
        return unsupported("source shoo needs distinct positive participants and an authenticated resource owner");
    const VmContext context{input.actor,input.destination,input.destination,input.owner};
    if(input.mode==SourceShooMode::direct_route){
        if(!input.owner_program)return unsupported("source shoo private owner bank is unknown");
        const auto key=routine_key(4107);const auto& routines=input.owner_program->routines();
        const auto found=routines.find(key);
        if(found==routines.end())return unsupported("source direct shoo private4107 is absent");
        if(!valid_routine(found->second))return unsupported("source direct shoo routine metadata is malformed");
        const auto arguments=found->second.metadata?std::max<std::size_t>(4,found->second.metadata->arguments):4;
        std::vector<std::int16_t> defaults(arguments,-1);std::fill_n(defaults.begin(),4,0);
        SourceShooDirectPlan direct{{key,context,std::move(defaults),VmCallReturnMode::ordinary,true}};
        return {SourceShooPlanStatus::ready,SourceShooPlan{std::move(direct)},{}};
    }
    if(input.mode!=SourceShooMode::queued_interaction)return unsupported("unknown source shoo dispatch mode");
    if(!input.interactions)return unsupported("source shoo interaction table is unknown");
    const auto& table=*input.interactions;
    if(table.status==TtabStatus::empty){
        if(table.declared_count||!table.interactions.empty()||table.compressed)
            return unsupported("source empty interaction table is inconsistent");
        return absent("source interaction table is known empty");
    }
    if(table.status!=TtabStatus::decoded||!table.version||*table.version<4||*table.version>9||
       (table.compressed&&*table.version!=9)||table.declared_count!=table.interactions.size()||table.interactions.empty())
        return unsupported("source interaction table is incomplete or inconsistent");
    const Interaction* selected=nullptr;std::set<std::uint32_t> keys;
    for(const auto& row:table.interactions){
        if(!keys.insert(row.label_index).second)return unsupported("source interaction number is ambiguous");
        if(row.label_index==3)selected=&row;
    }
    if(!selected)return absent("source interaction number3 is absent");
    const auto resolve=[&](RoutineKey key,const Bhav*& routine)->bool{
        const auto* program=key.scope==RoutineScope::global?input.global_program:input.owner_program;
        if(!program)return false;
        const auto found=program->routines().find(key);routine=found==program->routines().end()?nullptr:&found->second;
        return true;
    };
    const auto action=routine_key(selected->action);const Bhav* body=nullptr;
    if(!resolve(action,body))return unsupported("source interaction action bank is unknown");
    if(!body)return absent("source interaction action is absent from its complete bank");
    if(!valid_routine(*body))return unsupported("source interaction action metadata is malformed");
    std::optional<RoutineKey> check;
    if(selected->test){
        check=routine_key(selected->test);
        if(!resolve(*check,body))return unsupported("source interaction check bank is unknown");
        if(!body)return absent("source nonzero interaction check is absent from its complete bank");
        if(!valid_routine(*body))return unsupported("source interaction check metadata is malformed");
    }
    SourceShooQueuedPlan queued;queued.action=action;queued.context=context;queued.check=check;queued.flags=selected->flags;
    return {SourceShooPlanStatus::ready,SourceShooPlan{std::move(queued)},{}};
}
} // namespace odh
