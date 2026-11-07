// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_placement.hpp"
#include <functional>
#include <optional>
#include <string>

namespace odh_host {
// Invoked only inside the World's synchronous removed-container lease. These
// callbacks retain exact references; no fabricated VM invocation represents Delete.
struct SourceContainerDropServices {
    odh::WorldEntityReference parent;
    std::function<bool()> source_still_matches;
    std::function<std::optional<std::size_t>()> total_slots;
    // nullopt=unavailable, reference {0,0}=a source empty slot.
    std::function<std::optional<odh::WorldEntityReference>(std::size_t)> child;
    std::function<std::optional<std::uint8_t>(odh::WorldEntityReference)> direction;
    // Copy the child's actual raw Position to the retained parent.
    std::function<bool(odh::WorldEntityReference)> copy_position;
    std::function<std::optional<bool>()> anchor_positioned;
    std::function<odh::Ts1PlacementResult(odh::WorldEntityReference,std::uint8_t)> out_of_world;
    std::function<std::optional<std::int16_t>(odh::WorldEntityReference)> hidden;
    std::function<bool(odh::WorldEntityReference)> unhide;
    std::function<odh::Ts1PlacementResult(odh::WorldEntityReference)> find_location;
};
struct SourceContainerDropResult {bool completed=false;std::string diagnostic;};
inline SourceContainerDropResult source_container_drop(const SourceContainerDropServices& services){
    std::size_t slot=0;odh::WorldEntityReference child{};
    const auto fail=[&](const std::string& stage){return SourceContainerDropResult{false,
        "Delete children parent="+std::to_string(services.parent.id)+":"+std::to_string(services.parent.generation)+
        " child="+std::to_string(child.id)+":"+std::to_string(child.generation)+
        " slot="+std::to_string(slot)+" stage="+stage};};
    const auto valid=[&]{return services.source_still_matches&&services.source_still_matches();};
    if(services.parent.id<=0||!services.parent.generation||!valid()||!services.total_slots||!services.child||
       !services.direction||!services.copy_position||!services.anchor_positioned||!services.out_of_world||!services.hidden||!services.unhide||!services.find_location)
        return fail("services unavailable");
    const auto slots=services.total_slots();
    if(!slots||*slots>4096)return fail("TotalSlots unavailable or over budget");
    // Source snapshots only the extent. Each child is read anew because a prior
    // child PositionChange/EP9 may change later slots or recycle an entity ID.
    for(;slot<*slots;++slot){
        child={};if(!valid())return fail("source expired before GetSlot");
        const auto current=services.child(slot);if(!current||!valid())return fail("GetSlot unavailable or source expired");
        child=*current;if(child.id==0&&child.generation==0)continue;
        if(child.id<=0||!child.generation)return fail("GetSlot identity invalid");
        if(!services.copy_position(child)||!valid())return fail("copy actual child Position to retained parent");
        const auto direction=services.direction(child);
        if(!direction||*direction>7||!valid())return fail("child Direction unavailable or source expired");
        const auto moved=services.out_of_world(child,*direction);
        if(moved.status!=odh::Ts1PlacementStatus::success||!valid())return fail("child SetPosition OOW: "+moved.diagnostic);
        const auto hidden=services.hidden(child);if(!hidden||!valid())return fail("child Hidden unavailable");
        if(*hidden>0&&(!services.unhide(child)||!valid()))return fail("child Hidden=0");
        const auto positioned=services.anchor_positioned();
        if(!positioned||!valid())return fail("retained parent Position unavailable or source expired");
        if(*positioned){
            const auto placed=services.find_location(child);
            // Source ignores FindLocationFor's ordinary false. The OOW child
            // and all successful prior effects remain; unsupported is distinct.
            if(placed.status==odh::Ts1PlacementStatus::unsupported||!valid())return fail("FindLocationFor Default0: "+placed.diagnostic);
        }
    }
    if(!valid())return fail("source expired after slots");
    return {true,{}};
}
} // namespace odh_host
