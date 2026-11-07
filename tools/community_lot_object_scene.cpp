// SPDX-License-Identifier: MPL-2.0
#include "community_lot_object_scene.hpp"
#include "residential_property_scene.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace odh_host {
namespace {
using namespace odh;
void need(bool value,const char* why){if(!value)throw FormatError(why);}
bool same_identity(const Entity& a,const Entity& b){
    return a.id==b.id&&a.generation==b.generation&&a.kind==b.kind&&
        a.ui_ghost==b.ui_ghost&&a.definition.has_value()==b.definition.has_value()&&
        (!a.definition||(a.definition->guid==b.definition->guid&&
            a.definition->master_guid==b.definition->master_guid));
}
void authenticate_origin(const SessionCreatedObject& origin,ObjectTradeResourceLibrary& resources){
    const auto& actual=resources.load(origin.guid).code.origin;
    need(origin.source==actual.source&&origin.asset==actual.asset&&
        origin.definition_chunk_id==actual.definition_chunk_id,
        "community created object origin differs from installed ordinary resource");
    const auto* definition=resources.definitions().unique(origin.guid);
    need(definition&&!(definition->definition.master_id&&definition->definition.sub_index==-1),
        "community created object is an abstract master");
    if(origin.master_guid){const auto* master=resources.definitions().unique_master(*definition);
        need(master&&master->definition.guid==*origin.master_guid,"community created subpart master differs from exact resource");}
}
}
ActorlessObjectTradeState make_community_object_baseline(const SavedWorld& original,
        SessionResourceIdentity resources,RuntimeGlobalsSnapshot globals,
        std::optional<Ts1ClockSnapshot> clock,std::uint64_t random_seed){
    need(original.world&&original.relationships,"community baseline source World/relationships unavailable");
    ActorlessObjectTradeState state;state.resources=std::move(resources);
    state.world=original.world->snapshot();state.relationships=original.relationships->snapshot();
    need(state.world.retained_entities.empty(),"community baseline must start from a fresh imported lifetime");
    state.globals=std::move(globals);state.clock=std::move(clock);
    state.tick=state.clock?state.clock->ticks:0;state.random_seed=random_seed;
    state.scheduler.entity_limit=actorless_object_trade_limits().world.world.entities;
    state.history.complete=true;
    validate_actorless_object_trade_state(state);return state;
}

