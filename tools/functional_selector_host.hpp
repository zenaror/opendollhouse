// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include <set>

namespace odh_host {
using FunctionalSelectorGroup=std::function<std::optional<std::vector<odh::WorldEntityReference>>(odh::WorldEntityReference)>;
using FunctionalSelectorThread=std::function<const odh::Vm*(odh::WorldEntityReference)>;

// VMEntity.IsInUse: each physical part checks its flag, then real Thread stacks.
// Providers prove the complete physical group and actual entity Thread owner;
// raw IDs never substitute for pointers after native ID reuse.
inline std::optional<bool> functional_selector_in_use(const odh::World& world,
        odh::WorldEntityReference target,const FunctionalSelectorGroup& group,
        const FunctionalSelectorThread& thread_for){
    const auto* entity=world.find(target.id);
    if(!entity||entity->generation!=target.generation||!group)return {};
    const auto members=group(target);if(!members||members->empty()||members->size()>4096)return {};
    std::set<std::pair<std::int16_t,std::uint64_t>> identities;
    for(const auto member:*members){
        const auto* current=world.find(member.id);
        if(!current||current->generation!=member.generation||!identities.emplace(member.id,member.generation).second)return {};
    }
    if(!identities.count({target.id,target.generation}))return {};
    for(const auto member:*members){
        const auto* current=world.find(member.id);
        const auto flags=current->object_words.find(8);if(flags==current->object_words.end())return {};
        if((static_cast<std::uint16_t>(flags->second)&32U)!=0)return true;
        if(!thread_for)return {};
        // Source multitile IsInUse visits each part's flag, then that part's
        // avatar-stack scan, before proceeding to the next physical member.
        for(auto cursor=std::int16_t{0};;){
            const auto next=world.next_entity(cursor,odh::EntityKind::avatar);if(!next)break;cursor=*next;
            const auto* avatar=world.find(cursor);const odh::WorldEntityReference owner{cursor,avatar->generation};
            const auto* thread=thread_for(owner);if(!thread)return {};
            for(const auto& frame:thread->entity_thread_frames()){
                if(!frame.context.entity_references_bound)return {};
                const auto callee=odh::vm_context_reference(frame.context,odh::VmEntityRole::callee);
                if(!callee.generation)continue; // Known nil, even with a positive raw ID.
                if(!world.resolve_reference(callee))return {};
                if(callee.id==member.id&&callee.generation==member.generation)return true;
            }
        }
    }
    return false;
}
} // namespace odh_host
