// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "household_placement_adapter.hpp"
#include "live_wall_geometry.hpp"
#include "resource.hpp"
#include "odh/definition_index.hpp"
#include "odh/wall_route_state.hpp"
#include "odh/random.hpp"
#include "odh/ts1_placement_entry_point.hpp"
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
namespace odh_host {
struct HouseholdPlacementResource {
    std::string source,entry;
    std::shared_ptr<const std::string> frozen;
};
// Shared resource owner. Preload outside execution; lookup never does file I/O.
// Global100 belongs to the exact selected Global resource. Private SLOT belongs
// to the target's selected OBJD/resource, never to the source frame's CodeOwner.
class HouseholdPlacementSlotCatalog {
public:
    HouseholdPlacementSlotCatalog(const odh::DefinitionIndex& definitions,HouseholdPlacementResource globals)
        :definitions_(definitions),globals_(read(globals,100,{})){}
    void preload(const odh::World& world,const std::map<std::int16_t,odh::DefinitionCandidate>& avatar_definitions={}){
        for(std::int16_t cursor=0;;){const auto next=world.next_entity(cursor);if(!next)break;cursor=*next;
            const auto* entity=world.find(cursor);if(!entity||!entity->definition)continue;
            const auto override=avatar_definitions.find(cursor);
            const auto* definition=override!=avatar_definitions.end()?&override->second:definitions_.unique(entity->definition->guid);
            if(!definition)continue;
            if(definition->definition.guid!=entity->definition->guid)throw odh::FormatError("placement SLOT definition identity mismatch");
            const auto key=std::make_tuple(definition->source,definition->asset,definition->chunk_id);
            if(!private_.count(key)){
                if(!admit_resource(key,definition->definition.guid,read({definition->source,definition->asset,{}},{},*definition)))
                    throw odh::FormatError("placement SLOT admission conflicts with a decoded resource");
            }else guid_keys_[definition->definition.guid].insert(key);
            entities_[cursor]={entity->generation,entity->definition->guid,key};
        }
    }
    // Safe creation boundary, before Init/EP8/Main. These admissions perform no
    // I/O and never snapshot the World. Cached GUID aliases must be unambiguous.
    bool bind_cached(const odh::World& world,odh::Ts1PlacementIdentity identity){
        const auto* entity=world.find(identity.id);
        if(!entity||entity->generation!=identity.generation||!entity->definition)return false;
        const auto known=guid_keys_.find(entity->definition->guid);
        if(known==guid_keys_.end()||known->second.size()!=1)return false;
        entities_[identity.id]={identity.generation,entity->definition->guid,*known->second.begin()};return true;
    }
    bool bind_created(const odh::World& world,odh::Ts1PlacementIdentity identity,
        const odh::DefinitionCandidate& definition,const odh::ObjdBinding& binding,
        std::shared_ptr<const odh::SlotTable> decoded,bool slot_absence_known=false){
        const auto* entity=world.find(identity.id);
        if(!entity||entity->generation!=identity.generation||!entity->definition||
            entity->definition->guid!=definition.definition.guid||binding.guid!=definition.definition.guid||
            binding.master_id!=definition.definition.master_id||binding.sub_index!=definition.definition.sub_index||
            (!decoded&&!slot_absence_known))return false;
        if(entity->kind==odh::EntityKind::object){const auto* selected=definitions_.unique(binding.guid);
            if(!selected||selected->source!=definition.source||selected->asset!=definition.asset||selected->chunk_id!=definition.chunk_id)return false;}
        const Key key{definition.source,definition.asset,definition.chunk_id};
        odh::Ts1SlotLookup resource{decoded?odh::Ts1SlotLookupStatus::available:odh::Ts1SlotLookupStatus::unsupported,
            std::move(decoded),"selected private SLOT resource is absent"};
        if(!admit_resource(key,binding.guid,std::move(resource)))return false;
        entities_[identity.id]={identity.generation,binding.guid,key};return true;
    }
    odh::Ts1SlotLookup lookup(const odh::World& world,const odh::Ts1PlacementInvocation& inv,odh::Ts1PlacementIdentity target,bool global)const{
        if(!inv.source_still_matches||!inv.source_still_matches())return {};
        const auto* entity=world.find(target.id);if(!entity||entity->generation!=target.generation)return {};
        if(global)return globals_;
        const auto bound=entities_.find(target.id);
        if(bound==entities_.end()||bound->second.generation!=target.generation||!entity->definition||bound->second.guid!=entity->definition->guid)
            return {odh::Ts1SlotLookupStatus::unsupported,{},"target SLOT resource was not preloaded for this generation"};
        return private_.at(bound->second.key);
    }
private:
    using Key=std::tuple<std::string,std::string,std::uint16_t>;
    struct Bound {std::uint64_t generation;std::uint32_t guid;Key key;};
    static bool same_table(const odh::SlotTable& a,const odh::SlotTable& b){
        if(a.reserved!=b.reserved||a.version!=b.version||a.remaining_bytes!=b.remaining_bytes||a.records.size()!=b.records.size())return false;
        return std::equal(a.records.begin(),a.records.end(),b.records.begin(),[](const auto& x,const auto& y){
            return x.type==y.type&&x.offset_bits==y.offset_bits&&x.standing==y.standing&&x.sitting==y.sitting&&x.ground==y.ground&&
                x.flags==y.flags&&x.snap_target_slot==y.snap_target_slot&&x.proximity==y.proximity&&x.max_size==y.max_size&&
                x.unknown_i10==y.unknown_i10&&x.gradient_bits==y.gradient_bits&&x.height==y.height&&x.facing==y.facing&&x.resolution==y.resolution;});
    }
    bool admit_resource(const Key& key,std::uint32_t guid,odh::Ts1SlotLookup resource){
        const auto old=private_.find(key);
        if(old!=private_.end()){
            if(old->second.status!=resource.status||bool(old->second.table)!=bool(resource.table)||
                (resource.table&&!same_table(*old->second.table,*resource.table)))return false;
        }else{
            const auto count=resource.table?resource.table->records.size():0;
            if(count>262144-records_)return false;
            private_.emplace(key,std::move(resource));records_+=count;
        }
        guid_keys_[guid].insert(key);return true;
    }
    odh::Ts1SlotLookup read(const HouseholdPlacementResource& resource,std::optional<std::uint16_t> global_id,
        std::optional<odh::DefinitionCandidate> definition){
        std::optional<odh::ObjdBinding> binding;std::shared_ptr<odh::SlotTable> selected;
        const auto parse=[&](const odh::View& view){
            odh::Limits limits;limits.iff_chunks=4096;
            if(definition)odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="OBJD"&&chunk.id==definition->chunk_id){
                if(binding)throw odh::FormatError("ambiguous placement OBJD");binding=odh::read_objd_binding(chunk.payload);}},limits);
            if(definition&&(!binding||binding->guid!=definition->definition.guid))throw odh::FormatError("placement OBJD provenance mismatch");
            const auto slot_id=global_id?*global_id:binding->slot_table;
            odh::iff_chunks(view,[&](const auto& chunk){if(chunk.type=="SLOT"&&chunk.id==slot_id){
                if(selected)throw odh::FormatError("ambiguous placement SLOT resource");selected=std::make_shared<odh::SlotTable>(odh::read_slot(chunk.payload));}},limits);
        };
        if(resource.frozen){std::istringstream stream(*resource.frozen,std::ios::binary);parse(odh::View(stream,resource.frozen->size()));}
        else if(resource.entry.empty())with_iff_file(resource.source,parse);
        else with_far_entry(resource.source,resource.entry,parse);
        if(selected)return {odh::Ts1SlotLookupStatus::available,std::move(selected),{}};
        return {global_id?odh::Ts1SlotLookupStatus::missing:odh::Ts1SlotLookupStatus::unsupported,{},"selected private SLOT resource is absent"};
    }
    const odh::DefinitionIndex& definitions_;std::size_t records_=0;
    odh::Ts1SlotLookup globals_;std::map<Key,odh::Ts1SlotLookup> private_;std::map<std::int16_t,Bound> entities_;
    std::map<std::uint32_t,std::set<Key>> guid_keys_;
};
struct HouseholdPlacementAssembly {
    odh::World& world;
    const odh::PreparedArchitecture& prepared;
    const odh::SavedArchitecture& architecture;
    const odh::GeometricRoomMap& rooms;
    odh::AvatarPositionValid& avatar_position;
    HouseholdPlacementSlotCatalog& slots;
    odh::RandomState& random;
    // Real immutable resource styles, derived during lot preparation. Live
    // transforms/flags are overlaid for every wall query after preceding EP5.
    std::vector<odh::WallAppearanceInput> wall_inputs;
    std::function<std::vector<odh::ObjectTradeGroup>()> groups;
    std::function<odh::Vm*(std::int16_t)> vm_for;
    std::function<odh::Playback*(std::int16_t)> playback_for;
    std::function<odh::Routing*(std::int16_t)> routing_for;
    std::function<SessionObjectPositionContext(const odh::Ts1PlacementInvocation&)> object_context;
    std::function<bool(const odh::Ts1PlacementInvocation&,odh::Ts1PlacementIdentity)> avatar_entry_point9,shoo,moved_self;
    std::function<SessionObjectPositionContext(const odh::Ts1LifecyclePlacementInvocation&)> lifecycle_object_context;
    odh::Ts1SlotDiagnosticsObserver slot_diagnostics;
    std::function<SessionObjectPositionContext(const odh::Ts1PlacementInvocation&,odh::Ts1PlacementIdentity)> object_context_for;
};
// All handlers retain the owning assembly. Root supplies only actual thread,
// resource, group and entry-point authorities already owned by its live host.
inline odh::HouseholdPlacementBindings make_household_placement_bindings(std::shared_ptr<HouseholdPlacementAssembly> state){
    if(!state)throw odh::FormatError("placement assembly lifetime absent");
    odh::HouseholdPlacementBindings bindings{state->world,state->prepared,state->rooms,state->avatar_position};
    bindings.source_architecture=&state->architecture;
    bindings.object_context=state->object_context;
    bindings.object_context_for=state->object_context_for;
    bindings.lifecycle_object_context=state->lifecycle_object_context;
    bindings.slot_diagnostics=state->slot_diagnostics;
    bindings.slot_lifecycle=[state](const odh::Ts1PlacementInvocation& invocation){
        if(!state->object_context)throw odh::FormatError("slot lifecycle factory unavailable");
        auto context=state->object_context(invocation);
        if(context.world!=&state->world)throw odh::FormatError("slot lifecycle borrowed another World");
        context.source_still_matches=invocation.source_still_matches;
        return make_session_object_position_services(std::move(context));};
    bindings.execution=[state](odh::Ts1PlacementIdentity id)->std::optional<odh::HouseholdPlacementExecution>{
        const auto* avatar=state->world.find(id.id);if(!avatar||avatar->generation!=id.generation||!state->vm_for||!state->playback_for)return {};
        auto* vm=state->vm_for(id.id);auto* playback=state->playback_for(id.id);if(!vm||!playback)return {};
        return odh::HouseholdPlacementExecution{vm,playback,id.generation};};
    bindings.slots=[state](const auto& inv,auto target,bool global){return state->slots.lookup(state->world,inv,target,global);};
    bindings.group=[state](odh::Ts1PlacementIdentity target)->std::optional<std::vector<odh::Ts1PlacementIdentity>>{
        if(!state->groups)return {};std::optional<std::vector<odh::Ts1PlacementIdentity>> result;
        for(const auto& group:state->groups()){
            const auto found=std::find_if(group.members.begin(),group.members.end(),[&](const auto& member){return member.id==target.id;});
            if(found==group.members.end())continue;if(result||!group.complete||found->generation!=target.generation)return {};
            result.emplace();for(const auto member:group.members){const auto* live=state->world.find(member.id);
                if(!live||live->generation!=member.generation)return {};result->push_back({member.id,member.generation});}}
        return result;};
    const auto wall_diagnostic=std::make_shared<std::string>();
    bindings.wall_edges=[state,wall_diagnostic](std::int16_t level)->std::optional<std::vector<odh::ResolvedWallEdges>>{
        auto edges=live_household_wall_edges(state->world,state->architecture,state->wall_inputs,level);
        *wall_diagnostic=edges.diagnostic;
        if(edges.status!=odh::WallEdgeBuildStatus::ready)return {};
        return std::move(edges.edges);
    };
    bindings.wall_diagnostic=[wall_diagnostic]{return *wall_diagnostic;};
    bindings.destination_conflict=[state](const auto& inv,auto caller,const odh::Ts1PlacementGoal& goal)->std::optional<bool>{
        if(!state->vm_for||!state->routing_for||!inv.source_still_matches())return {};
        for(std::int16_t cursor=0;;){const auto next=state->world.next_entity(cursor,odh::EntityKind::avatar);if(!next)break;cursor=*next;
            if(cursor==caller.id)continue;auto* vm=state->vm_for(cursor);auto* routing=state->routing_for(cursor);
            if(!vm)return {}; // Native avatars have threads; absence is not no route.
            if(routing){const auto intersects=routing->intersects_destination({goal.position.x,goal.position.y},goal.chair.id?std::optional<std::int16_t>{goal.chair.id}:std::nullopt);
                if(!intersects)return {};if(*intersects)return true;}
            else{for(const auto& frame:vm->stack())if(frame.return_mode==odh::VmCallReturnMode::external_frame_next_tick)return {};
                if(vm->pending()&&(vm->pending()->instruction.instruction.opcode==27||vm->pending()->instruction.instruction.opcode==45))return {};}}
        return false;};
    bindings.random=[state](const auto& inv,std::uint64_t limit)->std::optional<std::uint64_t>{
        if(!limit||!inv.source_still_matches())return {};return state->random.next(limit);};
    bindings.avatar_entry_point9=[state](const odh::Ts1PlacementInvocation& invocation,odh::Ts1PlacementIdentity avatar){
        if(state->avatar_entry_point9)return state->avatar_entry_point9(invocation,avatar);
        if(!state->object_context||!state->vm_for)return false;
        auto context=state->object_context(invocation);auto* thread=state->vm_for(avatar.id);
        if(context.world!=&state->world||!thread||!context.owner_for||!context.primitive_factory)return false;
        const auto owner=context.owner_for(avatar.id);if(!owner)return false;
        odh::Ts1AvatarPlacementEntryPointRequest request;request.world=&state->world;request.invocation=&invocation;
        request.avatar=avatar;request.code_owner=*owner;request.register_owner=thread;
        request.global_program=thread->shared_program_for_owner(0);request.owner_program=thread->shared_program_for_owner(*owner);
        request.live_memory=context.memory;request.check_primitive=context.primitive_factory(context.memory);
        request.identities_still_match=[state,avatar,thread]{const auto* entity=state->world.find(avatar.id);
            return entity&&entity->generation==avatar.generation&&state->vm_for(avatar.id)==thread;};
        return odh::evaluate_ts1_avatar_placement_entry_point(request).state!=odh::Ts1PlacementEntryPointState::unsupported;};
    bindings.shoo=state->shoo;bindings.moved_self=state->moved_self;
    return bindings;
}
}
