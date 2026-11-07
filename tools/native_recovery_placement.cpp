// SPDX-License-Identifier: MPL-2.0
#include "native_recovery_placement.hpp"
#include "native_hierarchy_snapshot.hpp"
#include <limits>

namespace odh_host {
namespace {
using Ref=odh::WorldEntityReference;
odh::ObjectPositionIdentity id(Ref r){return {r.id,r.generation};}
bool fail(std::string& why,const char* message){why=message;return false;}
struct Call {bool active=true,entered=false,repeated=false,accepted=false;std::function<bool()> operation;};
struct End {std::shared_ptr<Call> call;~End(){call->active=false;call->operation={};}};
}
NativeRecoveryPlacement::NativeRecoveryPlacement(SessionObjectPositionContext context,
        SessionObjectPositionServices services,std::shared_ptr<NativeFloorHeads> floors):
    context_(std::move(context)),services_(std::move(services)),floors_(std::move(floors)){}
bool NativeRecoveryPlacement::current(Ref ref)const{
    return context_.source_still_matches&&context_.source_still_matches()&&context_.world&&
        context_.world->is_live_reference(ref)&&floors_&&floors_->ready();
}
bool NativeRecoveryPlacement::phase(Ref ref,std::optional<odh::EntityPosition> destination,
        SessionPositionPhase kind,std::optional<std::uint8_t> direction,const std::function<bool()>& operation){
    if(!current(ref)||!context_.position_phase)return false;
    auto call=std::make_shared<Call>();call->operation=operation;End end{call};
    const bool result=context_.position_phase(id(ref),destination,kind,direction,[this,ref,call]{
        if(!call->active)return false;if(call->entered){call->repeated=true;return false;}
        call->entered=true;return call->accepted=current(ref)&&call->operation();
    });
    return result&&call->entered&&call->accepted&&!call->repeated&&current(ref);
}
bool NativeRecoveryPlacement::physical(Ref root,Ref child,std::optional<Ref> parent,std::size_t slot,
        const std::function<bool()>& operation){
    if(!current(root)||!current(child))return false;
    const auto* e=context_.world->resolve_reference(child);if(!e)return false;
    const bool detach=e->attachment.has_value();const auto old_pose=e->position;
    const auto old_room=old_pose?services_.change.room_at(*old_pose):std::optional<std::uint16_t>{0};
    const bool other=child!=root;
    if(other&&(!old_room||!services_.change.unregister_position(id(child),true)))return false;
    bool result=false;
    if(detach)result=phase(child,{},SessionPositionPhase::pre,{},operation);
    else if(parent&&context_.slot_phase){
        auto call=std::make_shared<Call>();call->operation=operation;End end{call};
        result=context_.slot_phase(id(child),id(*parent),slot,SessionPositionPhase::publish,[this,child,call]{
            if(!call->active)return false;if(call->entered){call->repeated=true;return false;}
            call->entered=true;return call->accepted=current(child)&&call->operation();
        })&&call->entered&&call->accepted&&!call->repeated&&current(child);
    }
    return result&&(!other||services_.change.register_position(id(child),*old_room,true))&&current(root);
}
bool NativeRecoveryPlacement::level(Ref ref,std::int16_t level,std::string& why){
    if(!current(ref))return false;
    const auto* e=context_.world->resolve_reference(ref);if(!e)return false;
    if(!e->position)return context_.world->set_native_out_of_world_level(ref,level);
    auto destination=*e->position;destination.level=level;
    const auto room=services_.change.room_at(destination);
    if(!room||!services_.change.unregister_position(id(ref),true))return fail(why,"native level transfer has no actual room owner");
    return phase(ref,destination,SessionPositionPhase::publish,destination.direction,[&]{
        return context_.world->publish_native_recovery_position(ref,destination,false);
    })&&services_.change.register_position(id(ref),*room,true)&&current(ref);
}
bool NativeRecoveryPlacement::unlink(Ref ref,std::string& why){
    if(!current(ref))return fail(why,"native placement unlink authority unavailable");
    NativeHierarchyUnlinkServices hooks;
    hooks.native_in_world=[&](Ref child)->std::optional<bool>{
        if(!current(child)||!context_.prepared)return {};
        const auto pose=context_.world->native_position(child);if(!pose)return {};
        return native_in_world_xy(*pose,context_.prepared->width,context_.prepared->height);
    };
    hooks.physical_mutation=[&,ref](auto child,auto parent,std::size_t slot,const auto& operation){return physical(ref,child,parent,slot,operation);};
    hooks.publish_level=[&](auto child,std::int16_t next,std::string& error){return level(child,next,error);};
    hooks.unregister_native_floor=[&](auto child,const auto& pose,std::string& error){return floors_->publish(pose,child,{},error);};
    hooks.register_promoted_floor=[&](auto child,const auto& pose,std::string& error){return floors_->publish(pose,{},child,error);};
    return unlink_native_hierarchy_for_out_of_world(*context_.world,ref,context_.world->native_position(ref),context_.source_still_matches,why,hooks);
}
bool NativeRecoveryPlacement::restore(Ref ref,std::optional<odh::EntityPosition> destination,
        std::optional<Ref> parent,std::size_t slot,std::optional<odh::EntityPosition> saved,std::string& why){
    if(!current(ref)||!floors_)return fail(why,"native placement restoration requires its actual original floor owner");
    const auto receiver_scalar=context_.world->native_position(ref);if(!receiver_scalar)return false;
    if(destination)destination->direction=receiver_scalar->direction;
    if(parent){
        if(!current(*parent))return fail(why,"native destination container expired");
        const auto* body=context_.world->resolve_reference(*parent);
        if(!body||!body->position)return fail(why,"native restored container has no represented in-world scalar pose");
        const auto heading=context_.world->native_position(ref);if(!heading)return false;
        destination=body->position;destination->direction=heading->direction;
    }
    if(destination){
        if(slot>32766)return fail(why,"native logical container slot overflows source word");
        NativeHierarchyRestoreServices hooks;
        hooks.floor_head=[&](const auto& point){return floors_->head(point);};
        hooks.publish_floor_head=[&](const auto& point,auto old,auto next,std::string& error){return floors_->publish(point,old,next,error);};
        hooks.physical_mutation=[&,ref](auto child,auto container,std::size_t index,const auto& operation){return physical(ref,child,container,index,operation);};
        hooks.publish_level=[&](auto child,std::int16_t next,std::string& error){return level(child,next,error);};
        if(!restore_native_hierarchy_in_world(*context_.world,ref,{*destination,parent,static_cast<std::int32_t>(slot)},context_.source_still_matches,why,hooks))return false;
    }else if(!saved||saved->x!=-16||saved->y!=-16)return fail(why,"native OOW destination scalar is unknown");
    const auto scalar=context_.world->native_position(ref);if(!scalar)return false;
    const auto heading=destination?destination->direction:scalar->direction;
    if(!phase(ref,destination,SessionPositionPhase::publish,heading,[&]{return context_.world->publish_native_recovery_position(ref,destination,false);}))
        return fail(why,"native restored XYZ scalar publication failed");
    if(!destination&&!context_.world->set_native_out_of_world_level(ref,saved->level))return false;
    return current(ref);
}
}
