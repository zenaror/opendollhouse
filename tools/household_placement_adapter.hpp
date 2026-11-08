// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ts1_placement_slots.hpp"
#include "odh/avatar_position_valid.hpp"
#include "odh/object_position_change.hpp"
#include "odh/object_position_registry.hpp"
#include "odh/playback.hpp"
#include "odh/prepared_architecture.hpp"
#include "odh/room_map.hpp"
#include "odh/route_geometry.hpp"
#include "object_trade_session_adapter.hpp"
#include <array>
#include <stdexcept>
namespace odh {
struct HouseholdPlacementExecution {Vm* vm=nullptr;Playback* playback=nullptr;std::uint64_t generation=0;};
struct HouseholdPlacementBindings {
    World& world;
    const PreparedArchitecture& architecture;
    const GeometricRoomMap& rooms;
    AvatarPositionValid& avatar_position;
    const SavedArchitecture* source_architecture=nullptr;
    // Shared lifecycle factory owned by the host: existing or contained object
    // moves use the same ObjectPositionChange services as property commands.
    std::function<odh_host::SessionObjectPositionContext(const Ts1PlacementInvocation&)> object_context;
    // The same shared source PrePositionChange/PositionChange implementation
    // used by ordinary floor movement, specialized for a contained transfer.
    std::function<odh_host::SessionObjectPositionServices(const Ts1PlacementInvocation&)> slot_lifecycle;
    // Runtime bindings/resources are host-owned, dynamically looked up by exact
    // identity. The adapter never captures a disposable Check VM as a Thread.
    std::function<std::optional<HouseholdPlacementExecution>(Ts1PlacementIdentity)> execution;
    Ts1PlacementSlots slots;
    std::function<std::optional<std::vector<Ts1PlacementIdentity>>(Ts1PlacementIdentity)> group;
    std::function<std::optional<std::vector<ResolvedWallEdges>>(std::int16_t)> wall_edges;
    std::function<std::optional<bool>(const Ts1PlacementInvocation&,Ts1PlacementIdentity,const Ts1PlacementGoal&)> destination_conflict;
    std::function<std::optional<std::uint64_t>(const Ts1PlacementInvocation&,std::uint64_t)> random;
    // Runs actual Avatar PositionChange base EP9, unless OOW/contained, using
    // its real resource and registers. A true return proves execution, not its
    // authored BHAV result. World owns physical subtree/Room29 publication.
    std::function<bool(const Ts1PlacementInvocation&,Ts1PlacementIdentity)> avatar_entry_point9;
    std::function<bool(const Ts1PlacementInvocation&,Ts1PlacementIdentity)> shoo,moved_self;
    std::function<odh_host::SessionObjectPositionContext(const Ts1LifecyclePlacementInvocation&)> lifecycle_object_context;
    std::function<std::string()> wall_diagnostic;
    Ts1SlotDiagnosticsObserver slot_diagnostics;
    // Optional host observation of a real placement attempt. No callbacks run
    // or candidates change when observation is disabled.
    std::int16_t observe_find_target=0;
    std::function<void(const Ts1PlacementInvocation&,Ts1PlacementIdentity,std::optional<EntityPosition>,
        std::optional<EntityPosition>,std::uint8_t,const Ts1PlacementResult&)> observe_find_attempt;
    // The moving identity is explicit: source Caller/StackObject can differ.
    // Absent retains the original host/context and slot lifecycle factories.
    std::function<odh_host::SessionObjectPositionContext(const Ts1PlacementInvocation&,Ts1PlacementIdentity)> object_context_for;
};
// App adapter: geometry, tile queries, real PositionValid/PositionChange,
// reciprocal slot transfer and carrier animation publish together. This object
// must outlive all returned handlers; World/architecture/resources must outlive it.
class HouseholdPlacementAdapter {
public:
    explicit HouseholdPlacementAdapter(HouseholdPlacementBindings bindings):bindings_(std::move(bindings)),
        controller_(bindings_.world,make_services()){}
    HouseholdPlacementAdapter(const HouseholdPlacementAdapter&)=delete;
    HouseholdPlacementAdapter& operator=(const HouseholdPlacementAdapter&)=delete;
    VmPrimitive primitive(){return controller_.primitive();}
    Ts1PlacementSlots slots()const{return bindings_.slots;}
    Ts1PlacementResult transfer(const Ts1PlacementInvocation& invocation,Ts1PlacementIdentity container,std::size_t slot,Ts1PlacementIdentity item){
        if(!valid(invocation,container)||!valid(invocation,item))return unavailable("transfer source identity unavailable");
        VmFrame scoped;scoped.context.caller=container.id;
        if(slot>32767)return unavailable("container slot outside signed source index");
        const auto occupant=bindings_.world.read(scoped,20,static_cast<std::int16_t>(slot));
        if(!occupant)return unavailable("container slot extent unknown");
        if(*occupant==item.id)return success();
        if(*occupant)return blocked();
        const auto* ancestor=bindings_.world.find(container.id);std::size_t depth=0;
        while(ancestor){
            if(ancestor->id==item.id)return bindings_.world.find(container.id)->kind==EntityKind::object?blocked():unavailable("avatar containment cycle is outside safe source execution");
            if(!ancestor->attachment)break;
            if(++depth>4096)return unavailable("container ancestry exceeds budget");
            const auto* parent=bindings_.world.find(ancestor->attachment->container);
            if(!parent||ancestor->attachment->slot>=parent->slots.size()||parent->slots[ancestor->attachment->slot]!=ancestor->id)return unavailable("container ancestry is not reciprocal");
            ancestor=parent;
        }
        const auto* source=bindings_.world.find(item.id);const auto old=source->attachment;
        auto move=bindings_.world.prepare_move_to_slot(container.id,slot,item.id);
        if(!move)return unavailable("reciprocal containment/cycle/budget preflight unavailable");
        std::array<std::int16_t,2> carriers{};std::size_t count=0;
        if(old){const auto* parent=bindings_.world.find(old->container);if(!parent)return unavailable("old carrier identity unavailable");
            if(parent->kind==EntityKind::avatar)carriers[count++]=parent->id;}
        if(bindings_.world.find(container.id)->kind==EntityKind::avatar&&slot==0&&(!count||carriers[0]!=container.id))carriers[count++]=container.id;
        std::array<std::optional<Playback::PreparedCarryChange>,2> plans;
        for(std::size_t n=0;n<count;++n){const auto* carrier=bindings_.world.find(carriers[n]);const Ts1PlacementIdentity id{carrier->id,carrier->generation};
            const auto execution=binding(id);if(!execution)return unavailable("real carrier Playback/thread unavailable");
            std::string why;plans[n]=execution->playback->prepare_slot_carry_change(*move,invocation.frame,*invocation.dispatch.origin,
                invocation.instruction,invocation.dispatch,id.id,id.generation,execution->vm->run_generation(),&why);
            if(!plans[n])return unavailable(why);}
        if(!bindings_.object_context_for&&!bindings_.slot_lifecycle)return unavailable("shared slot PositionChange lifecycle unavailable");
        odh_host::SessionObjectPositionServices lifecycle;
        if(bindings_.object_context_for){
            auto context=bindings_.object_context_for(invocation,item);
            if(context.world!=&bindings_.world)return unavailable("slot factory borrowed another World");
            context.source_still_matches=invocation.source_still_matches;
            lifecycle=odh_host::make_session_object_position_services(std::move(context));
        }else lifecycle=bindings_.slot_lifecycle(invocation);
        if(!lifecycle.transfer_to_slot)return unavailable("shared slot lifecycle callbacks incomplete");
        if(!valid(invocation,container)||!valid(invocation,item)||!bindings_.world.can_commit_move_to_slot(*move))return unavailable("slot plan changed before publication");
        for(std::size_t n=0;n<count;++n)if(!plans[n]->can_commit())return unavailable("carry plan changed before publication");
        const auto transferred=lifecycle.transfer_to_slot({item.id,item.generation},{container.id,container.generation},slot);
        if(!transferred.completed)return unavailable(transferred.diagnostic);
        if(!transferred.placed)return blocked();
        if(!valid(invocation,item)||!valid(invocation,container))return unavailable("slot identity changed after source PositionChange");
        for(std::size_t n=0;n<count;++n)if(!plans[n]->can_publish())return unavailable("carry cannot publish after slot movement");
        for(std::size_t n=0;n<count;++n)if(!plans[n]->publish())return unavailable("prepared carry publication failed");
        return success();
    }
    Ts1PlacementResult place(const Ts1PlacementInvocation& invocation,Ts1PlacementIdentity target,
        std::optional<EntityPosition> destination,std::uint8_t flags){
        if(!valid(invocation,target))return unavailable("floor source identity unavailable");
        const EntityPosition point=destination.value_or(EntityPosition{-32768,-32768,1,0});
        destination=scalar_floor_destination(destination);
        const auto* entity=bindings_.world.find(target.id);const auto old=entity->attachment;
        std::optional<Playback::PreparedFloorCarryClear> carry;
        if(old){const auto* parent=bindings_.world.find(old->container);if(!parent)return unavailable("floor old carrier absent");
            if(parent->kind==EntityKind::avatar){const auto execution=binding({parent->id,parent->generation});if(!execution)return unavailable("floor carrier Playback/thread unavailable");
                std::string why;carry=execution->playback->prepare_floor_carry_clear(invocation.frame,*invocation.dispatch.origin,invocation.instruction,invocation.dispatch,
                    target.id,target.generation,destination,parent->id,parent->generation,execution->vm->run_generation(),&why);
                if(!carry)return unavailable(why);}}
        if(carry&&!carry->can_commit())return unavailable("floor carry preflight changed");
        if(entity->kind==EntityKind::avatar){
            if(!bindings_.slot_lifecycle)return unavailable("avatar floor lifecycle factory unavailable");
            auto lifecycle=bindings_.slot_lifecycle(invocation);
            std::optional<odh_host::SessionObjectPositionContext> context;
            if(bindings_.object_context)context=bindings_.object_context(invocation);
            const auto authorization=bindings_.avatar_position.prepare(invocation.frame,*invocation.dispatch.origin,
                invocation.instruction,invocation.dispatch,target.id,target.generation);
            if(!authorization)return unavailable("avatar floor source authorization could not be captured before Pre");
            const auto placed=avatar_floor(std::move(context),std::move(lifecycle),target,destination,point,
                invocation.source_still_matches,[&]{return bindings_.avatar_position.evaluate_prepared(*authorization,
                    invocation.frame,*invocation.dispatch.origin,invocation.instruction,invocation.dispatch,
                    target.id,target.generation,point,point.direction,flags);});
            if(placed.status!=Ts1PlacementStatus::success)return placed;
        }else{
            if(!bindings_.object_context_for&&!bindings_.object_context)return unavailable("shared object PositionChange factory unavailable");
            auto context=bindings_.object_context_for?bindings_.object_context_for(invocation,target):bindings_.object_context(invocation);
            const auto placed=object_floor(std::move(context),target,point,flags,invocation.source_still_matches);
            if(placed.status!=Ts1PlacementStatus::success)return placed;
        }
        if(carry&&(!carry->can_publish()||!carry->publish()))return unavailable("floor carry publication changed after movement");
        return valid(invocation,target)?success():unavailable("floor source identity changed after placement");
    }
    Ts1PlacementResult place_lifecycle(const Ts1LifecyclePlacementInvocation& invocation,
            Ts1PlacementIdentity target,std::optional<EntityPosition> destination,std::uint8_t direction,std::uint8_t flags=0){
        const auto* entity=bindings_.world.find(target.id);
        if(!invocation.source_still_matches||!invocation.source_still_matches()||
           !bindings_.world.retired_container_active(invocation.container)||
           !bindings_.world.retired_child_authorized(invocation.container,{target.id,target.generation})||!entity||entity->generation!=target.generation)
            return unavailable("source Delete placement identity/capability unavailable");
        if(!bindings_.lifecycle_object_context)return unavailable("source Delete position factory unavailable");
        const auto point=destination.value_or(EntityPosition{-32768,-32768,1,direction});
        destination=scalar_floor_destination(destination);
        auto context=bindings_.lifecycle_object_context(invocation);
        if(entity->kind==EntityKind::avatar){
            if(context.world!=&bindings_.world)return unavailable("source Delete avatar factory borrowed another World");
            auto lifecycle=odh_host::make_session_object_position_services(context);
            LiveRetiredContainerPlacementSource source{&bindings_.world,invocation.container,
                {target.id,target.generation},invocation.source_still_matches};
            return avatar_floor(std::move(context),std::move(lifecycle),target,destination,point,
                invocation.source_still_matches,[&]{return bindings_.avatar_position.evaluate_lifecycle(source,point,point.direction,flags);});
        }
        return object_floor(std::move(context),target,point,flags,invocation.source_still_matches);
    }
    Ts1PlacementResult find_lifecycle(const Ts1LifecyclePlacementInvocation& invocation,Ts1PlacementIdentity target){
        Ts1RadialPlacementServices services;services.source_still_matches=invocation.source_still_matches;
        services.reference_position=[&]()->std::optional<EntityPosition>{
            if(!bindings_.world.retired_container_active(invocation.container))return {};
            const auto* parent=bindings_.world.resolve_reference(invocation.container.parent());
            return parent?parent->position:std::optional<EntityPosition>{};
        };
        services.place=[&](EntityPosition point){return place_lifecycle(invocation,target,point,point.direction,0);};
        return ts1_find_location_radial(services,false);
    }
    Ts1PlacementLocations locations(const Ts1PlacementInvocation& invocation,const SlotRecord& slot,Ts1PlacementIdentity target,Ts1PlacementIdentity caller,
        const RoutePlacementContext* route_context=nullptr){
        Ts1SlotLocationsServices services;services.group=bindings_.group;
        services.out_of_bounds=[this](const EntityPosition& point)->std::optional<bool>{
            if(!bindings_.architecture.width||!bindings_.architecture.height)return {};
            // VMContext.IsOutOfBounds checks XY and level through architecture.
            return point.x<0||point.y<0||point.x/16>=static_cast<std::int32_t>(bindings_.architecture.width)||
                point.y/16>=static_cast<std::int32_t>(bindings_.architecture.height)||point.level<1||point.level>2;};
        services.room_at=[this](const EntityPosition& point){return geometric_room_at_runtime(bindings_.rooms,point.x,point.y,point.level);};
        services.raycast_wall=[this,&invocation,target,caller](std::int32_t x,std::int32_t y,std::int32_t tx,std::int32_t ty,std::int16_t level)->std::optional<bool>{
            const auto unknown=[&](const std::string& stage,const std::string& detail){
                throw std::runtime_error("SLOT wall raycast stage="+stage+" from="+std::to_string(x)+","+std::to_string(y)+
                    " to="+std::to_string(tx)+","+std::to_string(ty)+" level="+std::to_string(level)+
                    " extent="+std::to_string(bindings_.architecture.width)+"x"+std::to_string(bindings_.architecture.height)+
                    " caller="+std::to_string(caller.id)+":"+std::to_string(caller.generation)+
                    " target="+std::to_string(target.id)+":"+std::to_string(target.generation)+
                    " owner="+std::to_string(invocation.frame.context.code_owner)+
                    " routine="+std::to_string(invocation.frame.key.id)+" pc="+std::to_string(invocation.frame.pc)+
                    " opcode="+std::to_string(invocation.instruction.opcode)+" detail="+detail.substr(0,768));
            };
            if(!bindings_.wall_edges){unknown("wall-provider","unavailable");return {};}
            const auto edges=bindings_.wall_edges(level);
            if(!edges){unknown("wall-inputs",bindings_.wall_diagnostic?bindings_.wall_diagnostic():"provider returned unknown");return {};}
            if(level<1||level>2){unknown("level","unsupported");return {};}
            SavedArchitecture fallback;const auto* source=bindings_.source_architecture;
            if(!source){
                fallback.width=bindings_.architecture.width;fallback.height=bindings_.architecture.height;
                for(std::size_t n=0;n<2;++n){fallback.levels[n].walls=bindings_.architecture.levels[n].walls;
                    fallback.levels[n].floors=bindings_.architecture.levels[n].floors;}
                source=&fallback;
            }
            const auto result=saved_wall_raycast(*source,level,x,y,tx,ty,*edges);
            if(result.state==WallRaycastState::unsupported){
                unknown("raycast",std::string(wall_raycast_failure_name(result.failure))+" crossed="+
                    std::to_string(result.tile_x)+","+std::to_string(result.tile_y)+" top_left="+std::to_string(result.top_left));
                return {};
            }
            return result.state==WallRaycastState::blocked;
        };
        services.position_valid=[this,route_context](const auto& source,Ts1PlacementIdentity actor,const EntityPosition& point,std::uint8_t direction,std::uint8_t flags){
            const auto* entity=bindings_.world.find(actor.id);
            if(!entity||entity->generation!=actor.generation)return unavailable("candidate object identity unavailable");
            if(entity->kind==EntityKind::object){
                if(!bindings_.object_context_for&&!bindings_.object_context)return unavailable("object candidate factory unavailable");
                auto context=bindings_.object_context_for?bindings_.object_context_for(source,actor):bindings_.object_context(source);
                if(context.world!=&bindings_.world)return unavailable("object candidate factory borrowed another World");
                context.source_still_matches=source.source_still_matches;
                auto services=odh_host::make_session_object_position_services(std::move(context));
                ObjectPositionValid validator(bindings_.world,std::move(services.placement));
                ObjectPositionValidRequest request;request.target={actor.id,actor.generation};request.candidate=point;request.direction=direction;
                request.flags=flags;request.source_still_matches=source.source_still_matches;
                const auto result=validator.evaluate(request);
                return Ts1PlacementResult{result.status==ObjectPositionValidStatus::success?Ts1PlacementStatus::success:
                    result.status==ObjectPositionValidStatus::blocked?Ts1PlacementStatus::blocked:Ts1PlacementStatus::unsupported,
                    result.placement_error,{result.object.id,result.object.generation},result.diagnostic};
            }
            const auto result=route_context?
                bindings_.avatar_position.evaluate_route(*route_context,actor.id,actor.generation,point,direction,flags):
                bindings_.avatar_position.evaluate(source.frame,*source.dispatch.origin,source.instruction,source.dispatch,actor.id,actor.generation,point,direction,flags);
            return Ts1PlacementResult{result.status==AvatarPositionValidStatus::success?Ts1PlacementStatus::success:
                result.status==AvatarPositionValidStatus::blocked?Ts1PlacementStatus::blocked:Ts1PlacementStatus::unsupported,
                result.placement_error,{result.blocker,result.blocker_generation},result.diagnostic};};
        services.chair_entry_point26=[this](Ts1PlacementIdentity chair)->std::optional<bool>{const auto* entity=bindings_.world.find(chair.id);
            if(!entity||entity->generation!=chair.generation||!entity->entry_points||entity->entry_points->size()<=26)return {};
            return (*entity->entry_points)[26].action!=0;};
        services.architectural_door_at=[this](const EntityPosition& point)->std::optional<bool>{
            for(const auto id:objects_at(point)){const auto* e=bindings_.world.find(id);const auto flag=e->object_words.find(40);
                if(flag==e->object_words.end())return {};
                if(static_cast<std::uint16_t>(flag->second)&32768U)return true;}
            return false;};
        services.destination_conflict=bindings_.destination_conflict;services.random=bindings_.random;
        services.diagnostics=bindings_.slot_diagnostics;
        return ts1_find_slot_locations(bindings_.world,invocation,slot,target,caller,services);
    }
    Playback::ReachServices reach_services(std::function<std::shared_ptr<const AnimationHeader>(std::uint16_t)> animation){
        Playback::ReachServices services;services.animation=std::move(animation);
        services.slot_height=[this](std::int16_t container,std::size_t slot)->std::optional<float>{
            // Reach height runs in the actual primitive; resolve the caller's
            // live dispatch through reach_slot_height below rather than guessing.
            if(!reach_slot_height_)throw std::runtime_error("Reach SLOT context is not bound");
            return reach_slot_height_(container,slot);};
        services.transfer=[this](const auto& frame,const auto& instruction,const auto& dispatch,std::int16_t item,std::int16_t container,std::size_t slot,std::string& why)->std::optional<bool>{
            const auto* source=bindings_.world.find(item);const auto* target=bindings_.world.find(container);
            if(!source||!target||!dispatch.origin){why="Reach live transfer identities unavailable";return {};}
            Ts1PlacementInvocation invocation{frame,instruction,dispatch,[&]{return dispatch.origin->matches_active_primitive(frame,instruction,dispatch);}};
            const auto result=transfer(invocation,{container,target->generation},slot,{item,source->generation});why=result.diagnostic;
            if(result.status==Ts1PlacementStatus::unsupported)return {};
            return result.status==Ts1PlacementStatus::success;};
        return services;
    }
    // Wrapper retains the actual47 lookup context only for that synchronous
    // invocation. No fabricated frame/token or context survives the callback.
    VmPrimitive reach_primitive(Playback& playback,std::function<std::shared_ptr<const AnimationHeader>(std::uint16_t)> animation){
        auto handler=playback.reach_primitive(reach_services(std::move(animation)));
        return [this,handler=std::move(handler)](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->PrimitiveOutcome{
            if(reach_slot_height_)return {PrimitiveDisposition::unsupported,"Reach adapter reentry"};
            reach_slot_height_=[&](std::int16_t container,std::size_t slot)->std::optional<float>{
                const auto* entity=bindings_.world.find(container);if(!entity||!bindings_.slots)throw std::runtime_error("Reach SLOT owner unavailable");
                Ts1PlacementInvocation invocation{frame,instruction,dispatch,[&]{return dispatch.origin&&dispatch.origin->matches_active_primitive(frame,instruction,dispatch);}};
                const auto lookup=bindings_.slots(invocation,{container,entity->generation},false);
                if(lookup.status==Ts1SlotLookupStatus::missing)throw std::runtime_error("Reach private SLOT resource is absent");
                if(lookup.status!=Ts1SlotLookupStatus::available||!lookup.table)throw std::runtime_error("Reach SLOT resource unknown");
                const auto* record=slot_of_type(*lookup.table,0,slot);
                if(!record)throw std::runtime_error("Reach authored storage SLOT index is outside actual resource");
                const auto height=ts1_storage_slot_height(*record);if(!height)throw std::runtime_error("Reach SLOT height invalid");
                return height;};
            struct Guard{std::function<std::optional<float>(std::int16_t,std::size_t)>& fn;~Guard(){fn={};}} guard{reach_slot_height_};
            return handler(frame,instruction,temps,dispatch);};
    }
private:
    static std::optional<EntityPosition> scalar_floor_destination(std::optional<EntityPosition> destination){
        // LotTilePos.OUT_OF_WORLD is exactly(-32768,-32768,1), independent
        // of direction. PositionValid and ordinary-object ChangePosition already
        // accept this source point; World null represents its scalar geometry.
        // Normalize before carry/phase preparation, retaining the source point
        // separately for its heading and existing object/validation consumers.
        if(destination&&destination->x==-32768&&destination->y==-32768&&destination->level==1)return {};
        return destination;
    }
    Ts1PlacementResult avatar_floor(std::optional<odh_host::SessionObjectPositionContext> context,
            odh_host::SessionObjectPositionServices lifecycle,Ts1PlacementIdentity target,
            std::optional<EntityPosition> destination,EntityPosition point,const std::function<bool()>& authority,
            const std::function<AvatarPositionValidResult()>& evaluate){
        if(context&&context->world!=&bindings_.world)return unavailable("avatar floor factory borrowed another World");
        const auto valid_source=[&]{const auto* e=bindings_.world.find(target.id);
            return authority&&authority()&&e&&e->generation==target.generation;};
        if(!valid_source())return unavailable("avatar floor source unavailable");
        const auto* entity=bindings_.world.find(target.id);const auto old=entity->attachment;
            if(!lifecycle.before_slot_transfer||!lifecycle.after_slot_transfer||!lifecycle.change.story_count)
                return unavailable("avatar floor lifecycle services incomplete");
            const auto stories=lifecycle.change.story_count();
            if(!stories||*stories<1)return unavailable("avatar floor story count unknown");
            if(destination&&destination->level>*stories)return {Ts1PlacementStatus::blocked,18,{},{}};
            if(!entity->entry_points||entity->entry_points->size()<=15)return unavailable("avatar floor entry-point table unknown");
            const auto old_position=entity->position;
            const auto old_room=old_position?geometric_room_at_runtime(bindings_.rooms,old_position->x,old_position->y,old_position->level):std::optional<std::uint16_t>{0};
            const auto new_room=destination?geometric_room_at_runtime(bindings_.rooms,destination->x,destination->y,destination->level):std::optional<std::uint16_t>{0};
            // Out-of-bounds is a normal PositionValid false after Pre. Its
            // source room is zero, not a guessed successful placement.
            const bool outside=destination&&(destination->x<0||destination->y<0||destination->level<1||
                destination->x/16>=static_cast<std::int32_t>(bindings_.architecture.width)||
                destination->y/16>=static_cast<std::int32_t>(bindings_.architecture.height));
            if(!old_room||(!new_room&&!outside))return unavailable("avatar floor room lookup unavailable");
            const bool room_change=!destination||old.has_value()||entity->entry_points->at(15).action!=0||*old_room!=new_room.value_or(0);
            std::optional<ObjectPositionIdentity> old_parent;
            if(old){const auto parent=bindings_.world.attachment_parent({target.id,target.generation});
                if(!parent)return unavailable("avatar floor old container is not reciprocal");
                old_parent=ObjectPositionIdentity{parent->id,parent->generation};}
            const ObjectPositionIdentity root{target.id,target.generation};
            const auto phase=[&](odh_host::SessionPositionPhase stage,const std::optional<EntityPosition>& desired,const std::function<bool()>& operation){
                return context&&context->position_phase?context->position_phase(root,desired,stage,
                    stage==odh_host::SessionPositionPhase::publish?std::optional<std::uint8_t>{point.direction}:std::nullopt,operation):operation();
            };
            const auto detail=[&]{return lifecycle.lifecycle_diagnostic?lifecycle.lifecycle_diagnostic():std::string{};};
            struct EndFloor {ObjectPositionRegistrySnapshot* registry;bool completed=false;
                ~EndFloor(){if(registry&&!completed)registry->complete=false;}
            }end_floor{context?context->position_registry:nullptr};
            if(!phase(odh_host::SessionPositionPhase::pre,destination,[&]{return lifecycle.before_slot_transfer(root,room_change);})||!valid_source())
                return unavailable("avatar PrePositionChange: "+detail());
            AvatarPositionValidResult result;result.status=AvatarPositionValidStatus::success;
            if(destination)result=evaluate();
            if(result.status==AvatarPositionValidStatus::unsupported)return unavailable(result.diagnostic);
            // Carry remains prepared across source Pre detach; its exact final
            // destination and identity are checked after source Post below.
            if(!valid_source())return unavailable("avatar floor source changed after live EP5");
            if(result.status==AvatarPositionValidStatus::blocked){
                if(old_parent&&(!lifecycle.change.restore_container||!lifecycle.change.restore_container(root,*old_parent,old->slot)))
                    return unavailable("blocked avatar floor could not restore its original source slot");
                const auto* current=bindings_.world.find(target.id);const auto restored=current?current->position:std::optional<EntityPosition>{};
                if(!phase(odh_host::SessionPositionPhase::post,restored,[&]{return lifecycle.after_slot_transfer(root,room_change);})||!valid_source())
                    return unavailable("blocked avatar PositionChange: "+detail());
                end_floor.completed=true;
                return {Ts1PlacementStatus::blocked,result.placement_error,{result.blocker,result.blocker_generation},{}};
            }
            // Observe the actual fixed publisher once through the existing phase.
            // The receipt does not grant authority or retry a refused placement.
            const auto* before_publish=bindings_.world.find(target.id);
            const auto before_publish_position=before_publish?before_publish->position:std::optional<EntityPosition>{};
            const auto before_publish_attachment=before_publish?before_publish->attachment:std::optional<Attachment>{};
            std::size_t publish_calls=0;bool scalar_accepted=false;std::string scalar_detail;
            const bool publish_accepted=phase(odh_host::SessionPositionPhase::publish,destination,[&]{
                ++publish_calls;
                scalar_accepted=odh_host::publish_source_individual_position(bindings_.world,
                    {target.id,target.generation},destination,point.direction,valid_source,&scalar_detail);
                return scalar_accepted;
            });
            if(!publish_accepted){
                const auto pose=[](const std::optional<EntityPosition>& p){
                    return p?std::to_string(p->x)+","+std::to_string(p->y)+","+std::to_string(p->level)+","+
                        std::to_string(p->direction):std::string{"absent"};
                };
                const auto link=[](const std::optional<Attachment>& a){
                    return a?std::to_string(a->container)+","+std::to_string(a->slot)+","+
                        std::to_string(a->positioned):std::string{"absent"};
                };
                const auto* actual=bindings_.world.find(target.id);
                std::string why="avatar scalar floor publication failed: phase=publish target="+std::to_string(target.id)+
                    "/"+std::to_string(target.generation)+" callback_calls="+std::to_string(publish_calls)+
                    " scalar_accepted="+std::to_string(scalar_accepted)+" reason="+
                    (publish_calls?scalar_accepted?"phase refused after scalar callback":scalar_detail:"phase refused before scalar callback")+
                    " old="+pose(old_position)+" old_attachment="+link(old)+" before_publish="+pose(before_publish_position)+
                    " before_attachment="+link(before_publish_attachment)+" destination="+pose(destination)+
                    " direction="+std::to_string(point.direction)+" actual_generation="+std::to_string(actual?actual->generation:0)+
                    " actual="+pose(actual?actual->position:std::optional<EntityPosition>{})+
                    " actual_attachment="+link(actual?actual->attachment:std::optional<Attachment>{})+
                    " registry_complete="+(context&&context->position_registry?std::to_string(context->position_registry->complete):"unknown");
                const auto lifecycle_detail=detail();if(!lifecycle_detail.empty())why+=" lifecycle="+lifecycle_detail;
                return unavailable(why);
            }
            if(!phase(odh_host::SessionPositionPhase::post,destination,[&]{return lifecycle.after_slot_transfer(root,room_change);})||!valid_source())
                return unavailable("avatar PositionChange: "+detail());
            end_floor.completed=true;
        return success();
    }
    Ts1PlacementResult object_floor(odh_host::SessionObjectPositionContext context,Ts1PlacementIdentity target,
            EntityPosition point,std::uint8_t flags,const std::function<bool()>& authority){
        if(context.world!=&bindings_.world)return unavailable("object factory borrowed another World");
        context.source_still_matches=authority;
        auto* registry=context.position_registry;
        auto services=odh_host::make_session_object_position_services(std::move(context));
        const auto lifecycle_diagnostic=services.lifecycle_diagnostic;
        ObjectPositionValid validator(bindings_.world,std::move(services.placement));
        ObjectPositionChange movement(bindings_.world,validator,std::move(services.change));
        ObjectPositionValidRequest request;request.target={target.id,target.generation};request.candidate=point;
        request.direction=point.direction;request.flags=flags;request.source_still_matches=authority;
        const auto result=movement.apply_existing(request);
        if(result.status==ObjectPositionChangeStatus::unsupported){
            if(registry&&result.effects_started)registry->complete=false;
            return unavailable(result.diagnostic+(lifecycle_diagnostic?" "+lifecycle_diagnostic():std::string{}));
        }
        if(result.status==ObjectPositionChangeStatus::blocked)return {Ts1PlacementStatus::blocked,result.placement.placement_error,
            {result.placement.object.id,result.placement.object.generation},result.placement.diagnostic};
        return success();
    }
    static Ts1PlacementResult unavailable(std::string why){return {Ts1PlacementStatus::unsupported,-1,{},std::move(why)};}
    static Ts1PlacementResult success(){return {Ts1PlacementStatus::success,-1,{},{}};}
    static Ts1PlacementResult blocked(){return {Ts1PlacementStatus::blocked,10,{},{}};}
    bool valid(const Ts1PlacementInvocation& invocation,Ts1PlacementIdentity id)const{const auto* e=bindings_.world.find(id.id);
        return invocation.source_still_matches&&invocation.source_still_matches()&&e&&e->generation==id.generation;}
    std::optional<HouseholdPlacementExecution> binding(Ts1PlacementIdentity id){if(!bindings_.execution)return {};
        const auto result=bindings_.execution(id);if(!result||!result->vm||!result->playback||result->generation!=id.generation)return {};
        return result;}
    std::vector<std::int16_t> objects_at(const EntityPosition& point)const{
        const auto offset=[this](const std::optional<EntityPosition>& p)->std::int64_t{
            if(!p||(p->x==-32768&&p->y==-32768&&p->level==1))return -1;
            const auto tile=[](std::int32_t v){return v>=0?v/16:-static_cast<std::int32_t>((-static_cast<std::int64_t>(v)+15)/16);};
            return tile(p->x)+static_cast<std::int64_t>(tile(p->y))*bindings_.architecture.width+
                (static_cast<std::int64_t>(p->level)-1)*bindings_.architecture.width*bindings_.architecture.height;};
        const auto wanted=offset(point);std::vector<std::int16_t> ids;
        std::int16_t cursor=0;while(const auto next=bindings_.world.next_entity(cursor)){cursor=*next;
            if(offset(bindings_.world.find(cursor)->position)==wanted)ids.push_back(cursor);}
        return ids;
    }
    Ts1PlacementServices make_services(){
        Ts1PlacementServices services;services.slots=bindings_.slots;
        services.place=[this](const auto& invocation,auto target,auto destination,auto flags){
            if(invocation.instruction.opcode!=16||target.id!=bindings_.observe_find_target||!bindings_.observe_find_attempt)
                return place(invocation,target,destination,flags);
            const auto* entity=bindings_.world.find(target.id);
            const auto before=entity?entity->position:std::optional<EntityPosition>{};
            auto result=place(invocation,target,destination,flags);
            bindings_.observe_find_attempt(invocation,target,before,destination,flags,result);
            return result;
        };
        services.transfer=[this](const auto& invocation,auto container,auto slot,auto item){return transfer(invocation,container,slot,item);};
        services.locations=[this](const auto& invocation,const auto& slot,auto target,auto caller){return locations(invocation,slot,target,caller);};
        services.tile_occupied=[this](const auto& invocation,const auto& point)->std::optional<bool>{if(!invocation.source_still_matches())return {};return !objects_at(point).empty();};
        services.lot_size=[this](const auto&)->std::optional<std::pair<std::int16_t,std::int16_t>>{
            if(!bindings_.architecture.width||!bindings_.architecture.height||bindings_.architecture.width>32767||bindings_.architecture.height>32767)return {};
            return std::pair<std::int16_t,std::int16_t>{static_cast<std::int16_t>(bindings_.architecture.width),static_cast<std::int16_t>(bindings_.architecture.height)};};
        services.random=bindings_.random;services.shoo=bindings_.shoo;services.moved_self=bindings_.moved_self;return services;
    }
    HouseholdPlacementBindings bindings_;Ts1Placement controller_;
    std::function<std::optional<float>(std::int16_t,std::size_t)> reach_slot_height_;
};
}
