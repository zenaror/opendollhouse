// SPDX-License-Identifier: MPL-2.0
#include "catalog_object_visuals.hpp"
#include "resource.hpp"
#include "odh/object_runtime_resources.hpp"

namespace odh_host {
odh::BuyModePreviewGroup prepare_catalog_buy_group(const odh::DefinitionCandidate& requested,
        const odh::DefinitionIndex& definitions){
    if(definitions.unique(requested.definition.guid)!=&requested)
        throw odh::FormatError("catalog placement preview requires exact indexed resource");
    odh::BuyModePreviewGroup result;
    const auto read=[&](const odh::View& resource){
        const auto plan=odh::plan_object_group_construction(resource,requested,definitions,64);
        result.base_index=plan.base_index;result.parts.reserve(plan.parts.size());
        for(const auto& part:plan.parts)result.parts.push_back({part.candidate->definition.guid,
            part.offset_x,part.offset_y,part.offset_level,0,{}});
    };
    if(requested.asset.empty())with_iff_file(requested.source,read);
    else with_far_entry(requested.source,requested.asset,read);
    return result;
}
std::vector<SavedObjectVisual> prepare_catalog_object_visuals(
        const odh::DefinitionCandidate& requested,const odh::DefinitionIndex& definitions,
        std::uint8_t direction){
    if(direction!=0)throw odh::FormatError("catalog authored group supports only direction0");
    if(definitions.unique(requested.definition.guid)!=&requested)
        throw odh::FormatError("catalog visual requires its exact unique indexed definition");
    std::vector<SavedObjectVisual> visuals;
    const auto read=[&](const odh::View& resource){
        const auto plan=odh::plan_object_group_construction(resource,requested,definitions,64);
        visuals.reserve(plan.parts.size());
        // Pinned FreeSO7e76 VMMultitileGroup.GetBasePositions uses x/y divided
        // by16 and signed level offset times2.95. Preserve the authenticated
        // source order, including when BaseObject is not the first part.
        for(const auto& part:plan.parts){
            visuals.push_back({static_cast<std::int16_t>(visuals.size()+1),
                part.candidate->definition.guid,0,direction,
                {part.offset_x/16.0f,part.offset_y/16.0f,part.offset_level*2.95f},{}});
        }
    };
    if(requested.asset.empty())with_iff_file(requested.source,read);
    else with_far_entry(requested.source,requested.asset,read);
    return visuals;
}
}
