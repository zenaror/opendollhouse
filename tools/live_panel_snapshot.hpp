// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "live_panel_view.hpp"
#include "odh/world.hpp"
#include "odh/relationship.hpp"
namespace odh_host {
struct LivePanelBindings {
    std::function<std::optional<LiveCareerInfo>(std::int16_t,std::int16_t)> career;
    // Admit an actual neighbor identity. Only visible entries request a face.
    std::function<std::optional<LiveRelationshipInfo>(std::int16_t,bool)> neighbor;
    std::size_t first_portrait=0,portrait_count=0;
};
LivePanelSnapshot prepare_live_panel_snapshot(const odh::World&,odh::WorldEntityReference,
    const odh::RelationshipState*,const LivePanelBindings& = {});
}
