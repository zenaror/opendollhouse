// SPDX-License-Identifier: MPL-2.0
// Native contract: reports/ghidra/objt-version-audit/{0043206d,00432664,
// 004331d4,004333ef,004231a8,00422c62,00463648}.c.txt. Placement lifecycle and
// RoomInfo consumers are the existing pinned FreeSO native ports, not a new World.
#include "saved_recovery_object.hpp"
#include "native_hierarchy_snapshot.hpp"
#include "odh/object_position_change.hpp"
#include "odh/room_obstacles.hpp"
#include <algorithm>
#include <limits>

namespace odh_host {
namespace {
using namespace odh;
using Phase=SavedRecoveryPhase;using Ref=WorldEntityReference;
ObjectPositionIdentity id(Ref r){return {r.id,r.generation};}
Ref ref(ObjectPositionIdentity r){return {r.id,r.generation};}
std::optional<std::int16_t> word(const Entity& e,std::int16_t index){const auto i=e.object_words.find(index);return i==e.object_words.end()?std::nullopt:std::optional<std::int16_t>{i->second};}
bool fail(std::string& error,const std::string& text){error=text;return false;}
bool owned_phase(Phase p){switch(p){case Phase::capture_placement:case Phase::remove_entry:case Phase::detach:
    case Phase::before_clear:case Phase::restore_group:case Phase::restore_placement:case Phase::placement_entry:
    case Phase::cleanup_flags:case Phase::portal_detach:case Phase::portal_restore:case Phase::portal_reconcile:return true;
    default:return false;}}
}
std::optional<SavedRecoveryNativeOffset> saved_recovery_offset_from_native_tiles(
    const odh::NativeObjectGroupOffset& tiles,std::string& error){
    // FUN422055: supplied-objdump.txt 42206c/42207b SHL4. Keep host
    // arithmetic wide and reject unrepresentable metadata, never signed-UB
    // shifts or truncation into the distinct bounded absolute pose profile.
    const auto x=std::int64_t(tiles.x)*16,y=std::int64_t(tiles.y)*16;
    if(x<std::numeric_limits<std::int32_t>::min()||x>std::numeric_limits<std::int32_t>::max()||
       y<std::numeric_limits<std::int32_t>::min()||y>std::numeric_limits<std::int32_t>::max()){
        error="native tile offset cannot be represented in World XY units";return {};
    }
    error.clear();return SavedRecoveryNativeOffset{std::int32_t(x),std::int32_t(y),tiles.level};
}
SavedRecoveryObjectAdapter::SavedRecoveryObjectAdapter(SavedRecoveryObjectContext context):context_(std::move(context)){
    if(!context_.definitions && context_.position.resources)context_.definitions=&context_.position.resources->definitions();
    if(context_.wall_use)context_.services.change.wall_use=context_.wall_use;
    if(context_.architectural_wall_style)context_.services.change.architectural_wall_style=context_.architectural_wall_style;
    // These are direct native lifecycle operations. Movement's singleton-only
    // admission is not used as evidence for a source-authenticated loaded piece.
    context_.services.change.admit_single_ordinary=[this](auto i){return admit(i);};
    context_.services.change.admit_loaded_entity=[this](auto i){return admit(i);};
    // Recursive loaded-position children use the same native NULL-argument
    // entry semantics as the explicit EP9 pass owned by this adapter.
    context_.services.change.entry_point9=[this](auto i){std::string error;return entry(ref(i),9,error);};
}
bool SavedRecoveryObjectAdapter::handles(odh::SavedRecoveryPhase phase){return owned_phase(phase);}
bool SavedRecoveryObjectAdapter::current()const{return context_.position.world && context_.position.source_still_matches && context_.position.source_still_matches();}
const odh::Entity* SavedRecoveryObjectAdapter::entity(Ref r)const{
    if(!current())return nullptr;const auto* e=context_.position.world->find(r.id);
    return e&&e->generation==r.generation&&!e->ui_ghost?e:nullptr;
}
std::optional<bool> SavedRecoveryObjectAdapter::admit(odh::ObjectPositionIdentity i)const{
    const auto* e=entity(ref(i));if(!e||!e->definition||!e->entry_points||e->entry_points->size()<=15||!context_.definitions)return {};
    const auto* d=context_.definitions->unique(e->definition->guid);if(!d)return {};
    return !(d->definition.master_id && d->definition.sub_index==-1) &&
        (e->kind==odh::EntityKind::object || e->kind==odh::EntityKind::avatar);
}
std::optional<std::vector<odh::ObjectPositionIdentity>> SavedRecoveryObjectAdapter::group(Ref r)const{
    if(!current())return {};
    if(context_.position.registration_group)return context_.position.registration_group(id(r));
    if(context_.position.group){const auto g=context_.position.group(id(r));if(!g)return {};
        std::vector<odh::ObjectPositionIdentity> result;for(const auto& m:g->members)result.push_back({m.id,m.generation});return result;}
    const auto profile=profiles_.find(r);
    if(profile!=profiles_.end()&&!profile->second.native_group)return std::vector<odh::ObjectPositionIdentity>{id(r)};
    return {};
}
bool SavedRecoveryObjectAdapter::preflight(const std::vector<odh::SavedRecoveryEntity>& cohort,std::string& error){
    const auto& p=context_.position;const auto& s=context_.services.change;
    if(bool(context_.native_unlink)!=bool(context_.native_restore_position))
        return fail(error,"native placement unlink/insertion ownership must be paired");
    if(context_.native_restore_position&&!context_.native_position_neighbors)
        return fail(error,"native restored position requires its actual seating notifications");
    if(!current()||!context_.definitions||!p.prepared||!p.architecture||!p.rooms||!p.object_support||
       !p.position_registry||!p.position_registry->complete||!s.unregister_position||!s.register_position||
       !s.publish_position||!s.room_at||!s.wall_use||!s.object_supported||!s.dynamic_multitile||!context_.native_entry||!context_.native_detach)
        return fail(error,"saved recovery requires actual complete placement/registry/support/code services");
    std::map<Ref,odh::SavedRecoveryEntity> candidate;
    for(const auto& profile:cohort){
        if(profile.kind!=odh::SavedRecoveryClass::object&&profile.kind!=odh::SavedRecoveryClass::portal&&
           profile.kind!=odh::SavedRecoveryClass::special_cursor)continue;
        const auto* e=entity(profile.reference);const auto yes=admit(id(profile.reference));
        if(!e||!yes||!*yes||e->kind!=odh::EntityKind::object)return fail(error,"saved object resource/class admission unavailable");
        if(profile.kind==odh::SavedRecoveryClass::special_cursor){
            const auto* definition=context_.definitions->unique(e->definition->guid);
            if(e->definition->guid!=0x3c08965fU||!definition||definition->definition.object_type!=4||
               definition->definition.master_id!=0||definition->definition.sub_index!=0||profile.native_group)
                return fail(error,"always-reset definition lacks the proven ordinary native class profile");
        }
        for(const auto index:{1,8,13,14,26,29,34,40,51,59,63})if(!word(*e,index))return fail(error,"saved object native bank is incomplete");
        if((std::uint16_t(*word(*e,40))&0xc000U)&&!s.architectural_wall_style)
            return fail(error,"architectural recovery requires actual wall-style publication");
        if(context_.native_restore_position&&(std::uint16_t(*word(*e,40))&0x400U)&&!context_.native_position_finished)
            return fail(error,"native restored F2 needs its actual presentation event owner");
        if((*word(*e,63)&2)&&(*word(*e,13)%15)&&!context_.wall_use)
            return fail(error,"exclusive occupied-wall recovery needs its actual architecture owner");
        if(profile.has_cleanup&&!context_.clear_use_count)return fail(error,"Cleanup requires actual UseCount ownership publication");
        if(profile.native_group&&(!context_.native_offset||!context_.restore_group))
            return fail(error,"native group recovery requires actual current offset and restoration owners");
        if(profile.native_group&&!context_.native_offset(profile.reference))return fail(error,"native world-oriented piece offset unavailable");
        const auto hierarchy=capture_native_hierarchy_snapshot(*p.world,profile.reference,p.source_still_matches,error);
        if(!hierarchy)return false;
        const auto linked=p.world->native_slot0(profile.reference);
        if((hierarchy->chain.size()>1||(linked&&linked->id))&&(!context_.native_unlink||!context_.native_restore_position))
            return fail(error,"native zero-chain recovery requires actual unlink and insertion owners");
        if(hierarchy->parent&&(!s.restore_container||hierarchy->slot<0))
            return fail(error,"native logical container restoration owner unavailable");
        candidate.emplace(profile.reference,profile);
    }
    profiles_=std::move(candidate);return true;
}
bool SavedRecoveryObjectAdapter::supports(const odh::SavedRecoveryCall& call,std::string& error)const{
    if(!owned_phase(call.phase))return fail(error,"phase belongs to another recovery owner");
    if(!profiles_.count(call.entity)&&call.phase!=Phase::portal_reconcile)return fail(error,"object phase lacks preflighted source identity");
    if((call.phase==Phase::remove_entry||call.phase==Phase::detach||call.phase==Phase::restore_placement||call.phase==Phase::placement_entry)&&
       !context_.native_entry)return fail(error,"native receiver-owned entry executor absent");
    if(call.phase==Phase::detach&&!context_.native_detach)return fail(error,"native physical RemoveFromWorld owner absent");
    if(call.phase==Phase::cleanup_flags&&!context_.clear_use_count)return fail(error,"UseCount owner absent");
    if(call.phase==Phase::restore_group&&!context_.restore_group)return fail(error,"group restoration owner absent");
    if((call.phase==Phase::portal_detach||call.phase==Phase::portal_restore||call.phase==Phase::portal_reconcile)&&
       !context_.position.registration_group&&!context_.position.group)return fail(error,"portal topology needs current authenticated group membership");
    return true;
}
bool SavedRecoveryObjectAdapter::entry(Ref r,std::uint16_t ep,std::string& error){
    if(!entity(r)||!context_.native_entry)return fail(error,"native entry identity or actual executor unavailable");
    if(!context_.native_entry(r,ep,error))return !error.empty()?false:fail(error,"native recovery entry did not complete synchronously");
    return entity(r)!=nullptr||fail(error,"native recovery entry receiver or source authority expired after execution");
}
bool SavedRecoveryObjectAdapter::pre(Ref r,std::string& error){
    odh::ObjectPositionValid valid(*context_.position.world,context_.services.placement);
    auto services=context_.services.change;bool unlinked=false;
    if(context_.native_unlink)services.detach_container=[&](auto child,auto,std::size_t){
        if(child.id!=r.id||child.generation!=r.generation||unlinked)return false;
        unlinked=context_.native_unlink(r,error);return unlinked;
    };
    odh::ObjectPositionChange lifecycle(*context_.position.world,valid,services);
    const auto operation=[&]{return lifecycle.pre_position_change(id(r),true,context_.position.source_still_matches);};
    const auto& guard=context_.services.change.lifecycle_phase;
    const bool ok=guard?guard(id(r),odh::ObjectPositionChangePhase::pre,operation):operation();
    if(!ok)return !error.empty()?false:fail(error,"recovery PrePositionChange: "+lifecycle.diagnostic());
    return !context_.native_unlink||unlinked||context_.native_unlink(r,error);
}
bool SavedRecoveryObjectAdapter::post(Ref r,std::string& error){
    const auto* e=entity(r);if(!e)return fail(error,"post-position original identity unavailable");
    odh::ObjectPositionValid valid(*context_.position.world,context_.services.placement);
    auto services=context_.services.change;
    //3333ef publishes the new floor/room membership before seating notices,
    // then435051 performs wall style/use (including real EP6). Keep that
    // boundary inside Post: running notices after Post reverses script effects.
    const auto placed=context_.position.world->native_position(r);
    if(context_.native_position_neighbors){
        if(!placed)return fail(error,"native new-center scalar unavailable before registration");
        const auto registration=services.register_position;
        services.register_position=[&,registration](auto owner,auto room,bool changed){
            if(!registration||!registration(owner,room,changed)||!entity(ref(owner)))return false;
            return owner.id!=r.id||owner.generation!=r.generation||
                (context_.native_position_neighbors(r,*placed,true,error)&&entity(r));
        };
    }
    odh::ObjectPositionChange lifecycle(*context_.position.world,valid,services);
    // Source load-style post deliberately separates the final native EP9 pass.
    // A contained root's ordinary post returns before its own EP9 by contract.
    const bool contained=e->attachment.has_value();
    const auto operation=[&]{return contained?lifecycle.position_change(id(r),true,context_.position.source_still_matches):
        lifecycle.loaded_position_change(id(r),context_.position.source_still_matches);};
    const auto& guard=context_.services.change.lifecycle_phase;
    const bool ok=guard?guard(id(r),odh::ObjectPositionChangePhase::post,operation):operation();
    return ok||fail(error,"recovery PositionChange: "+lifecycle.diagnostic());
}
bool SavedRecoveryObjectAdapter::publish(Ref r,const std::optional<odh::EntityPosition>& pose,std::uint8_t direction,std::string& error){
    const bool ok=context_.services.change.publish_position&&context_.services.change.publish_position(id(r),pose,direction)&&entity(r);
    return ok||fail(error,"recovery scalar position publication failed");
}
bool SavedRecoveryObjectAdapter::regenerate(std::uint16_t room,std::int16_t level,std::string& error){
    auto& registry=*context_.position.position_registry;const auto& rooms=*context_.position.rooms;
    if(room>=registry.rooms.size()||level<1||level>2)return fail(error,"portal boundary room/level unavailable");
    std::vector<odh::RoomPortalOpening> openings;const auto cells=std::int32_t(registry.width*registry.height);
    for(const auto& tile:registry.tiles){if(tile.offset<0||!cells||tile.offset/cells!=level-1)continue;
        std::optional<Ref> door;
        for(const auto owner:tile.entities){const auto* e=entity(ref(owner));if(!e)return fail(error,"portal tile index has unavailable exact owner");
            const auto flags=word(*e,40);if(!flags)return false;if(std::uint16_t(*flags)&0x8000U){door=ref(owner);break;}}
        if(!door)continue;const auto members=group(*door);if(!members)return fail(error,"portal boundary current group unavailable");
        for(const auto part:*members){const auto* e=entity(ref(part));if(!e||!e->entry_points||e->entry_points->size()<=15)return false;
            const auto actual=e->position?context_.services.change.room_at(*e->position):std::optional<std::uint16_t>{0};if(!actual)return false;
            if(*actual==room&&e->entry_points->at(15).action){const auto at=tile.offset%cells;
                openings.push_back({room,std::uint32_t(at%std::int32_t(registry.width)),std::uint32_t(at/std::int32_t(registry.width)),std::uint8_t(level-1)});break;}}
    }
    const auto generated=odh::generate_room_obstacle_candidates(rooms,room,std::uint8_t(level-1),openings,true);
    if(!generated.complete)return fail(error,"portal source room obstacle generation incomplete");
    registry.rooms[room].boundaries[std::size_t(level-1)]=generated.rectangles;
    registry.rooms[room].boundaries_known[std::size_t(level-1)]=true;return true;
}
bool SavedRecoveryObjectAdapter::portal(Ref target,Phase phase,std::string& error){
    if(!current()||!context_.position.position_registry)return fail(error,"portal registry authority unavailable");
    auto& state=portals_[target];auto& registry=*context_.position.position_registry;
    if(phase==Phase::portal_detach){
        if(state.detached)return fail(error,"portal recovery already detached");
        state={};const auto members=group(target);if(!members)return fail(error,"portal source group unavailable");
        for(const auto member:*members){const auto* e=entity(ref(member));if(!e)return false;if(state.members.insert(ref(member)).second)state.order.push_back(ref(member));
            if(e->position){const auto room=context_.services.change.room_at(*e->position);if(!room)return false;state.rooms.emplace(*room,e->position->level);}}
        state.detached=true;
    }else if(!state.detached)return true; // Actual no-detach EP3 path has no pending portal operation.
    std::set<Ref> current_members=state.members,ordered_seen;std::vector<Ref> current_order;
    if(phase!=Phase::portal_detach)for(const auto original:state.order){const auto* e=entity(original);if(!e||e->dead)continue;
        const auto members=group(original);if(!members)return fail(error,"surviving portal group unavailable");
        for(const auto member:*members){current_members.insert(ref(member));
            if(ordered_seen.insert(ref(member)).second)current_order.push_back(ref(member));}}
    for(auto& room:registry.rooms){
        const auto remove=[&](auto& links){links.erase(std::remove_if(links.begin(),links.end(),[&](const auto& link){return current_members.count(ref(link.owner))!=0;}),links.end());};
        remove(room.portals);remove(room.window_portals);
    }
    if(phase!=Phase::portal_detach)for(const auto member:current_order){const auto* e=entity(member);
        if(!e||e->dead||e->attachment||!e->position)continue;
        const auto room=context_.services.change.room_at(*e->position);if(!room||*room>=registry.rooms.size()||!e->entry_points||e->entry_points->size()<=15)return false;
        state.rooms.emplace(*room,e->position->level);
        const auto flags=word(*e,40);if(!flags)return fail(error,"portal survivor flags unavailable");
        const bool portal=e->entry_points->at(15).action!=0;const bool window=(std::uint16_t(*flags)&0x4000U)!=0;if(!portal&&!window)continue;
        const auto peers=group(member);if(!peers)return false;
        for(const auto peer:*peers){if(ref(peer)==member)continue;const auto* other=entity(ref(peer));if(!other||other->dead||!other->position)continue;
            const auto target_room=context_.services.change.room_at(*other->position);if(!target_room)return false;
            if(*target_room==*room||(portal&&(!other->entry_points||other->entry_points->size()<=15||!other->entry_points->at(15).action)))continue;
            (portal?registry.rooms[*room].portals:registry.rooms[*room].window_portals).push_back({id(member),*target_room});break;}
    }
    for(const auto affected:state.rooms)if(!regenerate(affected.first,affected.second,error)){registry.complete=false;return false;}
    odh::validate_object_position_registry_snapshot(registry);
    // Final incompatibility Init follows portal_restore and can mutate groups
    // or delete pieces. Keep the actual pending snapshot until finally runs.
    if(phase==Phase::portal_reconcile)state.detached=false;
    return current();
}
bool SavedRecoveryObjectAdapter::execute(const odh::SavedRecoveryCall& call,std::string& error){
    if(!supports(call,error))return false;
    const auto r=call.entity,target=call.target;auto* world=context_.position.world;
    if(call.phase==Phase::portal_detach||call.phase==Phase::portal_restore||call.phase==Phase::portal_reconcile)
        return portal(target,call.phase,error);
    const auto* e=entity(r);if(!e)return fail(error,"object recovery original identity unavailable");
    const auto heading=word(*e,1);if(!heading)return fail(error,"object recovery direction unavailable");
    switch(call.phase){
    case Phase::capture_placement:{
        const auto hierarchy=capture_native_hierarchy_snapshot(*world,r,context_.position.source_still_matches,error);
        if(!hierarchy)return false;
        Snapshot snapshot;snapshot.position=hierarchy->root_position;snapshot.parent=hierarchy->parent;
        snapshot.native_position=world->native_position(r);
        if(snapshot.native_position&&!native_in_world_xy(*snapshot.native_position,context_.position.prepared->width,context_.position.prepared->height))snapshot.position.reset();
        if(snapshot.parent){if(hierarchy->slot<0)return fail(error,"native logical snapshot slot unavailable");snapshot.slot=static_cast<std::size_t>(hierarchy->slot);}
        if(!native_hierarchy_snapshot_still_matches(*world,*hierarchy,context_.position.source_still_matches,error))return false;
        if(hierarchy->normalize_zero_owner&&!world->declare_object_word(hierarchy->normalize_zero_owner->id,14,-1))return false;
        snapshots_[target]=snapshot;return true;}
    case Phase::remove_entry:return entry(r,10,error);
    case Phase::detach:
        if(!context_.native_detach(r,error))return !error.empty()?false:fail(error,"native physical RemoveFromWorld incomplete");
        return entity(r)!=nullptr||fail(error,"native physical RemoveFromWorld receiver or source authority expired");
    case Phase::before_clear:{odh::VmFrame frame;frame.context={r.id,r.id,0,0};if(!world->bind_context_live(frame.context))return false;
        for(const auto index:{59,8,34,25})if(!world->write(frame,3,index,0))return fail(error,"native before-clear index/flag publication failed");return true;}
    case Phase::cleanup_flags:{const auto flags=word(*e,8);if(!flags)return false;odh::VmFrame frame;frame.context={r.id,r.id,0,0};
        const bool ok=world->bind_context_live(frame.context)&&world->write(frame,3,8,std::int16_t(std::uint16_t(*flags)&~0x20U))&&context_.clear_use_count(r,error);
        return ok||(!error.empty()?false:fail(error,"Cleanup occupied/use-count publication failed"));}
    case Phase::restore_group:return context_.restore_group(r,target,error);
    case Phase::restore_placement:{
        const auto saved=snapshots_.find(target);if(saved==snapshots_.end())return fail(error,"placement snapshot missing");
        const auto& snapshot=saved->second;
        //3206d invokes virtual+58 only when the OLD432664 snapshot was in-world.
        // Init may have placed an originally OOW body; preserve that effect.
        if(!snapshot.position)return true;
        // 4333ef unregisters the CURRENT pose before scalar restoration, even
        // when it is OUT_OF_WORLD after reset/Init. Keep Room0/query ownership
        // out of the new registration rather than silently duplicating it.
        const auto previous_native=world->native_position(r);
        if(!pre(r,error))return false;
        if(context_.native_position_neighbors){
            if(!previous_native||!context_.native_position_neighbors(r,*previous_native,false,error)||!entity(r))return false;
        }
        auto pose=snapshot.position;const auto profile=profiles_.find(target);
        if(!snapshot.parent&&pose&&profile!=profiles_.end()&&profile->second.native_group){const auto origin=context_.native_offset(target),piece=context_.native_offset(r);
                if(!origin||!piece)return fail(error,"live native placement offsets unavailable");
                const auto x=std::int64_t(pose->x)-origin->x+piece->x,y=std::int64_t(pose->y)-origin->y+piece->y,level=std::int64_t(pose->level)-origin->level+piece->level;
                if(x<0||y<0||x>32767||y>32767||level<1||level>32767)return fail(error,"native group placement overflows World coordinate profile");
                pose->x=std::int32_t(x);pose->y=std::int32_t(y);pose->level=std::int16_t(level);}
        const auto direction=std::uint8_t((*heading%8+8)%8);if(pose)pose->direction=direction;
        if(context_.native_restore_position){
            if(!context_.native_restore_position(r,pose,snapshot.parent,snapshot.slot,snapshot.native_position,error))return false;
        }else if(snapshot.parent){const auto* parent=entity(*snapshot.parent);if(!parent)return fail(error,"saved container exact identity unavailable");
            if(!context_.services.change.restore_container(id(r),id(*snapshot.parent),snapshot.slot))return fail(error,"native container/slot restoration failed");
            if(!world->declare_object_word(r.id,26,snapshot.parent->id)||!world->declare_object_word(r.id,14,std::int16_t(snapshot.slot+1)))return false;
        }else {
            if(!publish(r,pose,direction,error))return false;
        }
        if(!post(r,error))return false;
        const auto* placed=entity(r);if(!placed||!placed->definition)return false;
        const auto* definition=context_.definitions->unique(placed->definition->guid);if(!definition)return false;
        if(definition->definition.master_id==0&&!entry(r,9,error))return false;
        if(snapshot.parent&&!entry(r,9,error))return false;
        placed=entity(r);const auto flags=placed?word(*placed,40):std::nullopt;if(!flags)return false;
        if(std::uint16_t(*flags)&0x400U){
            if(!context_.native_position_finished)return fail(error,"native F2 presentation owner unavailable after placement entries");
            if(!context_.native_position_finished(r,error))return false;
            return entity(r)!=nullptr||fail(error,"native F2 presentation receiver or source expired after event");
        }
        return true;}
    case Phase::placement_entry:{const auto saved=snapshots_.find(target);
        if(saved!=snapshots_.end()&&!saved->second.position)return true;
        const auto profile=profiles_.find(target);
        return profile!=profiles_.end()&&(!profile->second.native_group||entry(r,9,error));}
    default:return fail(error,"object adapter phase not implemented");}
}
} // namespace odh_host
