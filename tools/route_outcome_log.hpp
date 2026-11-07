// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/routing.hpp"
#include <algorithm>
#include <limits>
#include <iterator>

namespace odh_host {
struct RouteOutcomeObservation {
    std::uint64_t tick=0;
    odh::RouteOutcomeReceipt value;
    std::optional<std::uint16_t> actor_room,target_room,goal_room;
    std::optional<odh::RoomPortalEdge> pending_portal;
    std::uint64_t portal_generation=0;
    bool portal_context_present=false;
};
struct RouteOutcomeSummary {
    std::optional<RouteOutcomeObservation> first_failure,completion;
};
// One bounded summary per actual (actor generation, VM run, route ticket).
// A failure-tree request is not a successful VM completion. Restored routes may
// have a completion without a first failure observed in this process.
class RouteOutcomeLog {
public:
    void record(RouteOutcomeObservation observation){
        const auto& key=observation.value;
        auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){
            const auto& saved=row.completion?row.completion->value:row.first_failure->value;
            return saved.actor.id==key.actor.id&&saved.actor.generation==key.actor.generation&&
                saved.run_generation==key.run_generation&&saved.ticket==key.ticket;
        });
        if(found==rows.end()){
            const auto actor_count=std::count_if(rows.begin(),rows.end(),[&](const auto& row){
                const auto& saved=row.completion?row.completion->value:row.first_failure->value;
                return saved.actor.id==key.actor.id&&saved.actor.generation==key.actor.generation;
            });
            if(rows.size()>=128||actor_count>=32){if(dropped!=std::numeric_limits<std::uint64_t>::max())++dropped;return;}
            rows.emplace_back();found=std::prev(rows.end());
        }
        if(observation.value.reason.size()>512)observation.value.reason.resize(512);
        if(key.stage==odh::RouteOutcomeStage::failure_decision){if(!found->first_failure)found->first_failure=std::move(observation);}
        else if(!found->completion)found->completion=std::move(observation);
    }
    std::vector<RouteOutcomeSummary> rows;
    std::uint64_t dropped=0;
};
} // namespace odh_host
