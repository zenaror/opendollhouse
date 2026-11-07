// SPDX-License-Identifier: MPL-2.0
#include "phonebook_choices.hpp"
#include <set>
#include <algorithm>
namespace odh_host {
PhonebookChoices phonebook_choices(const odh::World& world,odh::WorldEntityReference identity,
    const odh::NeighborContext& neighbors,const odh::RelationshipState& relationships,
    const PhonebookDisplayName& display_name,std::size_t max_choices,std::size_t max_bytes){
    const auto refuse=[](const char* reason){return PhonebookChoices{false,{},reason};};
    if(max_choices>4096||max_bytes>1048576)return refuse("phonebook budget exceeds dialog bounds");
    const auto* caller=world.resolve_reference(identity);
    if(!caller||caller->dead||caller->ui_ghost||caller->kind!=odh::EntityKind::avatar||
       !caller->definition||caller->person_data.size()<=31)
        return refuse("phonebook caller identity or neighbor field unavailable");
    const auto nid=caller->person_data[31];const auto* owner=neighbors.by_id(nid);
    if(!owner||owner->guid!=caller->definition->guid)
        return refuse("phonebook caller differs from current neighbor authority");
    const auto relation_state=relationships.snapshot();
    const auto source=std::find_if(relation_state.sources.begin(),relation_state.sources.end(),[&](const auto& row){
        return row.matrix==odh::RelationshipMatrix::neighbor&&row.id==nid&&row.generation==0;});
    if(source==relation_state.sources.end())return refuse("phonebook caller relationship authority unavailable");
    PhonebookChoices result;std::set<std::int16_t> emitted;std::size_t bytes=0;
    for(const auto& pair:source->pairs){
        // The reference explicitly casts the relationship key to signed short.
        const auto word=static_cast<std::uint32_t>(pair.target)&65535U;
        const auto target=static_cast<std::int16_t>(word<=32767U?static_cast<std::int32_t>(word):static_cast<std::int32_t>(word)-65536);
        const auto* neighbor=neighbors.by_id(target);
        if(!neighbor)return refuse("phonebook relationship references a missing current neighbor");
        const auto family=neighbor->person_data?neighbor->person_data->at(61):0;
        const auto gender=neighbor->person_data?neighbor->person_data->at(65):0;
        if(!family||gender>=2)continue;
        // Source UI returns positive NIDs for calls and -1 for its separate
        // Cancel button. Do not reinterpret malformed selected IDs as Cancel.
        if(target<=0)return refuse("phonebook eligible neighbor has no positive selectable ID");
        if(!emitted.insert(target).second)return refuse("phonebook relationship keys alias one selectable neighbor");
        if(result.choices.size()>=max_choices)return refuse("phonebook choice budget exhausted");
        if(!display_name)return refuse("phonebook character display-name provider unavailable");
        const auto name=display_name(target,neighbor->guid);
        if(!name||name->empty()||name->size()>4096||name->find('\0')!=std::string::npos)
            return refuse("phonebook admitted character display name unavailable");
        if(name->size()>max_bytes-bytes)return refuse("phonebook label byte budget exhausted");
        bytes+=name->size();result.choices.push_back({*name,target});
    }
    result.ready=true;return result;
}
}
