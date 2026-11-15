// SPDX-License-Identifier: MPL-2.0
#include "house_panel_snapshot.hpp"
#include <limits>
namespace odh_host {
HousePanelSnapshot prepare_house_panel_snapshot(const HousePanelInput& input){
    HousePanelMetrics metrics{input.rooms};metrics.family_members=input.family_members;
    metrics.size_curve=input.size_curve;metrics.furnishings_curve=input.furnishings_curve;
    metrics.tuning=input.tuning;
    if(input.layout_history){
        HousePanelLayoutMetrics history;history.last_sample_tick=input.layout_history->last_sample_tick;
        history.current={input.layout_history->current.samples,input.layout_history->current.routing};
        for(unsigned i=0;i<5;++i)history.past[i]={input.layout_history->past[i].samples,input.layout_history->past[i].routing};
        metrics.layout_history=history;
    }
    metrics.outdoor_good_terrain_cells=input.bindings.outdoor_good_terrain_cells;
    metrics.object_words_current=bool(input.bindings.object_word);metrics.objects_complete=true;
    if(input.valuation&&input.valuation->status==odh::PropertyValuationStatus::resolved&&
        input.valuation->objects.primary&&input.valuation->combined_architecture&&input.furnishings_curve){
        const float denominator=float(*input.valuation->objects.primary);
        const float value=denominator==0?100.0f:float(*input.valuation->combined_architecture)/denominator;
        metrics.furnishings_ratio=value;
    }
    if(input.object_assets&&input.architecture_value&&input.furnishings_curve&&
        input.object_assets->status==odh::PropertyObjectAssetsStatus::resolved&&
        input.architecture_value->status==odh::PropertyArchitectureValueStatus::resolved&&
        input.object_assets->primary&&input.object_assets->architecture_objects){
        const auto combined=std::int64_t(input.architecture_value->total)+*input.object_assets->architecture_objects;
        if(combined>=INT32_MIN&&combined<=INT32_MAX){
            const float denominator=float(*input.object_assets->primary);
            const float value=denominator==0?100.0f:float(combined)/denominator;
            metrics.furnishings_ratio=value;
        }
    }

    const auto word=[&](const odh::Entity& entity,std::int16_t field)->std::optional<std::int16_t>{
        if(input.bindings.object_word)return input.bindings.object_word(entity,field);
        if(field==29)return {};
        const auto at=entity.object_words.find(field);
        return at==entity.object_words.end()?std::optional<std::int16_t>{}:at->second;
    };
    auto cursor=std::int16_t{-1};
    while(const auto id=input.world.next_entity(cursor)){
        cursor=*id;
        if(metrics.objects.size()>=4096){metrics.objects_complete=false;break;}
        const auto* entity=input.world.find(*id);if(!entity){metrics.objects_complete=false;break;}
        HousePanelObjectMetrics row;row.positioned=entity->position.has_value();
        row.hidden=word(*entity,34);row.room=word(*entity,29);row.category=word(*entity,59);
        row.impact=word(*entity,7);row.repair=word(*entity,15);row.dirty=word(*entity,39);
        if(input.bindings.definition_word)for(unsigned i=0;i<3;++i)
            row.upkeep_definition_words[i]=input.bindings.definition_word(*entity,i==0?39:i==1?40:69);
        metrics.objects.push_back(std::move(row));
    }
    return prepare_house_panel_metrics(metrics);
}
}
