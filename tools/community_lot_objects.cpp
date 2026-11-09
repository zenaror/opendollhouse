// SPDX-License-Identifier: MPL-2.0
#include "community_lot_objects.hpp"
#include "odh/seated_session_continuation.hpp"
#include <algorithm>
#include <set>

namespace odh_host {
namespace {
using namespace odh;
void need(bool value,const char* why){if(!value)throw FormatError(why);}
}
ObjectTradeThreadPrograms reconstruct_community_object_programs(ObjectTradeResourceLibrary& resources,
        std::int16_t id,const SessionBundle& source){
    const auto thread=std::find_if(source.object_threads.begin(),source.object_threads.end(),[&](const auto& t){return t.id==id;});
    need(thread!=source.object_threads.end()&&thread->code_owners.empty()&&!thread->resource_owners.empty(),
        "actorless object Thread needs exact represented resource banks");
    ObjectTradeThreadPrograms result;result.primary_owner=thread->primary_owner;result.globals=resources.global_program();
    for(const auto& pin:thread->resource_owners){const auto& r=resources.load(pin.resource_guid).code;
        auto bank=r.runtime.private_routines;bank.insert(r.linked_routines.begin(),r.linked_routines.end());
        need(pin.source==r.origin.source&&pin.asset==r.origin.asset&&pin.definition_chunk_id==r.origin.definition_chunk_id&&
            pin.tree_table_id==r.runtime.definition.tree_table_id&&pin.root_routine==(bank.empty()?4096:bank.begin()->first.id)&&
            pin.linked_semiglobal==r.linked_semiglobal&&pin.routine_fingerprint==session_loaded_bank_fingerprint(bank)&&
            pin.bank_kind==(r.linked_semiglobal?SessionCodeBankKind::private_and_semiglobal:SessionCodeBankKind::complete_private),
            "actorless object Thread resource provenance changed");
        need(result.owners.emplace(pin.owner,std::make_shared<const VmProgram>(std::move(bank))).second,
            "actorless object Thread repeats an installed owner");
    }
    const auto primary=std::find_if(thread->resource_owners.begin(),thread->resource_owners.end(),[&](const auto& pin){return pin.owner==thread->primary_owner;});
    need(primary!=thread->resource_owners.end()&&primary->resource_guid==thread->guid,
        "actorless object Thread primary owner differs from its actual resource");
    return result;
}
ActorlessObjectTradeResult prepare_community_object_command(ObjectTradeResourceLibrary& resources,
        const ActorlessObjectTradeState& state,const PreparedArchitecture& prepared,
        const SavedArchitecture& architecture,const GeometricRoomMap& rooms,
        const ObjectTradeSessionCommand& command,CommunityObjectCommandServices services){
    ActorlessObjectTradeResult result;
    if(command.kind==ObjectTradeSessionCommandKind::cancel){result.diagnostic="cancelled";return result;}
    try{
        validate_actorless_object_trade_state(state);
        need(services.source_still_matches&&services.source_still_matches(),"community object command source authority unavailable");
        auto source=actorless_object_trade_components(state);
        resources.preload(source.world);
        std::int16_t selected=command.target.id;std::uint32_t guid=command.guid;CodeOwner owner=guid;
        std::vector<std::int16_t> ids;
        auto hooks=std::move(services.bindings);
        if(!hooks.thread_programs)hooks.thread_programs=[&resources](auto id,const auto& source){return reconstruct_community_object_programs(resources,id,source);};
        ObjectTradeResource resource;
        if(command.kind==ObjectTradeSessionCommandKind::purchase){
            resource=resources.command_resource(guid);
            ids=allocate_object_trade_group_ids(state.world,state.history,resource.group?resource.group->parts.size():1);
            need(!ids.empty(),"community object identity allocation failed");selected=ids.front();
        }else{
            const auto entity=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& e){return e.id==selected;});
            need(entity!=source.world.entities.end()&&entity->generation==command.target.generation&&entity->definition,
                "community object target is stale");guid=entity->definition->guid;
            const auto thread=std::find_if(source.object_threads.begin(),source.object_threads.end(),[&](const auto& t){return t.id==selected;});
            need(thread!=source.object_threads.end(),"community object target has no authenticated frozen Thread");owner=thread->primary_owner;
            resource=resolve_object_trade_source_group(resources,source,hooks,services.groups,command.target);
        }
        const auto* metadata=resources.find(guid);need(metadata,"community object resource unavailable");
        auto support=source.architecture_object_support;auto objects=source.architecture_object_state;
        auto* support_ptr=objects?&objects->object_support:support?&*support:nullptr;
        auto* registry=objects&&objects->position_registry?&*objects->position_registry:nullptr;
        const auto factory=make_object_trade_candidate_factory(resources,source,prepared,architecture,rooms,hooks,
            services.groups,selected,owner,guid,services.source_still_matches,support_ptr,registry,objects?&*objects:nullptr);
        if(command.kind==ObjectTradeSessionCommandKind::purchase)
            result=prepare_ts1_actorless_object_purchase(state,{selected,metadata->catalog,command.position,ids},resource,factory);
        else{
            ObjectSaleRequest request;request.target=command.target;request.groups=services.groups;
            request.move_objects=command.move_objects;request.avatar_threads=services.avatar_threads;
            // An empty complete list is proved only by an actual avatar-free World.
            const bool no_avatars=std::none_of(state.world.entities.begin(),state.world.entities.end(),[](const auto& e){return e.kind==EntityKind::avatar;});
            request.avatar_threads_complete=no_avatars||services.avatar_threads_complete;
            request.buildable=ObjectTradeBuildableArea{prepared.width,prepared.height,5,0,0,
                static_cast<std::int32_t>(prepared.width),static_cast<std::int32_t>(prepared.height),
                std::vector<bool>(prepared.buildable.begin(),prepared.buildable.end())};
            if(std::any_of(state.created_objects.begin(),state.created_objects.end(),[&](const auto& o){return o.id==selected&&o.generation==command.target.generation;}))request.selected_disabled=0;
            if(command.kind==ObjectTradeSessionCommandKind::sell)
                result=prepare_ts1_actorless_object_sale(state,request,resource,factory);
            else result=prepare_ts1_actorless_object_move(state,{request,command.position},resource,factory);
        }
        if(result.candidate){
            need(services.source_still_matches(),"community object command source changed before staging");
            need(encode_actorless_object_trade_state(*result.candidate)==result.archive,
                "community object final candidate differs from its first captured archive");
            (void)decode_actorless_object_trade_state(result.archive,&state.resources);
        }
    }catch(const std::exception& e){result={};result.diagnostic=e.what();}
    return result;
}
}