static CommunityObjectScene prepare_object_scene_impl(const SavedWorld& original,
        const ActorlessObjectTradeState& state,const DefinitionIndex& definitions,
        ObjectTradeResourceLibrary& resources,const ResidentialPropertyRetirementProof* retirement){
    need(original.world&&original.relationships,"community scene original World/relationships unavailable");
    validate_actorless_object_trade_state(state);
    if(retirement)need(retirement->matches(state),"residential property changed after its retirement proof");
    const auto original_snapshot=original.world->snapshot();
    need(state.world.generation_sequence>=original_snapshot.generation_sequence,
        "community scene lifetime sequence predates original import");
    std::map<std::int16_t,const Entity*> initial,current;
    for(const auto& e:original_snapshot.entities)initial.emplace(e.id,&e);
    for(const auto& e:state.world.entities)current.emplace(e.id,&e);
    std::map<std::int16_t,const SessionCreatedObject*> created;
    for(const auto& origin:state.created_objects){
        need(origin.generation>original_snapshot.generation_sequence,
            "community created identity borrows an original generation");
        authenticate_origin(origin,resources);created.emplace(origin.id,&origin);
        const auto* actual=definitions.unique(origin.guid);
        const auto* resource_definition=resources.definitions().unique(origin.guid);
        need(actual&&actual->chunk_id==origin.definition_chunk_id&&
            actual->asset==origin.asset&&resource_definition&&
            actual->source==resource_definition->source&&actual->asset==resource_definition->asset&&
            actual->chunk_id==resource_definition->chunk_id,
            "community created render definition unavailable, ambiguous or differs from resource library");
    }
    for(const auto& origin:state.history.retired_created_objects){
        need(origin.generation>original_snapshot.generation_sequence,
            "community retired creation borrows an original generation");
        authenticate_origin(origin,resources);
    }
    // No visitor creation is available in this editor profile. Their saved
    // original entities may remain, but new identities need a future actor
    // loader and authenticated character provenance, not generic object art.
    if(retirement){
        for(const auto& origin:state.history.retired_npc_avatars)
            need(retirement->retired_npc(origin),"residential visitor retirement differs from authenticated provenance");
        for(const auto& origin:state.history.retired_household_avatars)
            need(retirement->retired_household(origin),"residential household retirement differs from authenticated provenance");
    }else need(state.history.retired_npc_avatars.empty()&&state.history.retired_household_avatars.empty(),
        "community scene contains unsupported created visitor or household history");
    std::set<std::int16_t> removed;
    for(const auto& origin:state.removed_original_objects){
        const auto old=initial.find(origin.id);
        need(old!=initial.end()&&old->second->kind==EntityKind::object&&
            old->second->generation==origin.generation&&old->second->definition&&
            old->second->definition->guid==origin.guid,
            "community removed original identity differs from immutable import");
        removed.insert(origin.id);
    }
    for(const auto& [id,old]:initial){
        const auto live=current.find(id);
        if(live!=current.end()&&same_identity(*old,*live->second)){
            need(!removed.count(id),"community surviving original also marked removed");
        }else need(removed.count(id)||(retirement&&retirement->retired_original_avatar(*old)),
            "community original identity missing without exact removal provenance");
    }
    for(const auto& [id,live]:current){
        const auto old=initial.find(id);
        if(old!=initial.end()&&same_identity(*old->second,*live))continue;
        const auto origin=created.find(id);
        need(origin!=created.end()&&live->kind==EntityKind::object&&!live->ui_ghost&&
            live->generation==origin->second->generation&&live->definition&&
            live->definition->guid==origin->second->guid&&live->definition->master_guid==origin->second->master_guid,
            "community current object lacks exact authenticated creation provenance");
    }
    const auto components=actorless_object_trade_components(state);
    for(const auto& thread:state.object_threads){
        auto programs=reconstruct_community_object_programs(resources,thread.id,components);
        Vm vm(programs.globals,ShareVmProgramTag{},VmMemory{},VmLimits{},VmPrimitive{},
            std::move(programs.owners),ShareVmOwnerProgramsTag{});
        need(vm.restore_snapshot(thread.vm,programs.saved_routine_policy),
            "community frozen object Thread differs from authenticated program closure");
    }
    // Created objects always have their actual staged Main (or empty ready
    // Thread) captured by the transaction. Do not treat absent execution state
    // as a fresh Init or silently manufacture one on reopen.
    for(const auto& [id,origin]:created){
        need(std::any_of(state.object_threads.begin(),state.object_threads.end(),[&](const auto& t){
            return t.id==id&&t.generation==origin->generation&&t.guid==origin->guid;
        }),"community created object has no actual frozen lifecycle Thread");
    }
    CommunityObjectScene scene;scene.saved_world=std::make_unique<SavedWorld>();
    auto limits=actorless_object_trade_limits().world.world;
    scene.saved_world->world=std::make_unique<World>(limits,state.world.attribute_policy);
    scene.saved_world->relationships=std::make_unique<RelationshipState>(state.relationships.limits);
    need(scene.saved_world->world->restore_snapshot(state.world)&&
        scene.saved_world->world->install_reserved_entity_ids(state.history.reserved_entity_ids)&&
        scene.saved_world->relationships->restore_snapshot(state.relationships),
        "community scene candidate World/relationships cannot be restored");
    std::set<std::int16_t> recorded;
    for(const auto& record:original.records){
        const auto id=static_cast<std::int16_t>(record.saved.object_id);
        const auto old=initial.find(id),live=current.find(id);
        if(old==initial.end()||live==current.end()||!same_identity(*old->second,*live->second))continue;
        need(live->second->definition&&record.imported_generation==live->second->generation&&
            record.saved.guid==live->second->definition->guid&&recorded.insert(id).second,
            "community original render record lifetime differs from World");
        scene.saved_world->records.push_back(record);
    }
    std::set<std::int16_t> grouped;
    if(state.object_groups)for(const auto& group:state.object_groups->groups){
        if(group.kind==ObjectGroupKind::live_association){
            ObjectTradeGroup physical;physical.complete=true;
            for(const auto& part:group.members){
                need(part.origin&&grouped.insert(part.id).second,"community live association member lacks unique provenance");
                const auto& source=*part.origin;
                SessionCreatedObject origin;origin.id=part.id;origin.generation=part.generation;
                origin.guid=source.guid;origin.master_guid=source.master_guid;origin.source=source.source;
                origin.asset=source.asset;origin.definition_chunk_id=source.definition_chunk_id;
                authenticate_origin(origin,resources);
                const auto live=current.find(part.id);
                need(live!=current.end()&&live->second->generation==part.generation&&live->second->definition&&
                    live->second->definition->guid==origin.guid&&live->second->definition->master_guid==origin.master_guid,
                    "community live association member differs from actual World");
                if(source.source_kind==ObjectGroupMemberSource::native_created){
                    const auto found=created.find(part.id);
                    need(found!=created.end()&&found->second->generation==part.generation&&found->second->guid==origin.guid&&
                        found->second->source==origin.source&&found->second->asset==origin.asset&&
                        found->second->definition_chunk_id==origin.definition_chunk_id&&
                        (!found->second->master_guid||found->second->master_guid==origin.master_guid),
                        "community live association native origin differs from actual creation");
                }else{
                    const auto old=initial.find(part.id);
                    need(old!=initial.end()&&!created.count(part.id)&&same_identity(*old->second,*live->second)&&
                        old->second->definition&&old->second->definition->guid==origin.guid&&old->second->definition->master_guid==origin.master_guid,
                        "community live association original member differs from immutable import");
                }
                physical.members.push_back({part.id,part.generation});
            }
            scene.runtime_groups.push_back(group);scene.physical_groups.push_back(std::move(physical));
            continue;
        }
        const bool imported=retirement&&std::all_of(group.members.begin(),group.members.end(),[&](const auto& part){
            const auto before=initial.find(part.id),now=current.find(part.id);
            return before!=initial.end()&&now!=current.end()&&!created.count(part.id)&&
                now->second->generation==part.generation&&same_identity(*before->second,*now->second);
        });
        if(imported){
            ObjectTradeGroup physical;physical.complete=true;
            for(const auto& part:group.members){
                need(grouped.insert(part.id).second,"residential imported membership overlaps");
                const auto* own=definitions.unique(current.at(part.id)->definition->guid);
                need(own,"residential imported member lacks its exact resource");
                if(group.multitile){const auto* master=definitions.unique_master(*own);
                    need(master&&master->definition.guid==group.definition_guid,"residential imported master differs");}
                else need(own->definition.guid==group.definition_guid&&!own->definition.is_multitile,
                    "residential imported singleton resource differs");
                physical.members.push_back({part.id,part.generation});
            }
            scene.runtime_groups.push_back(group);scene.physical_groups.push_back(std::move(physical));continue;
        }
        const auto resource=resources.load_group(group.definition_guid);
        need(resource.group&&resource.group->parts.size()==group.members.size()&&
            resource.group->multitile==group.multitile&&resource.group->base_index<group.members.size()&&
            group.members[resource.group->base_index].id==group.base_id,
            "community constructed physical group differs from exact construction plan");
        ObjectTradeGroup physical;physical.complete=true;
        for(std::size_t n=0;n<group.members.size();++n){const auto& part=group.members[n];
            need(created.count(part.id)&&created.at(part.id)->generation==part.generation&&grouped.insert(part.id).second,
                "community constructed physical group lacks its exact creation cohort");
            const auto& expected=resource.group->parts[n];const auto& origin=*created.at(part.id);
            need(part.offset_x==expected.offset_x&&part.offset_y==expected.offset_y&&part.offset_level==expected.offset_level&&
                origin.guid==expected.resource.origin.guid&&origin.source==expected.resource.origin.source&&
                origin.asset==expected.resource.origin.asset&&origin.definition_chunk_id==expected.resource.origin.definition_chunk_id&&
                origin.master_guid==expected.resource.origin.master_guid,
                "community constructed physical group member/order/offset/resource differs from source plan");
            physical.members.push_back({part.id,part.generation});
        }
        scene.runtime_groups.push_back(group);scene.physical_groups.push_back(std::move(physical));
    }
    for(const auto& group:original.multitile_groups){
        ObjectTradeGroup physical;physical.complete=true;std::size_t surviving=0,shadowed=0;
        for(const auto id:group.members){
            const auto old=initial.find(id),live=current.find(id);
            need(old!=initial.end(),"community original physical group references missing source identity");
            if(live!=current.end()&&same_identity(*old->second,*live->second)){
                ++surviving;if(grouped.count(id)){++shadowed;continue;}
                physical.members.push_back({id,live->second->generation});
            }
        }
        if(shadowed){need(shadowed==surviving,"community original physical group only partly overridden");continue;}
        need(!surviving||surviving==group.members.size(),"community original physical group was only partly removed");
        if(!surviving)continue;
        need(!physical.members.empty(),"community original physical group is empty");
        for(const auto& member:physical.members)need(grouped.insert(member.id).second,
            "community original physical groups overlap");
        scene.saved_world->multitile_groups.push_back(group);
        scene.physical_groups.push_back(std::move(physical));
    }
    for(const auto& [id,live]:current)if(!grouped.count(id)){
        // Imported ungrouped members are actual singleton groups created by
        // the original loader; matching MasterID alone never joins them.
        scene.physical_groups.push_back({true,{{id,live->generation}}});
    }
    for(const auto& [id,origin]:created){
        const auto* definition=definitions.unique(origin->guid);
        scene.runtime_objects.push_back({id,origin->generation,origin->guid,
            definition->source,definition->asset,definition->chunk_id});
        if(!state.object_groups||!std::any_of(state.object_groups->groups.begin(),state.object_groups->groups.end(),[&](const auto& group){
            return std::any_of(group.members.begin(),group.members.end(),[&](const auto& part){return part.id==id&&part.generation==origin->generation;});}))
            scene.runtime_groups.push_back({origin->guid,false,id,{{id,origin->generation,0,0,0}}});
    }
    return scene;
}
CommunityObjectScene prepare_community_object_scene(const SavedWorld& original,
        const ActorlessObjectTradeState& state,const DefinitionIndex& definitions,ObjectTradeResourceLibrary& resources){
    return prepare_object_scene_impl(original,state,definitions,resources,nullptr);
}
CommunityObjectScene prepare_community_object_scene(const SavedWorld& original,
        const ActorlessObjectTradeState& state,const DefinitionIndex& definitions,ObjectTradeResourceLibrary& resources,
        const ResidentialPropertyRetirementProof& retirement){
    return prepare_object_scene_impl(original,state,definitions,resources,&retirement);
}

}
