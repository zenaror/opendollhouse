// SPDX-License-Identifier: MPL-2.0
#include "live_panel_snapshot.hpp"
#include <limits>
namespace odh_host {
LivePanelSnapshot prepare_live_panel_snapshot(const odh::World& world,odh::WorldEntityReference selected,
        const odh::RelationshipState* relationships,const LivePanelBindings& bindings){
    LivePanelSnapshot result;
    const auto* person=world.resolve_reference(selected);
    if(!person||person->kind!=odh::EntityKind::avatar||person->dead)return result;
    result.has_selected_person=true;
    const auto value=[&](unsigned index)->std::optional<std::int16_t>{
        return index<person->person_data.size()?std::optional<std::int16_t>{person->person_data[index]}:std::nullopt;};
    for(std::size_t i=0;i<result.skills.size();++i)result.skills[i]=value(live_skill_indices[i]);
    for(std::size_t i=0;i<result.personality.size();++i)result.personality[i]=value(live_personality_indices[i]);
    if(const auto age=value(58)){
        LiveInterestsInfo interests;interests.age=*age;bool complete=true;
        for(std::size_t i=0;i<interests.raws.size();++i){
            const auto raw=value(live_interest_indices[i]);
            if(!raw){complete=false;break;}interests.raws[i]=*raw;
        }
        if(complete)result.interests=interests;
    }
    const auto job=value(live_job_index),level=value(live_job_level_index);
    if(job&&*job==0)result.career=LiveCareerInfo{};
    else if(job&&level&&bindings.career)result.career=bindings.career(*job,*level);
    if(result.career)result.career->performance=value(live_job_performance_index);
    const auto neighbor=value(live_neighbor_index);
    if(neighbor&&relationships&&bindings.neighbor&&bindings.neighbor(*neighbor,false)){
        result.relationships.emplace();
        for(const auto& pair:relationships->neighbor_pairs(*neighbor)){
            if(pair.target<std::numeric_limits<std::int16_t>::min()||pair.target>std::numeric_limits<std::int16_t>::max())continue;
            const auto index=result.relationships->size();
            const bool visible=index>=bindings.first_portrait&&index-bindings.first_portrait<bindings.portrait_count;
            auto item=bindings.neighbor(static_cast<std::int16_t>(pair.target),visible);
            if(!item)continue;
            if(!pair.values.empty())item->daily=pair.values[0];
            if(pair.values.size()>2)item->long_term=pair.values[2];
            result.relationships->push_back(std::move(*item));
        }
    }
    return result;
}
}
