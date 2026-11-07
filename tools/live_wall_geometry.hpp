// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/wall_route_state.hpp"
#include <set>
#include <string>

namespace odh_host {
// The captured source flags identify inputs which had no architectural role.
// Their obsolete numeric IDs are not historical wall owners. Current flags
// remain mutable and every live object is checked below, so a new/unbound door
// cannot disappear just because its number once belonged to an ordinary object.
inline odh::WallEdgeBuildResult live_household_wall_edges(const odh::World& world,
        const odh::SavedArchitecture& architecture,const std::vector<odh::WallAppearanceInput>& saved,
        std::int16_t level){
    const auto unavailable=[](std::string why){return odh::WallEdgeBuildResult{
        odh::WallEdgeBuildStatus::unsupported,{},std::move(why)};};
    if(saved.size()>4096)return unavailable("architectural input budget exceeded");
    std::vector<odh::WallAppearanceInput> inputs;std::set<std::int16_t> known;
    for(const auto& original:saved){
        if(!(original.flags2&0xc000U))continue;
        const auto* live=world.find(original.id);
        if(!live)return unavailable("architectural owner removed without a current wall binding event id="+std::to_string(original.id)+
            " generation="+std::to_string(original.generation));
        if(live->generation!=original.generation||!live->definition||live->definition->guid!=original.guid||live->kind!=original.kind)
            return unavailable("architectural binding identity changed id="+std::to_string(original.id)+
                " expected_generation="+std::to_string(original.generation)+" actual_generation="+std::to_string(live->generation));
        const auto flags=live->object_words.find(40),placement=live->object_words.find(13);
        if(flags==live->object_words.end()||placement==live->object_words.end())
            return unavailable("architectural flags/placement unavailable id="+std::to_string(live->id));
        auto input=original;input.position=live->position;input.contained=bool(live->attachment);
        input.flags2=static_cast<std::uint16_t>(flags->second);input.placement=static_cast<std::uint16_t>(placement->second);
        known.insert(live->id);inputs.push_back(std::move(input));
    }
    for(std::int16_t cursor=0;;){const auto next=world.next_entity(cursor);if(!next)break;cursor=*next;
        const auto* entity=world.find(cursor);
        if(entity->kind!=odh::EntityKind::object||!entity->position||entity->attachment)continue;
        const auto flags=entity->object_words.find(40);
        if(flags==entity->object_words.end())return unavailable("live object flags unavailable id="+std::to_string(cursor));
        if((static_cast<std::uint16_t>(flags->second)&0xc000U)&&!known.count(cursor))
            return unavailable("live architectural object has no exact binding id="+std::to_string(cursor)+
                " generation="+std::to_string(entity->generation));
    }
    try{
        const auto appearance=odh::derive_wall_appearance(architecture,inputs);
        return odh::resolve_initial_wall_edges(architecture,appearance,level);
    }catch(const std::exception& error){return unavailable(std::string("architectural appearance: ")+error.what());}
}
} // namespace odh_host
