// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
namespace odh {
inline constexpr std::size_t ts1_neighbor_person_words=88;
// NBRS.Neighbour pads its old 80-word TS1 data to88. InheritNeighbor then
// replaces the constructor's101-word array with that88-word projection.
// Only the authenticated saved-avatar80/88 profile receives this projection;
// arbitrary synthetic banks, constructor101 and raw256 history stay unchanged.
inline bool ts1_saved_short_person_data(const Entity& entity) noexcept {
    return entity.kind==EntityKind::avatar&&entity.saved_object_banks&&
        (entity.person_data.size()==80||entity.person_data.size()==ts1_neighbor_person_words);
}
inline std::size_t logical_person_data_words(const Entity& entity) noexcept {
    return ts1_saved_short_person_data(entity)?ts1_neighbor_person_words:entity.person_data.size();
}
inline std::optional<std::int16_t> person_data_value(const Entity& entity,std::int16_t index) noexcept {
    if(index<0)return std::nullopt;
    const auto selected=static_cast<std::size_t>(index);
    if(selected<entity.person_data.size())return entity.person_data[selected];
    if(ts1_saved_short_person_data(entity)&&selected<ts1_neighbor_person_words)return std::int16_t{0};
    return std::nullopt;
}
inline std::vector<std::int16_t> imported_ts1_person_data(const std::vector<std::int16_t>& raw){
    auto result=raw;
    if(result.size()==80)result.resize(ts1_neighbor_person_words,0);
    return result;
}
// Bounded diagnostic only: never changes compatibility or grants metadata
// authority. Callers supply the actual observed and reconstructed expected body.
inline std::string describe_person_metadata_pair(const Entity& observed,const Entity& expected){
    const auto body=[](const Entity& e){
        return std::string("id=")+std::to_string(e.id)+",gen="+std::to_string(e.generation)+
            ",kind="+(e.kind==EntityKind::avatar?"avatar":"object")+
            ",guid="+(e.definition?std::to_string(e.definition->guid):"absent")+
            ",nid="+(e.person_data.size()>31?std::to_string(e.person_data[31]):"absent")+
            ",person_words="+std::to_string(e.person_data.size())+
            ",definition_words="+std::to_string(e.definition_words.size())+
            ",saved_banks="+std::to_string(e.saved_object_banks.has_value());
    };
    std::string changes;std::size_t differences=0;
    const auto add=[&](std::int16_t index,const std::string& a,const std::string& b){
        if(differences++>=16)return;
        if(!changes.empty())changes+=';';
        changes+=std::to_string(index)+":"+a+"/"+b;
    };
    for(const auto& word:observed.definition_words){const auto old=expected.definition_words.find(word.first);
        if(old==expected.definition_words.end())add(word.first,std::to_string(word.second),"absent");
        else if(word.second!=old->second)add(word.first,std::to_string(word.second),std::to_string(old->second));}
    for(const auto& word:expected.definition_words)if(!observed.definition_words.count(word.first))
        add(word.first,"absent",std::to_string(word.second));
    return "observed{"+body(observed)+"} expected{"+body(expected)+"} definition_diff(observed/expected)=["+
        changes+"] total_definition_differences="+std::to_string(differences);
}
inline bool compatible_person_data_extent(const Entity& a,const Entity& b) noexcept {
    if(a.person_data.size()==b.person_data.size())return true;
    return ts1_saved_short_person_data(a)&&ts1_saved_short_person_data(b)&&a.id==b.id&&a.generation==b.generation&&
        a.definition&&b.definition&&a.definition->guid==b.definition->guid&&a.definition->master_guid==b.definition->master_guid&&
        a.saved_object_banks->object_data==b.saved_object_banks->object_data&&a.saved_object_banks->extras==b.saved_object_banks->extras;
}
}
