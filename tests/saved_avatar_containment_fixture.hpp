#pragma once
#include "odh/avatar_containment.hpp"
#include <algorithm>
// Convert owned synthetic fixture metadata into the existing saved-avatar
// profile. Runtime values stay mutable; immutable banks come from its baseline.
inline void saved_avatar_fixture_profile(odh::WorldSnapshot& world,const odh::WorldSnapshot* original=nullptr){
    for(auto& entity:world.entities){
        if(entity.kind!=odh::EntityKind::avatar)continue;
        if(!entity.saved_object_banks){
            if(original){const auto source=std::find_if(original->entities.begin(),original->entities.end(),
                [&](const auto& candidate){return candidate.id==entity.id;});
                if(source==original->entities.end()||!source->saved_object_banks)throw std::runtime_error("fixture provenance absent");
                entity.saved_object_banks=source->saved_object_banks;
            }else{
                odh::SavedObjectBanks banks{};banks.object_data[11]=entity.id;banks.object_data[3]=entity.unattached_slot;
                for(const auto& field:entity.object_words)if(field.first<68)banks.object_data[static_cast<std::size_t>(field.first)]=field.second;
                entity.saved_object_banks=banks;
            }
            world.word_count+=73;
        }
        for(std::int16_t index=0;index<68;++index){
            if(index==2||index==3||index==11||index==67)continue;
            if(entity.object_words.emplace(index,0).second)++world.word_count;
        }
    }
}
inline void saved_avatar_fixture_full_slots(odh::WorldSnapshot& world){
    for(auto& entity:world.entities)if(odh::ts1_saved_avatar_containment(entity)){
        world.word_count+=3-entity.slots.size();entity.slots.resize(3,0);
    }
}
