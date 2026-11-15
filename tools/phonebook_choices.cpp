// SPDX-License-Identifier: MPL-2.0
#include "phonebook_choices.hpp"
#include "phonebook_candidates.hpp"
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
    PhonebookSnapshot snapshot;snapshot.caller_current=snapshot.relationships_current=true;
    for(const auto& pair:source->pairs){
        snapshot.relationship_keys.push_back(pair.target);
        const auto target=static_cast<std::int16_t>(static_cast<std::uint32_t>(pair.target)&65535U);
        const auto* n=neighbors.by_id(target);if(!n)continue;
        const bool data=n->person_data&&n->person_data->size()>65;
        snapshot.neighbors.push_back({target,n->guid,data,data?n->person_data->at(61):int16_t(0),data?n->person_data->at(65):int16_t(0)});
    }
    const auto selected=select_phonebook_choices(snapshot,display_name,max_choices,max_bytes);
    PhonebookChoices result;result.ready=selected.ready;result.diagnostic=selected.diagnostic;
    for(const auto& c:selected.choices)result.choices.push_back({c.label,c.value});
    return result;
}
}
