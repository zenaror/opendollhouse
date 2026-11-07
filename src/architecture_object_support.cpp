// SPDX-License-Identifier: MPL-2.0
#include "odh/architecture_object_support.hpp"
#include "odh/formats.hpp"

namespace odh {
void validate_architecture_object_support(const ArchitectureObjectSupport& state,std::uint32_t dimension){
    if(!state.width||state.width!=state.height||state.width>64||state.width>dimension)
        throw FormatError("architecture object support extent outside TS1 profile");
    const auto cells=std::size_t(state.width)*state.height;
    for(const auto& level:state.levels){
        if(level.size()!=cells)throw FormatError("architecture object support grid extent mismatch");
        for(const auto value:level)if(value>1)throw FormatError("architecture object support marker invalid");
    }
}
ArchitectureObjectSupport make_architecture_object_support(std::uint32_t width,std::uint32_t height){
    // Validate before allocation, including an overflow-safe bounded extent.
    if(!width||width!=height||width>64)throw FormatError("architecture object support extent outside TS1 profile");
    ArchitectureObjectSupport result;result.width=width;result.height=height;
    for(auto& level:result.levels)level.assign(std::size_t(width)*height,0);
    return result;
}
bool set_architecture_object_supported(ArchitectureObjectSupport& state,const World& world,
        ObjectPositionIdentity identity,const EntityPosition& position,bool set,
        const std::function<bool()>& source_still_matches){
    try{
        validate_architecture_object_support(state);
        if(!source_still_matches||!source_still_matches())return false;
        const auto* entity=world.find(identity.id);
        if(!entity||entity->generation!=identity.generation||entity->kind!=EntityKind::object||
           entity->ui_ghost||entity->attachment||!entity->position||!(*entity->position==position))return false;
        const auto category=entity->object_words.find(59);
        if(category==entity->object_words.end()||category->second!=8)return false;
        if(position.x<0||position.y<0||position.level<1||position.level>2||
           position.x/16>=std::int32_t(state.width)||position.y/16>=std::int32_t(state.height))return false;
        const auto offset=std::size_t(position.y/16)*state.width+std::size_t(position.x/16);
        // Source assignment is intentionally not a refcount. Clearing one
        // overlapping Category8 object clears the cell even if another remains.
        state.levels[std::size_t(position.level-1)][offset]=set?1:0;
        return true;
    }catch(const FormatError&){return false;}
}
}
