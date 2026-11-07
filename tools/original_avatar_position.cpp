// SPDX-License-Identifier: MPL-2.0
#include "original_avatar_position.hpp"
#include "native_hierarchy_snapshot.hpp"
#include <exception>
#include <set>

namespace odh_host {
OriginalEntityRemovalAttempt remove_original_entity_from_world(
        odh::WorldEntityReference reference,SessionObjectPositionContext context,
        const OriginalEntityRemovalServices& hooks){
    OriginalAvatarRemovalAttempt out;bool finished=false;
    std::function<bool()> current;
    struct Finish {OriginalAvatarRemovalAttempt& out;bool& finished;odh::ObjectPositionRegistrySnapshot* registry;
        ~Finish(){if(registry&&out.effects_started&&!finished)registry->complete=false;}
    }finish{out,finished,context.position_registry};
    const auto resample_pose=[&]() noexcept {
        if(!out.native_pose)return;
        // fail() is also used from catch. A callback can expire the borrowed
        // World/Thread owners before returning or throwing; authority callbacks
        // must run before even inspecting those owners. Preserve the last
        // authenticated effect when a fresh read is no longer authorized.
        try{
            if(!current||!current())return;
            const auto* e=context.world->resolve_reference(reference);if(!e)return;
            const auto heading=e->object_words.find(1);
            if(heading==e->object_words.end()||heading->second<0||heading->second>7)return;
            if(e->position)out.native_pose=e->position;
            else out.native_pose->direction=static_cast<std::uint8_t>(heading->second);
        }catch(...){/* Preserve the last authenticated scalar result. */}
    };
    const auto fail=[&](const std::string& why){resample_pose();out.diagnostic=why;return out;};
    try{
        using namespace odh;
        if(!context.source_still_matches||!context.source_still_matches())return fail("native entity removal source authority unavailable");
        if(!context.world||!context.resources||!context.prepared||!context.architecture||!context.rooms||!context.position_registry||
           !context.position_registry->complete||!context.position_phase||!context.source_still_matches||
           !context.thread_for||!hooks.conditions||!hooks.admit||!hooks.entry||!hooks.move_bookkeeping||!hooks.world_removed)
            return fail("native avatar removal owners incomplete");
        if(context.prepared->width!=context.rooms->width||context.prepared->height!=context.rooms->height||
           context.prepared->width!=context.architecture->width||context.prepared->height!=context.architecture->height)
            return fail("native avatar removal geometry owners disagree");
        auto* world=context.world;
        if(!context.source_still_matches())return fail("native entity removal source expired before Thread binding");
        auto* thread=context.thread_for(reference.id);
        if(!context.source_still_matches()||!thread)return fail("native entity removal actual Thread unavailable");
        const auto run=thread->run_generation();
        // A real object Thread without Main can still be at generation zero.
        // Capture that actual state; never manufacture a start just for proof.
        if(thread->dispatch_active())return fail("native entity removal requires an idle actual Thread/run");
        current=[authority=context.source_still_matches,thread_for=context.thread_for,reference,world,thread,run]{
            if(!authority||!authority()||!thread_for||thread_for(reference.id)!=thread||!authority())return false;
            if(thread->dispatch_active()||thread->run_generation()!=run||!world->is_live_reference(reference))return false;
            const auto* e=world->resolve_reference(reference);
            return e&&!e->dead&&!e->ui_ghost&&(e->kind==EntityKind::object||e->kind==EntityKind::avatar);
        };
        const ObjectPositionIdentity root{reference.id,reference.generation};
        const auto body=[&](ObjectPositionIdentity id)->const Entity*{
            if(!current())return nullptr;const auto* e=world->find(id.id);
            return e&&e->generation==id.generation&&!e->dead&&!e->ui_ghost?e:nullptr;
        };
        const auto word=[](const Entity& e,std::int16_t index)->std::optional<std::int16_t>{
            const auto p=e.object_words.find(index);return p==e.object_words.end()?std::nullopt:std::optional<std::int16_t>{p->second};
        };
        // Native432664 resolves logical containment through the dedicated
        // raw-slot-zero chain. These links are not physical World attachments.
        const auto parent=[&](ObjectPositionIdentity id,std::optional<WorldEntityReference>& result){
            std::string why;
            const auto snapshot=capture_native_hierarchy_snapshot(*world,{id.id,id.generation},current,why);
            if(!snapshot)return false;result=snapshot->parent;return true;
        };
        const auto tree=[&](ObjectPositionIdentity start,std::vector<ObjectPositionIdentity>& result){
            result.clear();std::set<std::int16_t> seen;
            std::function<bool(ObjectPositionIdentity)> visit=[&](ObjectPositionIdentity id){
                const auto* e=body(id);if(!e||result.size()>=4096||!seen.insert(id.id).second||!word(*e,1)||!word(*e,8)||!word(*e,29))return false;
                result.push_back(id);
                for(std::size_t slot=0;slot<e->slots.size();++slot)if(e->slots[slot]){
                    const auto* child=world->find(e->slots[slot]);if(!child||!child->attachment||child->attachment->container!=id.id||child->attachment->slot!=slot)return false;
                    if(!visit({child->id,child->generation}))return false;
                }return true;
            };return visit(start);
        };
        std::string error;
        const auto supported=[&](const OriginalAvatarRemovalConditions& p){
            return (!p.presentation_before||bool(hooks.presentation_before))&&(!p.tile_use||bool(hooks.tile_use))&&
                (!p.neighbor_updates||bool(hooks.neighbor_updates))&&(!p.type6||bool(hooks.type6))&&(!p.visual_controller||bool(hooks.visual_controller))&&
                (!p.flagfield_event||bool(hooks.flagfield_event));
        };
        const auto sample=[&]{auto p=hooks.conditions(root);return current()?p:std::nullopt;};
        auto conditions=hooks.conditions(root);
        if(!current()||!conditions||!supported(*conditions)||!hooks.admit(root,error)||!current())
            return fail("native avatar removal conditional owners unavailable: "+error);
        auto* avatar=body(root);
        if(!avatar||(avatar->kind!=EntityKind::avatar&&avatar->kind!=EntityKind::object)||!avatar->definition)return fail("native entity removal body unavailable");
        const auto* definition=context.resources->definitions().unique(avatar->definition->guid);
        if(!definition)return fail("native avatar removal exact OBJD unavailable");
        std::vector<ObjectPositionIdentity> removed,moving;
        if(!tree(root,removed))return fail("native avatar removal contained cohort unavailable");
        std::optional<WorldEntityReference> old_parent;
        if(!parent(root,old_parent))return fail("native avatar removal hierarchy unavailable");
        // Native4331ed tests OBJD.MasterID, not runtime ObjectData10.
        if(definition->definition.master_id==0){
            out.effects_started=true;
            if(!hooks.entry(root,10,error)||!current())return fail("native avatar removal EP10 incomplete: "+error);
        }
        avatar=body(root);if(!avatar)return fail("native avatar removal person expired after EP10");
        auto pose=avatar->position?avatar->position:hooks.native_pose?hooks.native_pose():std::nullopt;
        const auto heading=word(*avatar,1);
        if(!current()||!pose||!heading||*heading<0||*heading>7||pose->direction!=*heading)
            return fail("native avatar removal actual scalar pose unavailable");
        //4341a6 follows actual native parent links to the top owner, then marks
        // its complete existing child tree before4331d4's own removal snapshot.
        ObjectPositionIdentity top=root;std::set<std::int16_t> parents;
        for(;;){
            if(!parents.insert(top.id).second)return fail("native avatar removal parent cycle");
            const auto snapshot=capture_native_hierarchy_snapshot(*world,{top.id,top.generation},current,error);
            if(!snapshot)return fail("native avatar removal top-owner hierarchy unavailable: "+error);
            if(snapshot->normalize_zero_owner){out.effects_started=true;if(!world->declare_object_word(snapshot->normalize_zero_owner->id,14,-1))return fail("native hierarchy normalization failed");}
            const auto p=snapshot->parent;
            if(!p)break;top={p->id,p->generation};
        }
        if(!tree(top,moving)||!tree(root,removed))return fail("native avatar removal moving cohort unavailable");
        // All required banks are present before the first scalar write. The
        // host admits presentation/lighting support before even EP10 above.
        auto services=make_session_object_position_services(context);
        if(!services.record_moved_self||!services.change.unregister_position)return fail("native avatar removal registry services unavailable");
        out.effects_started=true;
        for(const auto id:moving)if(!services.record_moved_self(id,true)||!current())return fail("native avatar removal MovedSelf publication failed");
        if(!hooks.move_bookkeeping(top,root,error)||!current())return fail("native avatar removal move bookkeeping incomplete: "+error);
        conditions=sample();if(!conditions)return fail("native avatar removal presentation condition unavailable");
        if(conditions->presentation_before&&(!hooks.presentation_before||!hooks.presentation_before(root,*pose,error)||!current()))return fail("native avatar removal presentation incomplete: "+error);
        // Each native control is read at its own source instruction. Earlier
        // callbacks can change a later branch; a frozen post-EP10 plan cannot.
        conditions=sample();avatar=body(root);const auto light=avatar?word(*avatar,16):std::nullopt;
        if(!conditions||!light||conditions->tile_use!=(*light==1))return fail("native avatar removal Light16 condition unavailable");
        if(conditions->tile_use&&(!hooks.tile_use||!hooks.tile_use(root,*pose,error)||!current()))return fail("native avatar removal tile-use removal incomplete: "+error);
        conditions=sample();avatar=body(root);const auto flags2=avatar?word(*avatar,40):std::nullopt;
        if(!conditions||!flags2||conditions->flagfield_event!=((std::uint16_t(*flags2)&0x400U)!=0))return fail("native avatar removal Flags2 condition unavailable");
        if(conditions->flagfield_event&&(!hooks.flagfield_event||!hooks.flagfield_event(root,error)||!current()))return fail("native avatar removal F2 event incomplete: "+error);
        // Capture after the early bookkeeping/EP10 callbacks, like432664 at
        //4332cc. A callback may legitimately have changed the actual container.
        const auto hierarchy=capture_native_hierarchy_snapshot(*world,reference,current,error);
        if(!hierarchy||!tree(root,removed))return fail("native avatar removal old-parent snapshot expired: "+error);
        old_parent=hierarchy->parent;
        if(hierarchy->normalize_zero_owner){out.effects_started=true;if(!world->declare_object_word(hierarchy->normalize_zero_owner->id,14,-1))return fail("native hierarchy normalization failed");}
        avatar=body(root);if(!avatar)return fail("native removal scalar owner expired before unlink");
        pose=avatar->position?avatar->position:hooks.native_pose?hooks.native_pose():std::nullopt;
        if(!current()||!pose)return fail("native removal scalar pose unavailable before unlink");
        out.old_parent=old_parent;
        const auto phase=[&](ObjectPositionIdentity id,SessionPositionPhase kind,std::optional<std::uint8_t> direction,
                const std::function<bool()>& operation,const std::optional<EntityPosition>& destination=std::nullopt){
            struct Call{bool active=true,entered=false,accepted=false,repeated=false;std::function<bool()> operation;};
            auto call=std::make_shared<Call>();call->operation=operation;
            struct End{std::shared_ptr<Call> call;~End(){call->active=false;call->operation={};}}end{call};
            if(!current())return false;
            const bool accepted=context.position_phase(id,destination,kind,direction,[call,current]{
                if(!call->active)return false;if(call->entered){call->repeated=true;return false;}
                call->entered=true;return call->accepted=current()&&call->operation();
            });
            return accepted&&call->entered&&call->accepted&&!call->repeated&&current();
        };
        // Event-owned tile/room/footprint state is removed at its OLD pose.
        for(const auto id:removed)if(!services.change.unregister_position(id,true)||!current())return fail("native avatar removal registry unregister incomplete");
        NativeHierarchyUnlinkServices hierarchy_services;
        hierarchy_services.native_in_world=[&](WorldEntityReference child)->std::optional<bool>{
            if(!current())return {};
            const auto native=world->native_position(child);if(!native)return {};
            return native_in_world_xy(*native,context.prepared->width,context.prepared->height);
        };
        hierarchy_services.unregister_native_floor=hooks.native_floor_unregister;
        hierarchy_services.physical_mutation=[&](WorldEntityReference child,std::optional<WorldEntityReference> container,
                std::size_t slot,const std::function<bool()>& operation){
            if(!current())return false;const auto* e=world->resolve_reference(child);if(!e)return false;
            if(e->attachment)return phase({child.id,child.generation},SessionPositionPhase::pre,{},operation);
            if(!container||!context.slot_phase)return false;
            const auto room=e->position?services.change.room_at(*e->position):std::optional<std::uint16_t>{0};
            if(!room||!services.change.register_position||
               !services.change.unregister_position({child.id,child.generation},true)||!current())return false;
            // The existing slot owner guards the promoted child's real physical
            // attachment. Native slot zero never passes through this callback.
            struct Call{bool active=true,entered=false,accepted=false,repeated=false;std::function<bool()> operation;};
            auto call=std::make_shared<Call>();call->operation=operation;
            struct End{std::shared_ptr<Call> call;~End(){call->active=false;call->operation={};}}end{call};
            const bool result=context.slot_phase({child.id,child.generation},{container->id,container->generation},slot,
                SessionPositionPhase::publish,[call,current]{
                    if(!call->active)return false;if(call->entered){call->repeated=true;return false;}call->entered=true;
                    return call->accepted=current()&&call->operation();
                });
            return result&&call->entered&&call->accepted&&!call->repeated&&current()&&
                services.change.register_position({child.id,child.generation},*room,true)&&current();
        };
        hierarchy_services.publish_level=[&](WorldEntityReference child,std::int16_t level,std::string& why){
            if(!current())return false;const auto* e=world->resolve_reference(child);if(!e||!e->position){why="native level owner has no live scalar pose";return false;}
            auto destination=*e->position;destination.level=level;
            const auto room=services.change.room_at(destination);
            if(!room||!services.change.register_position||!services.change.unregister_position({child.id,child.generation},true)||!current())return false;
            return phase({child.id,child.generation},SessionPositionPhase::publish,destination.direction,
                [&]{return world->publish_native_recovery_position(child,destination,false);},destination)&&
                services.change.register_position({child.id,child.generation},*room,true)&&current();
        };
        hierarchy_services.register_promoted_floor=[&](WorldEntityReference child,const EntityPosition& old,std::string& why){
            if(!current())return false;
            const auto* e=world->resolve_reference(child);
            if(!e||!e->position||e->attachment||e->position->x/16!=old.x/16||e->position->y/16!=old.y/16){
                why="native promoted floor owner differs from source tile";return false;
            }
            // The registry stores every tile occupant, rather than only its
            // chain head. Promotion changes the dedicated native links; the
            // surviving child's existing spatial registration remains valid.
            // A different old child level is transferred by publish_level
            // immediately afterwards, through the actual unregister/register
            // owners. Authenticate the registration at its current level here.
            const auto offset=(std::int32_t(e->position->level)-1)*std::int32_t(context.prepared->width*context.prepared->height)+
                (old.y/16)*std::int32_t(context.prepared->width)+old.x/16;
            for(const auto& tile:context.position_registry->tiles)if(tile.offset==offset)
                for(const auto id:tile.entities)if(id.id==child.id&&id.generation==child.generation)
                    return !hooks.native_floor_promote||hooks.native_floor_promote(child,old,why);
            why="native promoted floor child lacks actual spatial registration";return false;
        };
        if(!unlink_native_hierarchy_for_out_of_world(*world,reference,pose,current,error,hierarchy_services)||!current())
            return fail("native avatar removal actual detach incomplete: "+error);
        for(const auto id:removed){
            const auto* e=body(id);if(!e)return fail("native avatar removal recursive scalar identity expired");
            const auto direction=word(*e,1);if(!direction||*direction<0||*direction>7)return fail("native avatar removal recursive direction unknown");
            if(!phase(id,SessionPositionPhase::publish,static_cast<std::uint8_t>(*direction),[&,id]{
                return world->publish_native_recovery_position({id.id,id.generation},std::nullopt,
                    id.id==root.id&&id.generation==root.generation);
            }))return fail("native avatar removal recursive OOW publication incomplete");
        }
        avatar=body(root);const auto flags=avatar?word(*avatar,8):std::nullopt;
        VmFrame frame;frame.context.caller=root.id;
        if(!flags||!world->write(frame,3,8,static_cast<std::int16_t>(std::uint16_t(*flags)&0xfeffU))||!current())
            return fail("native avatar removal Flags8 publication incomplete");
        out.native_pose=EntityPosition{-16,-16,1,static_cast<std::uint8_t>(*heading)};
        conditions=sample();avatar=body(root);
        if(!conditions||!avatar||!avatar->entry_points||avatar->entry_points->size()<=26||
           conditions->neighbor_updates!=(avatar->entry_points->at(26).action!=0))return fail("native avatar removal SitAction26 condition unavailable");
        if(conditions->neighbor_updates&&(!hooks.neighbor_updates||!hooks.neighbor_updates(root,*pose,error)||!current()))return fail("native avatar removal neighbor updates incomplete: "+error);
        if(old_parent&&(!world->is_live_reference(*old_parent)||!hooks.entry({old_parent->id,old_parent->generation},9,error)||!current()))
            return fail("native avatar removal old-parent EP9 incomplete: "+error);
        resample_pose();
        conditions=sample();if(!conditions)return fail("native avatar removal master BuildModeType condition unavailable");
        if(conditions->type6&&(!hooks.type6||!hooks.type6(root,*pose,error)||!current()))return fail("native avatar removal type6 notification incomplete: "+error);
        conditions=sample();if(!conditions)return fail("native avatar removal visual-controller condition unavailable");
        if(conditions->visual_controller&&(!hooks.visual_controller||!hooks.visual_controller(root,error)||!current()))return fail("native avatar removal visual controller incomplete: "+error);
        if(!hooks.world_removed(root,error)||!current())return fail("native avatar removal World notification incomplete: "+error);
        resample_pose();if(!out.native_pose)return fail("native avatar removal final scalar heading unavailable");
        out.completed=true;finished=true;return out;
    }catch(const std::exception& e){return fail(std::string("native avatar removal exception: ")+e.what());}
    catch(...){return fail("native avatar removal unknown exception");}
}
OriginalAvatarRemovalAttempt remove_original_avatar_from_world(
        const odh::LiveOriginalRecoveryPlacementSource& source,SessionObjectPositionContext context,
        const OriginalAvatarRemovalServices& hooks){
    const auto authority=context.source_still_matches;
    const auto thread_for=context.thread_for;
    context.source_still_matches=[source,authority,thread_for]{
        if(!authority||!authority()||!source.source_still_matches||!source.source_still_matches()||
           !thread_for||thread_for(source.person.id)!=source.thread||!source.world)return false;
        return odh::live_original_recovery_placement_source_matches(source,*source.world);
    };
    if(context.world!=source.world){OriginalAvatarRemovalAttempt out;out.diagnostic="native avatar removal World source mismatch";return out;}
    return remove_original_entity_from_world(source.person,std::move(context),hooks);
}
OriginalAvatarPositionAttempt place_original_avatar_position(
        const odh::LiveOriginalRecoveryPlacementSource& source,const odh::EntityPosition& destination,
        odh::AvatarPositionValid& validator,SessionObjectPositionContext context,bool check_before_publish){
    OriginalAvatarPositionAttempt attempt;
    bool completed=false;
    struct Finish {
        OriginalAvatarPositionAttempt& attempt;bool& completed;
        odh::ObjectPositionRegistrySnapshot* registry;
        ~Finish(){if(registry&&attempt.effects_started&&!completed)registry->complete=false;}
    } finish{attempt,completed,context.position_registry};
    const auto reject=[&](const std::string& message){
        attempt.result.status=odh::Ts1PlacementStatus::unsupported;
        attempt.result.diagnostic=message;return attempt;
    };
    try {
        if(!context.world||context.world!=source.world||!context.source_still_matches||
           !context.position_phase||!context.native_entry_point9||!context.thread_for||
           (context.position_registry&&!context.position_registry->complete)||destination.direction>7)
            return reject("original avatar placement lacks actual lifecycle authority");
        auto scope=std::make_shared<bool>(true);
        struct EndScope {std::shared_ptr<bool> scope;~EndScope(){*scope=false;}} end_scope{scope};
        auto* world=context.world;
        const auto current=[source,world,authority=context.source_still_matches,thread_for=context.thread_for,
                weak=std::weak_ptr<bool>(scope)]{
            const auto live=weak.lock();
            return live&&*live&&authority&&authority()&&
                odh::live_original_recovery_placement_source_matches(source,*world)&&
                thread_for(source.person.id)==source.thread;
        };
        if(!current())return reject("original avatar placement source expired before PrePositionChange");
        const auto* avatar=world->resolve_reference(source.person);
        if(!avatar||!avatar->entry_points||avatar->entry_points->size()<=15)
            return reject("original avatar placement entry-point table unavailable");
        const auto heading=avatar->object_words.find(1);
        if(heading==avatar->object_words.end()||heading->second<0||heading->second>7||heading->second!=destination.direction)
            return reject("original avatar scalar placement cannot change its actual heading");
        const auto old_position=avatar->position;
        const bool contained=avatar->attachment.has_value();
        const bool has_room_entry=avatar->entry_points->at(15).action!=0;
        const odh::ObjectPositionIdentity root{source.person.id,source.person.generation};
        context.source_still_matches=current;
        auto lifecycle=make_session_object_position_services(context);
        if(!lifecycle.before_slot_transfer||!lifecycle.after_slot_transfer||!lifecycle.change.room_at)
            return reject("original avatar placement real Pre/Post services unavailable");
        const auto old_room=old_position?lifecycle.change.room_at(*old_position):std::optional<std::uint16_t>{0};
        if(!current())return reject("original avatar placement source expired resolving old room");
        const auto new_room=lifecycle.change.room_at(destination);
        if(!current()||!old_room||!new_room)
            return reject("original avatar placement geometric room unavailable");
        const bool room_change=contained||has_room_entry||*old_room!=*new_room;
        const auto phase=[&](SessionPositionPhase stage,const std::optional<odh::EntityPosition>& point,
                std::function<bool()> operation,const char* label){
            struct Invocation {
                bool active=true,entered=false,accepted=false,repeated=false;
                std::function<bool()> operation;
            };
            auto invocation=std::make_shared<Invocation>();invocation->operation=std::move(operation);
            struct EndInvocation {
                std::shared_ptr<Invocation> invocation;
                ~EndInvocation(){invocation->active=false;invocation->operation={};}
            } end{invocation};
            if(!current())return false;
            const bool accepted=context.position_phase(root,point,stage,
                stage==SessionPositionPhase::publish?std::optional<std::uint8_t>{destination.direction}:std::nullopt,
                [invocation,current]{
                    if(!invocation->active)return false;
                    if(invocation->entered){invocation->repeated=true;return false;}
                    invocation->entered=true;
                    return invocation->accepted=current()&&invocation->operation();
                });
            if(accepted&&invocation->entered&&invocation->accepted&&!invocation->repeated&&current())return true;
            attempt.result.diagnostic=std::string("original avatar ")+label+" incomplete";
            if(lifecycle.lifecycle_diagnostic){const auto detail=lifecycle.lifecycle_diagnostic();
                if(!detail.empty())attempt.result.diagnostic+="; "+detail;}
            return false;
        };
        if(!phase(SessionPositionPhase::pre,destination,[&]{
                attempt.effects_started=true;
                return lifecycle.before_slot_transfer(root,room_change);
            },"PrePositionChange"))return reject(attempt.result.diagnostic.empty()?
                "original avatar source expired before PrePositionChange":attempt.result.diagnostic);
        if(check_before_publish){
            auto scoped_source=source;scoped_source.source_still_matches=current;
            const auto checked=validator.evaluate_recovery(scoped_source,destination,destination.direction,0);
            if(!current()||checked.status==odh::AvatarPositionValidStatus::unsupported)
                return reject(checked.diagnostic.empty()?"original avatar PositionValid authority expired":checked.diagnostic);
            attempt.result.placement_error=checked.placement_error;
            attempt.result.blocker={checked.blocker,checked.blocker_generation};
            if(checked.status==odh::AvatarPositionValidStatus::blocked){
                if(!phase(SessionPositionPhase::post,old_position,[&]{return lifecycle.after_slot_transfer(root,room_change);},
                        "blocked PositionChange"))return reject(attempt.result.diagnostic);
                attempt.result.status=odh::Ts1PlacementStatus::blocked;
                attempt.result.diagnostic=checked.diagnostic;completed=true;return attempt;
            }
        }
        if(!phase(SessionPositionPhase::publish,destination,[&]{
                return world->publish_native_recovery_position(source.person,destination,false);
            },"position publication")||
           !phase(SessionPositionPhase::post,destination,[&]{return lifecycle.after_slot_transfer(root,room_change);},
                "PositionChange"))return reject(attempt.result.diagnostic);
        attempt.result.status=odh::Ts1PlacementStatus::success;completed=true;return attempt;
    }catch(const std::exception& error){return reject(std::string("original avatar placement exception: ")+error.what());}
    catch(...){return reject("original avatar placement unknown exception");}
}
}
