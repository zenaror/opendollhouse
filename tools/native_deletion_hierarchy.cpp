// SPDX-License-Identifier: MPL-2.0
#include "native_deletion_hierarchy.hpp"
#include "native_hierarchy_snapshot.hpp"
#include "odh/object_position_change.hpp"
#include <algorithm>
#include <exception>
#include <sstream>

namespace odh_host {
namespace {bool fail(std::string& error,const char* why){error=why;return false;}}
NativeDeletionHierarchy::NativeDeletionHierarchy(SessionObjectPositionContext context,
    SessionObjectPositionServices services,Ref target):context_(std::move(context)),services_(std::move(services)),target_(target){}
NativeDeletionHierarchy::~NativeDeletionHierarchy(){if(retirement_token_&&floors_)floors_->abandon_retired_container(*retirement_token_);}
bool NativeDeletionHierarchy::current()const{return context_.world&&context_.source_still_matches&&context_.source_still_matches();}
bool NativeDeletionHierarchy::unindexed_constructor_ready(std::string& error)const{
    if(!unindexed_constructor_||context_.position_registry||existing_floors_||!current()||!unindexed_construction_owner_||!unindexed_construction_owner_(target_))
        return fail(error,"unindexed constructor requires its exact active construction owner and absent registry");
    const auto* body=context_.world->resolve_reference(target_);const auto pose=context_.world->native_position(target_);
    if(!body||body->ui_ghost||body->position||body->attachment||!pose||pose->x!=-16||pose->y!=-16||!body->native_out_of_world_level||
       body->native_slot0!=std::optional<Ref>{Ref{}}||!body->object_words.count(14)||!body->object_words.count(26)||body->object_words.at(14)!=-1||body->object_words.at(26)!=0||
       std::any_of(body->slots.begin(),body->slots.end(),[](auto child){return child!=0;}))
        return fail(error,"unindexed constructor requires known OOW empty/unparented state without physical contents");
    for(std::int16_t cursor=0;;){const auto next=context_.world->next_entity(cursor,{},true);if(!next)break;cursor=*next;
        const auto* other=context_.world->find(cursor);if(other&&other->native_slot0==std::optional<Ref>{target_})return fail(error,"unindexed constructor has an incoming virtual link");}
    return current()&&unindexed_construction_owner_(target_)||fail(error,"unindexed constructor source receipt expired");
}
bool NativeDeletionHierarchy::flat_registry_ready(std::string& error)const{
    if(unindexed_constructor_)return unindexed_constructor_ready(error);
    if(!current()||!context_.position_registry||!context_.position_registry->complete||!context_.prepared||
       context_.position_registry->width!=context_.prepared->width||context_.position_registry->height!=context_.prepared->height)
        return fail(error,"runtime flat Delete requires its complete actual position registry");
    try{odh::validate_object_position_registry_snapshot(*context_.position_registry);}
    catch(const std::exception& exception){error=std::string("runtime flat position registry invalid: ")+exception.what();return false;}
    return current()||fail(error,"runtime flat registry source expired");
}
bool NativeDeletionHierarchy::flat_unregistered(Ref ref,std::string& error)const{
    if(unindexed_constructor_)return ref==target_?unindexed_constructor_ready(error):fail(error,"unindexed constructor does not own a child registration");
    if(!flat_registry_ready(error))return false;
    const auto same=[&](auto owner){return owner.id==ref.id;};
    const auto& registry=*context_.position_registry;
    for(const auto& room:registry.rooms){
        if(std::any_of(room.entities.begin(),room.entities.end(),same))return fail(error,"actual Pre left target in flat RoomInfo entities");
        for(const auto* values:{&room.static_obstacles,&room.dynamic_obstacles})for(const auto& value:*values)if(same(value.owner))return fail(error,"actual Pre left target footprint in flat RoomInfo");
        for(const auto* values:{&room.portals,&room.window_portals})for(const auto& value:*values)if(same(value.owner))return fail(error,"actual Pre left target in flat portal registry");
    }
    for(const auto& tile:registry.tiles)if(std::any_of(tile.entities.begin(),tile.entities.end(),same))return fail(error,"actual Pre left target in flat tile query list");
    // The source keeps an unbound Footprint object after unregister. Its
    // metadata may remain present, but no live room binding may remain.
    for(const auto& footprint:registry.footprints)if(same(footprint.owner)){
        if(footprint.owner.generation!=ref.generation)return fail(error,"actual Pre target footprint metadata has stale identity");
        if(footprint.room)return fail(error,"actual Pre left target footprint bound to a room");
    }
    for(const auto& motion:registry.motion)if(same(motion.owner)&&motion.owner.generation!=ref.generation)return fail(error,"actual Pre target motion metadata has stale identity");
    error.clear();return true;
}
bool NativeDeletionHierarchy::known_absent_oow(Ref ref,std::string& error)const{
    // Original virtual reset unregisters every body, but 3206d calls
    // virtual+58/Post only for an OLD in-world snapshot (see
    // SavedRecoveryObjectAdapter::restore_placement). This is a known absence
    // proof, never a claim that the OOW body has been registered.
    if(!flat_registry_ready(error)||!context_.world->is_live_reference(ref))return fail(error,"known-absent OOW exact registry owner unavailable");
    const auto* body=context_.world->resolve_reference(ref);
    if(!body||!odh::object_position_known_absent_oow(*context_.position_registry,*body,error))return false;
    return current()&&context_.world->is_live_reference(ref)||fail(error,"known-absent OOW source identity expired");
}
bool NativeDeletionHierarchy::flat_position_proven(Ref ref,std::string& error)const{
    if(unindexed_constructor_)return ref==target_?unindexed_constructor_ready(error):fail(error,"unindexed constructor does not own a child registration");
    if(!flat_registry_ready(error)||!context_.world->is_live_reference(ref))return fail(error,"runtime flat registration exact identity unavailable");
    const auto* body=context_.world->resolve_reference(ref);if(!body)return false;
    const auto& registry=*context_.position_registry;
    std::int64_t expected=-1;std::optional<std::uint16_t> room=0;
    if(body->position){const auto pose=*body->position;
        if(pose.x<0||pose.y<0||pose.level<1||pose.level>2||pose.x/16>=std::int32_t(registry.width)||pose.y/16>=std::int32_t(registry.height))return fail(error,"runtime flat registration position is outside source registry extent");
        expected=pose.x/16+std::int64_t(pose.y/16)*registry.width+std::int64_t(pose.level-1)*registry.width*registry.height;
        room=services_.change.room_at(pose);if(!room)return fail(error,"runtime flat registration room owner unavailable");
    }
    unsigned tiles=0,rooms=0;
    for(const auto& tile:registry.tiles)for(const auto owner:tile.entities)if(owner.id==ref.id){
        if(owner.generation!=ref.generation||tile.offset!=expected)return fail(error,"runtime flat tile registration contains stale identity or wrong current position");++tiles;
    }
    for(const auto& value:registry.rooms)for(const auto owner:value.entities)if(owner.id==ref.id){
        if(owner.generation!=ref.generation||value.id!=*room)return fail(error,"runtime flat RoomInfo registration contains stale identity or wrong current room");++rooms;
    }
    if(tiles==0&&rooms==0&&!body->position)return known_absent_oow(ref,error);
    if(tiles!=1||rooms!=1){
        std::ostringstream detail;detail<<"runtime flat exact entity lacks unique actual room/tile registration"
            <<" id="<<ref.id<<" generation="<<ref.generation<<" expected_tile="<<expected
            <<" expected_room="<<*room<<" actual_tile_count="<<tiles<<" actual_room_count="<<rooms;
        const auto pose=context_.world->native_position(ref);detail<<" native_position=";
        if(pose)detail<<pose->x<<','<<pose->y<<','<<pose->level;else detail<<"unknown";
        error=detail.str();return false;
    }
    error.clear();return true;
}
bool NativeDeletionHierarchy::flat_cleanup_legacy(const NativeHierarchySnapshot& before,Ref expected_zero,const std::optional<Proof>& admitted_child,std::string& error){
    if(!flat_unregistered(target_,error)||before.root!=target_)return false;
    const auto zero=context_.world->native_slot0(target_);if(!zero)return fail(error,"runtime legacy cleanup root zero storage unknown");
    const auto* root=context_.world->resolve_reference(target_);if(!root||root->attachment)return fail(error,"actual flat Pre did not detach its physical root");
    if(!(context_.world->native_position(target_)==before.native_root_position))return fail(error,"actual flat Pre changed the captured root native pose");
    if(*zero!=expected_zero)return fail(error,"runtime legacy zero child changed during actual Pre");
    const bool virtual_parent=before.parent&&before.slot==0;
    if(virtual_parent){
        const auto* parent=context_.world->resolve_reference(*before.parent);
        if(!parent||parent->native_slot0!=std::optional<Ref>{target_})return fail(error,"runtime legacy parent no longer owns the exact virtual child");
    }
    Proof child_before;const auto child=zero->id?zero:std::optional<Ref>{};
    if(child){
        if(!admitted_child||admitted_child->reference!=*child)return fail(error,"runtime legacy child differs from its admitted before-Pre identity");
        if(!matches(*admitted_child,true,error)){error="runtime legacy child changed from its admitted before-Pre state: "+error;return false;}
        child_before=*admitted_child;
        if(child_before.attachment||child_before.raw_slot!=0||child_before.raw_parent!=target_.id)return fail(error,"runtime legacy child lacks its exact independent virtual backing");
        if(!flat_position_proven(*child,error))return false;
    }
    // LinkedSlot0 is not consumed as managed Slots/Container by Simitone's
    // TS1 activator. Clean only this compatibility graph for World's retirement
    // guard: never put its child into an old physical slot or copy XYZ/level.
    if(virtual_parent&&!context_.world->publish_native_slot0(*before.parent,{}))return fail(error,"runtime legacy parent unlink failed");
    if(!context_.world->publish_native_slot0(target_,{})||!context_.world->declare_object_word(target_.id,14,-1)||!context_.world->declare_object_word(target_.id,26,0))return fail(error,"runtime legacy root cleanup failed");
    if(child){
        if(!context_.world->declare_object_word(child->id,14,virtual_parent?0:-1)||!context_.world->declare_object_word(child->id,26,virtual_parent?before.parent->id:0)||
           (virtual_parent&&!context_.world->publish_native_slot0(*before.parent,*child)))return fail(error,"runtime legacy virtual child splice failed");
        const auto* after=context_.world->resolve_reference(*child);
        if(!after||!(context_.world->native_position(*child)==child_before.pose)||after->attachment||after->slots!=child_before.slots||!flat_position_proven(*child,error))
            return fail(error,"runtime legacy cleanup changed the child's actual pose/physical contents or lost registration");
    }
    error.clear();return true;
}
bool NativeDeletionHierarchy::snapshot(Ref ref,Proof& result,std::string& error)const{
    if(!current())return fail(error,"deletion hierarchy source lease expired");
    const auto* e=context_.world->resolve_reference(ref);
    if(!e||e->ui_ghost||!e->native_slot0||!e->object_words.count(14)||!e->object_words.count(26))
        return fail(error,"deletion hierarchy exact body/backing unavailable");
    result={ref,context_.world->native_position(ref),e->native_slot0,context_.world->attachment_parent(ref),e->attachment,
        e->object_words.at(14),e->object_words.at(26),e->slots};
    if(result.pose)return true;
    // Keep root and closure failures distinct: a held object's parent can lose
    // its native OOW level independently of the object being deleted.
    std::ostringstream details;
    const auto position=[&](const std::optional<odh::EntityPosition>& pose){
        if(pose)details<<'['<<pose->x<<','<<pose->y<<','<<pose->level<<','<<unsigned(pose->direction)<<']';
        else details<<"unknown";
    };
    const auto reference=[&](const std::optional<Ref>& value){if(value)details<<value->id<<':'<<value->generation;else details<<"none";};
    const auto* root=context_.world->resolve_reference(target_);
    const char* role=ref==target_?"root":root&&root->native_slot0==std::optional<Ref>{ref}?"native_zero_child":
        context_.world->attachment_parent(target_)==std::optional<Ref>{ref}?"physical_parent":"native_closure";
    details<<"deletion hierarchy native scalar unavailable subject="<<ref.id<<':'<<ref.generation
        <<" role="<<role<<" target="<<target_.id<<':'<<target_.generation<<" represented_position=";
    if(e->position)position(e->position);else details<<"none";details<<" native_position=";position(result.pose);
    details<<" native_oow_level=";if(e->native_out_of_world_level)details<<*e->native_out_of_world_level;else details<<"unknown";
    details<<" raw14="<<result.raw_slot<<" raw26="<<result.raw_parent<<" direction=";
    const auto direction=e->object_words.find(1);if(direction==e->object_words.end())details<<"unknown";else details<<direction->second;
    details<<" physical_parent=";reference(result.parent);
    if(e->attachment)details<<" physical_slot="<<e->attachment->slot<<" positioned="<<(e->attachment->positioned?"true":"false");
    if(result.parent){const auto* parent=context_.world->resolve_reference(*result.parent);
        details<<" parent_native_position=";position(context_.world->native_position(*result.parent));
        details<<" parent_native_oow_level=";if(parent&&parent->native_out_of_world_level)details<<*parent->native_out_of_world_level;else details<<"unknown";
    }
    error=details.str();return false;
}
bool NativeDeletionHierarchy::matches(const Proof& saved,bool slots,std::string& error)const{
    Proof now;if(!snapshot(saved.reference,now,error))return false;
    const auto attachment=[](const auto& a,const auto& b){return bool(a)==bool(b)&&(!a||(a->container==b->container&&a->slot==b->slot&&a->positioned==b->positioned));};
    return now.pose==saved.pose&&now.child==saved.child&&now.parent==saved.parent&&attachment(now.attachment,saved.attachment)&&
        now.raw_slot==saved.raw_slot&&now.raw_parent==saved.raw_parent&&(!slots||now.slots==saved.slots)||
        fail(error,"deletion hierarchy source changed between owned phases");
}
bool NativeDeletionHierarchy::detached(std::string& error)const{
    Proof root;if(!snapshot(target_,root,error))return false;
    if(root.child!=std::optional<Ref>{Ref{}}||root.attachment||root.raw_parent||root.raw_slot!=-1)
        return fail(error,"deletion hierarchy receiver is not natively detached");
    for(std::int16_t cursor=0;;){const auto next=context_.world->next_entity(cursor,{},true);if(!next)break;cursor=*next;
        const auto* e=context_.world->find(cursor);
        if(e&&!e->ui_ghost&&e->native_slot0==std::optional<Ref>{target_})return fail(error,"deletion hierarchy receiver retains an incoming raw-zero link");}
    return current()||fail(error,"deletion hierarchy authority expired while checking incoming links");
}
std::unique_ptr<NativeDeletionHierarchy> NativeDeletionHierarchy::prepare(SessionObjectPositionContext context,
        SessionObjectPositionServices services,Ref ref,std::string& error,DropOwner drop_owner,
        std::shared_ptr<NativeFloorHeads> existing,NativeFloorHeads::PendingUnlinkedSource pending,UnindexedConstructionOwner unindexed_owner){
    auto lease=std::unique_ptr<NativeDeletionHierarchy>(new NativeDeletionHierarchy(std::move(context),std::move(services),ref));
    lease->drop_owner_=std::move(drop_owner);lease->existing_floors_=std::move(existing);lease->pending_unlinked_source_=std::move(pending);
    lease->unindexed_construction_owner_=std::move(unindexed_owner);
    auto& c=lease->context_;auto& s=lease->services_.change;
    lease->unindexed_constructor_=!c.position_registry&&!lease->existing_floors_&&bool(lease->unindexed_construction_owner_);
    if(lease->unindexed_constructor_&&!lease->unindexed_constructor_ready(error))return {};
    if(!lease->current()||!c.prepared||!c.position_phase||!c.slot_phase||!s.unregister_position||!s.register_position||!s.room_at||!s.wall_use||
       !s.object_supported||(!s.admit_loaded_entity&&!s.admit_single_ordinary)||!c.world->is_live_reference(ref)){
        fail(error,"deletion hierarchy requires actual phase/registry/support/admission owners before cleanup");return {};
    }
    if(s.admit_loaded_entity)s.admit_single_ordinary=s.admit_loaded_entity;
    const auto* receiver=c.world->resolve_reference(ref);
    if(!receiver||(std::any_of(receiver->slots.begin(),receiver->slots.end(),[](auto child){return child!=0;})&&!lease->drop_owner_)){
        fail(error,"deletion hierarchy physical contents require the actual source drop owner before cleanup");return {};
    }
    auto hierarchy=capture_native_direct_hierarchy_snapshot(*c.world,ref,c.source_still_matches,error);if(!hierarchy)return {};
    lease->closure_.push_back(ref);if(hierarchy->parent)lease->closure_.push_back(*hierarchy->parent);
    const auto zero=c.world->native_slot0(ref);if(!zero){fail(error,"deletion hierarchy raw-zero backing unavailable");return {};}
    if(zero->id)lease->closure_.push_back(*zero);
    // Admission for the entire physical Pre tree is read-only and precedes VM
    // cleanup. Native slot0/parent resources also need their actual owners.
    std::vector<Ref> admission=lease->closure_;
    for(std::size_t n=0;n<admission.size();++n){
        if(n>=4096){fail(error,"deletion hierarchy admission closure exceeds budget");return {};}
        const auto r=admission[n];const auto yes=s.admit_single_ordinary({r.id,r.generation});
        if(!yes||!*yes||!lease->current()){fail(error,"deletion hierarchy loaded resource admission unavailable");return {};}
        const auto* body=c.world->resolve_reference(r);if(!body){fail(error,"deletion hierarchy admission body expired");return {};}
        for(const auto child:body->slots)if(child){const auto* value=c.world->find(child);if(!value){fail(error,"deletion hierarchy physical child absent");return {};}
            const Ref next{value->id,value->generation};if(std::find(admission.begin(),admission.end(),next)==admission.end())admission.push_back(next);}
    }
    lease->floors_=lease->existing_floors_;
    if(lease->floors_){
        if(!lease->floors_->original_cohort_owner()){fail(error,"original recovery requires its strict native floor-head owner");return {};}
        if(!hierarchy->parent&&hierarchy->native_root_position&&native_in_world_xy(*hierarchy->native_root_position,c.prepared->width,c.prepared->height)){
            std::string reason;const auto head=lease->floors_->checked_head(*hierarchy->native_root_position,reason);
            if(head!=std::optional<Ref>{ref}){error="original recovery consulted native floor head is unknown or differs from target; "+reason;return {};}
        }
    }else if(!lease->flat_position_proven(ref,error))return {};
    for(const auto r:lease->closure_){Proof proof;if(!lease->snapshot(r,proof,error))return {};lease->proofs_.push_back(std::move(proof));}
    if(!native_hierarchy_snapshot_still_matches(*c.world,*hierarchy,c.source_still_matches,error))return {};
    if(lease->floors_)lease->placement_=std::make_unique<NativeRecoveryPlacement>(c,lease->services_,lease->floors_);
    error.clear();return lease;
}
bool NativeDeletionHierarchy::certified_detached()const{
    // Read-only fact, not execution admission. The queue provider separately
    // proves it owns this operation; an entry-only execution guard may be closed
    // between immediate and deferred Pre without erasing the unlink receipt.
    if(!unlinked_||failed_||finished_||!detached_pose_||!context_.world||!context_.world->is_live_reference(target_))return false;
    const auto revision=context_.world->spatial_revision();const auto* body=context_.world->resolve_reference(target_);
    const auto pose=context_.world->native_position(target_);
    if(!body||!pose||pose->x!=detached_pose_->x||pose->y!=detached_pose_->y||pose->level!=detached_pose_->level||body->attachment||
       body->native_slot0!=std::optional<Ref>{Ref{}}||!body->object_words.count(14)||!body->object_words.count(26)||body->object_words.at(14)!=-1||body->object_words.at(26)!=0)return false;
    for(std::int16_t cursor=0;;){const auto next=context_.world->next_entity(cursor,{},true);if(!next)break;cursor=*next;
        const auto* e=context_.world->find(cursor);if(e&&!e->ui_ghost&&e->native_slot0==std::optional<Ref>{target_})return false;}
    return context_.world->is_live_reference(target_)&&context_.world->spatial_revision()==revision;
}
bool NativeDeletionHierarchy::before_position_change(std::string& error){
    if(entered_||finished_||failed_||retired_forgotten_||!current()||!context_.world->is_live_reference(target_))return fail(error,"deletion hierarchy lease unavailable or reentered");
    if(unlinked_&&!certified_detached()){
        // Source may attach/move a queued object again before end-frame. A real
        // new physical/raw parent or new XYZ rearms native unlink; a bare final
        // Pre never repeats it on the old pending scalar.
        const auto* body=context_.world->resolve_reference(target_);const auto pose=context_.world->native_position(target_);
        const bool moved=pose&&detached_pose_&&(pose->x!=detached_pose_->x||pose->y!=detached_pose_->y||pose->level!=detached_pose_->level);
        if(!body||(!body->attachment&&(!body->object_words.count(26)||!body->object_words.at(26))&&!moved)){
            failed_=true;return fail(error,"pending Delete topology changed without an actual reattachment/position owner");
        }
        unlinked_=false;detached_pose_.reset();
    }
    if(floors_){
        if(unlinked_&&!floors_->mark_source_unlinked(target_,error)){failed_=true;return false;}
        if(!unlinked_){
            const auto source=capture_native_direct_hierarchy_snapshot(*context_.world,target_,context_.source_still_matches,error);if(!source){failed_=true;return false;}
            if(!source->parent&&source->native_root_position&&native_in_world_xy(*source->native_root_position,context_.prepared->width,context_.prepared->height)&&floors_->head(*source->native_root_position)!=std::optional<Ref>{target_}){failed_=true;return fail(error,"original recovery source floor ownership changed before Pre");}
        }
    }else if(!flat_registry_ready(error)){failed_=true;return false;}
    if(floors_)placement_=std::make_unique<NativeRecoveryPlacement>(context_,services_,floors_);
    // Only this target's current state belongs to the ongoing Delete receipt;
    // previously promoted children/parents continue their own live lifecycles.
    proofs_.clear();Proof current_root;if(!snapshot(target_,current_root,error)){failed_=true;return false;}proofs_.push_back(current_root);
    std::optional<NativeHierarchySnapshot> runtime_legacy;std::optional<Ref> runtime_zero;std::optional<Proof> runtime_child;
    if(!floors_&&!unlinked_){
        runtime_legacy=capture_native_direct_hierarchy_snapshot(*context_.world,target_,context_.source_still_matches,error);runtime_zero=context_.world->native_slot0(target_);
        if(!runtime_legacy||!runtime_zero){failed_=true;return fail(error,"runtime legacy cleanup before-Pre backing unavailable");}
        if(runtime_zero->id){Proof child;if(!snapshot(*runtime_zero,child,error)||!flat_position_proven(*runtime_zero,error)){failed_=true;return false;}runtime_child=std::move(child);}
    }
    entered_=true;struct End{bool& entered;~End(){entered=false;}}end{entered_};
    auto services=services_.change;bool unlink_requested=false;
    const auto unlink=[&](){
        if(unlinked_)return detached(error);
        if(unlink_requested)return fail(error,"deletion hierarchy unlink requested twice");
        unlink_requested=true;++unlink_calls_;
        if(floors_?!placement_->unlink(target_,error):(!runtime_legacy||!runtime_zero||!flat_cleanup_legacy(*runtime_legacy,*runtime_zero,runtime_child,error)))return false;
        unlinked_=true;detached_pose_=context_.world->native_position(target_);return detached(error)&&(floors_?floors_->mark_source_unlinked(target_,error):flat_unregistered(target_,error));
    };
    if(floors_)services.detach_container=[&](auto child,auto,std::size_t){return child.id==target_.id&&child.generation==target_.generation&&unlink();};
    odh::ObjectPositionValid valid(*context_.world,services_.placement);
    odh::ObjectPositionChange lifecycle(*context_.world,valid,services);
    struct Call {bool active=true,called=false,repeated=false;std::function<bool()> operation;};
    auto call=std::make_shared<Call>();
    struct Expire {std::shared_ptr<Call> call;~Expire(){call->active=false;call->operation={};}}expire{call};
    call->operation=[&](){effects_=true;++pre_calls_;return lifecycle.pre_position_change({target_.id,target_.generation},true,context_.source_still_matches);};
    const auto operation=[call](){if(!call->active)return false;if(call->called){call->repeated=true;return false;}call->called=true;return call->operation();};
    bool completed=false;
    try{completed=services.lifecycle_phase?services.lifecycle_phase({target_.id,target_.generation},odh::ObjectPositionChangePhase::pre,operation):operation();}
    catch(const std::exception& exception){failed_=true;error=std::string("deletion hierarchy phase exception: ")+exception.what();return false;}
    catch(...){failed_=true;return fail(error,"deletion hierarchy phase exception");}
    if(!completed||!call->called||call->repeated||!current()||(!unlinked_&&!unlink())||!detached(error)){
        failed_=true;if(error.empty())error="deletion hierarchy Pre/unlink incomplete: "+lifecycle.diagnostic();return false;
    }
    if(!floors_&&!flat_unregistered(target_,error)){failed_=true;return false;}
    proofs_.clear();Proof proof;if(!snapshot(target_,proof,error)){failed_=true;return false;}proofs_.push_back(std::move(proof));
    error.clear();return true;
}
bool NativeDeletionHierarchy::copy_retired_child_position(const odh::World::RetiredContainer& token,Ref child,
        const std::function<bool()>& actual,std::string& error){
    if(!drop_active_||failed_||!current()||!actual||token.parent()!=target_||
       !context_.world->retired_container_active(token)||!context_.world->retired_child_authorized(token,child))
        return fail(error,"deletion child copy requires its actual active retired owner");
    const auto* source=context_.world->resolve_reference(child);const auto* parent=context_.world->resolve_reference(target_);
    if(!source||!parent||!parent->object_words.count(1)||parent->object_words.at(1)<0||parent->object_words.at(1)>7)return fail(error,"deletion child copy scalar backing unavailable");
    auto expected=source->position;
    if(expected)expected->direction=parent->position?parent->position->direction:std::uint8_t(parent->object_words.at(1));
    const auto expected_level=source->native_out_of_world_level;
    if(!actual()||!current()){failed_=true;return fail(error,"actual retained child position copy failed");}
    parent=context_.world->resolve_reference(target_);
    if(!parent||!(parent->position==expected)||(!expected&&parent->native_out_of_world_level!=expected_level)){
        failed_=true;return fail(error,"retained parent position differs from actual child copy");
    }
    for(auto& proof:proofs_)if(proof.reference==target_)proof.pose=context_.world->native_position(target_);
    error.clear();return true;
}
odh::Ts1PlacementResult NativeDeletionHierarchy::place_retired_child(const odh::World::RetiredContainer& token,Ref child,
        const std::function<odh::Ts1PlacementResult()>& actual,std::string& error){
    odh::Ts1PlacementResult result;result.status=odh::Ts1PlacementStatus::unsupported;
    if(!drop_active_||failed_||!current()||!actual||token.parent()!=target_||
       !context_.world->retired_container_active(token)||!context_.world->retired_child_authorized(token,child)||
       (floors_?!floors_->begin_retired_child_position(token,child,error):!flat_position_proven(child,error))){
        if(drop_active_)failed_=true;result.diagnostic=error.empty()?"deletion child placement requires its active source owner":error;return result;
    }
    try{result=actual();}
    catch(const std::exception& exception){error=exception.what();failed_=true;result.diagnostic=error;return result;}
    catch(...){error="deletion child placement exception";failed_=true;result.diagnostic=error;return result;}
    if(result.status==odh::Ts1PlacementStatus::unsupported||!current()||
       (floors_?!floors_->end_retired_child_position(token,child,error):(!flat_position_proven(child,error)||!flat_unregistered(target_,error)))){
        failed_=true;result.status=odh::Ts1PlacementStatus::unsupported;
        if(!error.empty())result.diagnostic=error;return result;
    }
    // A successful actual FindLocation may put the released child into a
    // surviving container which was also the deleted root's old parent. Admit
    // exactly that reciprocal slot insertion, not arbitrary closure changes.
    const auto parent=context_.world->attachment_parent(child);
    const auto* body=context_.world->resolve_reference(child);
    if(parent&&body&&body->attachment)for(auto& proof:proofs_)if(proof.reference==*parent&&proof.reference!=target_){
        Proof now;auto expected=proof.slots;const auto slot=body->attachment->slot;
        if(!snapshot(*parent,now,error)||!matches(proof,false,error)){
            failed_=true;result.status=odh::Ts1PlacementStatus::unsupported;result.diagnostic=error;return result;
        }
        if(slot>=expected.size())expected.resize(slot+1);
        if(expected[slot]&&expected[slot]!=child.id){failed_=true;result.status=odh::Ts1PlacementStatus::unsupported;result.diagnostic="source child placement replaced an unowned surviving slot";return result;}
        expected[slot]=child.id;if(expected.size()<now.slots.size())expected.resize(now.slots.size());
        if(expected!=now.slots){failed_=true;result.status=odh::Ts1PlacementStatus::unsupported;result.diagnostic="source child placement changed unrelated surviving slots";return result;}
        proof.slots=std::move(expected);
    }
    error.clear();return result;
}
bool NativeDeletionHierarchy::begin_indexed_retirement(const odh::World::RetiredContainer& token,std::string& error){
    if(retirement_token_){
        return !failed_&&current()&&(!unindexed_constructor_||unindexed_constructor_ready(error))&&token.parent()==target_&&retirement_token_->parent()==target_&&
            context_.world->retired_container_active(token)&&context_.world->retired_container_active(*retirement_token_)||
            fail(error,"deletion indexed-retirement token expired or changed");
    }
    if(entered_||finished_||failed_||!unlinked_||retired_forgotten_||!current()||token.parent()!=target_||
       !context_.world->retired_container_active(token)||context_.world->is_live_reference(target_))
        return fail(error,"deletion indexed retirement requires the exact actual removal boundary");
    for(const auto& proof:proofs_)if(!matches(proof,true,error)){failed_=true;return false;}
    if(!detached(error)||(floors_?!floors_->begin_retired_container(token,error):!flat_unregistered(target_,error))){failed_=true;return false;}
    retired_forgotten_=true;retirement_token_=token;error.clear();return true;
}
bool NativeDeletionHierarchy::drop_retired_contents(const odh::World::RetiredContainer& token,std::string& error){
    if(entered_||finished_||failed_||drop_completed_||!drop_owner_||!begin_indexed_retirement(token,error))
        return fail(error,"deletion contents require the exact active indexed-retirement boundary");
    drop_active_=true;entered_=true;
    struct End{NativeDeletionHierarchy& self;const odh::World::RetiredContainer& token;
        ~End(){self.drop_active_=false;self.entered_=false;if(self.floors_)self.floors_->abandon_retired_container(token);}}end{*this,token};
    bool completed=false;
    try{completed=drop_owner_(token,*this,error);}
    catch(const std::exception& exception){error=exception.what();}
    catch(...){error="actual source container drop exception";}
    if(!completed||failed_||!current()||(floors_?!floors_->end_retired_container(token,error):!flat_unregistered(target_,error))){
        failed_=true;if(error.empty())error="actual source container drop incomplete";return false;
    }
    const auto count=context_.world->retired_slot_count(token);if(!count){failed_=true;return fail(error,"retired source slots unavailable at drop completion");}
    for(std::size_t slot=0;slot<*count;++slot){const auto child=context_.world->retired_slot(token,slot);if(!child||child->id||child->generation){failed_=true;return fail(error,"actual source drop left a physical child in its retained parent");}}
    drop_completed_=true;error.clear();return true;
}
bool NativeDeletionHierarchy::finish(std::string& error){
    if(entered_||finished_||failed_||!unlinked_||!current()||context_.world->is_live_reference(target_)||(retirement_token_&&!drop_completed_))
        return fail(error,"deletion hierarchy finish requires actual retirement under its source lease");
    for(const auto& proof:proofs_)if(!matches(proof,proof.reference!=target_,error)){failed_=true;return false;}
    if(!detached(error)||(floors_?((!retired_forgotten_&&!floors_->forget_unlinked(target_,error))||!floors_->settled(error)):!flat_unregistered(target_,error))){failed_=true;return false;}
    finished_=true;error.clear();return true;
}
}
