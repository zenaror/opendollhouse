// SPDX-License-Identifier: MPL-2.0
// Source: objt-version-audit/{00432664,004327c8,00407498,004328f3}.c.txt.
#include "native_hierarchy_snapshot.hpp"
#include <memory>
#include <set>

namespace odh_host {
namespace {
using Ref=odh::WorldEntityReference;
bool fail(std::string& error,const char* message){error=message;return false;}
const odh::Entity* body(const odh::World& world,Ref ref){
    const auto* e=world.find(ref.id);
    return e&&ref.generation&&e->generation==ref.generation&&!e->ui_ghost?e:nullptr;
}
std::optional<Ref> resolve_parent(const odh::World& world,std::int16_t id){
    if(id<=0)return {};
    const auto* e=world.find(id);
    return e?std::optional<Ref>{{id,e->generation}}:std::nullopt;
}
std::optional<NativeHierarchyNode> node(const odh::World& world,Ref ref){
    const auto* e=body(world,ref);if(!e)return {};
    const auto slot=e->object_words.find(14),parent=e->object_words.find(26);
    if(slot==e->object_words.end()||parent==e->object_words.end())return {};
    return NativeHierarchyNode{ref,slot->second,parent->second};
}
bool reciprocal(const odh::World& world,const NativeHierarchyNode& child,
    const std::optional<Ref>& parent,std::string& error){
    const auto* e=body(world,child.reference);if(!e)return fail(error,"native hierarchy child unavailable");
    if(!parent)return !e->attachment||fail(error,"native NULL parent differs from physical containment");
    if(child.raw_slot14==0){
        const auto stored=world.native_slot0(*parent);
        return (!e->attachment&&stored&&*stored==child.reference)||
            fail(error,"native zero-slot relation lacks exact reciprocal backing storage");
    }
    if(child.raw_slot14<1)return fail(error,"native parent has invalid negative vector slot");
    const auto* p=body(world,*parent);const auto slot=std::size_t(child.raw_slot14-1);
    return (p&&e->attachment&&world.attachment_parent(child.reference)==parent&&
        e->attachment->slot==slot&&slot<p->slots.size()&&p->slots[slot]==child.reference.id)||
        fail(error,"native positive slot differs from physical reciprocal containment");
}
std::optional<NativeHierarchySnapshot> capture(const odh::World& world,Ref root,
    const std::function<bool()>& authority,std::string& error,bool walked){
    error.clear();
    if(!authority||!authority()){fail(error,"native hierarchy source authority unavailable");return {};}
    const auto* e=body(world,root);
    if(!e){fail(error,"native hierarchy root identity unavailable");return {};}
    NativeHierarchySnapshot result;result.root=root;result.root_position=e->position;result.native_root_position=world.native_position(root);
    std::set<Ref> seen;
    auto current=root;
    for(;;){
        if(!seen.insert(current).second){fail(error,"native zero-slot hierarchy cycle");return {};}
        const auto entry=node(world,current);
        if(!entry){fail(error,"native hierarchy identity or raw words unavailable");return {};}
        result.chain.push_back(*entry);
        const auto parent=resolve_parent(world,entry->raw_parent26);
        if(parent&&!body(world,*parent)){fail(error,"native hierarchy resolved parent is not a native entity");return {};}
        if(!reciprocal(world,*entry,parent,error))return {};
        if(walked&&entry->raw_slot14==0&&parent){current=*parent;continue;}
        result.hierarchy_owner=current;result.parent=parent;result.slot=entry->raw_slot14;
        if(walked&&result.slot!=-1){
            --result.slot;
            if(result.slot==-1)result.normalize_zero_owner=current;
        }
        break;
    }
    if(!native_hierarchy_snapshot_still_matches(world,result,authority,error))return {};
    return result;
}
}
bool native_hierarchy_snapshot_still_matches(const odh::World& world,
    const NativeHierarchySnapshot& snapshot,const std::function<bool()>& authority,std::string& error){
    if(!authority||!authority())return fail(error,"native hierarchy source authority expired");
    if(snapshot.chain.empty()||snapshot.chain.front().reference!=snapshot.root||
       snapshot.chain.back().reference!=snapshot.hierarchy_owner)
        return fail(error,"native hierarchy snapshot has no authenticated chain");
    const auto* root=body(world,snapshot.root);
    if(!root||!(root->position==snapshot.root_position))return fail(error,"native hierarchy root pose or generation changed");
    if(!(world.native_position(snapshot.root)==snapshot.native_root_position))return fail(error,"native hierarchy native scalar pose changed");
    for(const auto& saved:snapshot.chain){
        const auto now=node(world,saved.reference);
        if(!now||now->raw_slot14!=saved.raw_slot14||now->raw_parent26!=saved.raw_parent26)
            return fail(error,"native hierarchy raw chain or generation changed");
        if(!reciprocal(world,saved,resolve_parent(world,saved.raw_parent26),error))return false;
    }
    const auto parent=resolve_parent(world,snapshot.chain.back().raw_parent26);
    if(parent!=snapshot.parent||(parent&&!body(world,*parent)))
        return fail(error,"native hierarchy resolved parent identity changed");
    error.clear();return true;
}
std::optional<NativeHierarchySnapshot> capture_native_hierarchy_snapshot(const odh::World& world,
    Ref root,const std::function<bool()>& authority,std::string& error){return capture(world,root,authority,error,true);}
std::optional<NativeHierarchySnapshot> capture_native_direct_hierarchy_snapshot(const odh::World& world,
    Ref root,const std::function<bool()>& authority,std::string& error){return capture(world,root,authority,error,false);}
bool unlink_native_hierarchy_for_out_of_world(odh::World& world,Ref root,
    std::optional<odh::EntityPosition> old_pose,const std::function<bool()>& authority,
    std::string& error,const NativeHierarchyUnlinkServices& services){
    const auto saved=capture_native_direct_hierarchy_snapshot(world,root,authority,error);
    if(!saved)return false;
    if(saved->root_position&&!(saved->root_position==old_pose))
        return fail(error,"native unlink pose differs from the captured receiver pose");
    if(saved->native_root_position&&!(saved->native_root_position==old_pose))
        return fail(error,"native unlink scalar differs from authenticated native state");
    std::optional<bool> root_in_world=false;
    if(!saved->parent){
        if(services.native_in_world)root_in_world=services.native_in_world(root);
        else if(!old_pose||old_pose->x!=-16||old_pose->y!=-16)root_in_world.reset();
        if(!root_in_world)return fail(error,"native direct InWorld bounds proof unavailable");
        if(*root_in_world&&!saved->root_position)return fail(error,"native InWorld proof contradicts the represented scalar pose");
    }
    if(!native_hierarchy_snapshot_still_matches(world,*saved,authority,error))return false;
    const auto child_link=world.native_slot0(root);
    if(!child_link)return fail(error,"native forced-OOW requires known slot-zero backing storage");
    const auto child=child_link->id?child_link:std::optional<Ref>{};
    std::optional<NativeHierarchySnapshot> child_saved;
    if(child){
        child_saved=capture_native_direct_hierarchy_snapshot(world,*child,authority,error);
        if(!child_saved||child_saved->parent!=std::optional<Ref>{root}||child_saved->slot!=0)
            return fail(error,"native forced-OOW child is not exact reciprocal slot zero");
    }
    if(saved->parent&&saved->slot>0&&!services.physical_mutation)
        return fail(error,"native physical hierarchy mutation owner unavailable");
    const bool writes_level=child&&(saved->parent||*root_in_world);
    if(writes_level){
        const auto* c=body(world,*child);
        const auto child_pose=world.native_position(*child);
        if(!old_pose||!c||!child_pose)
            return fail(error,"native promoted child level lacks represented source pose");
        if(c->position&&child_pose->level!=old_pose->level&&!services.publish_level)
            return fail(error,"native promoted child requires actual level publication owner");
    }
    if(child&&!saved->parent&&*root_in_world){
        const auto* c=body(world,*child);
        if(!services.register_promoted_floor||!c||!c->position||
           c->position->x/16!=saved->root_position->x/16||c->position->y/16!=saved->root_position->y/16)
            return fail(error,"native floor promotion requires actual source-tile registry owner");
    }
    // Recheck after all caller authority callbacks, before the first write.
    if(!native_hierarchy_snapshot_still_matches(world,*saved,authority,error)||
       (child_saved&&!native_hierarchy_snapshot_still_matches(world,*child_saved,authority,error)))return false;
    if(world.native_slot0(root)!=child_link)return fail(error,"native slot-zero child changed before unlink");
    const auto live=[&](Ref reference){
        return authority&&authority()&&body(world,reference)&&body(world,root)&&
            (!saved->parent||body(world,*saved->parent))&&(!child||body(world,*child));
    };
    const auto unlink=[&](const NativeHierarchySnapshot& snapshot)->bool{
        if(!native_hierarchy_snapshot_still_matches(world,snapshot,authority,error))return false;
        if(snapshot.parent){
            if(snapshot.slot==0){
                if(!world.publish_native_slot0(*snapshot.parent,{}))return false;
            }else{
                const auto slot=std::size_t(snapshot.slot-1);
                const std::function<bool()> operation=[&]{return
                    native_hierarchy_snapshot_still_matches(world,snapshot,authority,error)&&world.detach(snapshot.parent->id,slot);};
                if(!services.physical_mutation(snapshot.root,snapshot.parent,slot,operation))return false;
            }
        }else if(*root_in_world&&snapshot.root_position&&services.unregister_native_floor){
            if(!services.unregister_native_floor(snapshot.root,*snapshot.root_position,error)||!live(snapshot.root))return false;
        }
        if(!live(snapshot.root))return false;
        return world.declare_object_word(snapshot.root.id,14,-1)&&world.declare_object_word(snapshot.root.id,26,0);
    };
    if(!unlink(*saved))return fail(error,"native hierarchy unlink failed; candidate may be partially changed");
    if(!child)return live(root)||fail(error,"native hierarchy source expired after unlink");
    if(!unlink(*child_saved))return fail(error,"native slot-zero child unlink failed after receiver unlink");
    if(!live(*child))return fail(error,"native hierarchy identities expired after unlink");
    if(saved->parent){
        if(saved->slot==0){
            if(!world.declare_object_word(child->id,14,0)||!world.declare_object_word(child->id,26,saved->parent->id)||
               !world.publish_native_slot0(*saved->parent,*child))
                return fail(error,"native slot-zero promotion failed after unlink");
        }else{
            const auto slot=std::size_t(saved->slot-1);
            const std::function<bool()> operation=[&]{
                if(!live(*child))return false;
                const auto c=node(world,*child);const auto* p=body(world,*saved->parent);
                return c&&c->raw_slot14==-1&&c->raw_parent26==0&&p&&
                    slot<p->slots.size()&&p->slots[slot]==0&&world.attach(saved->parent->id,slot,child->id)&&
                    world.declare_object_word(child->id,14,static_cast<std::int16_t>(saved->slot))&&
                    world.declare_object_word(child->id,26,saved->parent->id);
            };
            if(!services.physical_mutation(*child,saved->parent,slot,operation))
                return fail(error,"native physical-slot promotion failed after unlink");
        }
    }else if(*root_in_world){
        if(!services.register_promoted_floor(*child,*saved->root_position,error))
            return !error.empty()?false:fail(error,"native promoted floor publication failed after unlink");
    }
    if(writes_level){
        if(!live(*child))return fail(error,"native promoted child identity expired before level publication");
        const auto* c=body(world,*child);
        const auto child_pose=world.native_position(*child);
        if(!c||!child_pose)return fail(error,"native promoted child pose expired after unlink");
        if(child_pose->level!=old_pose->level){
            if(c->position){if(!services.publish_level(*child,old_pose->level,error))
                return !error.empty()?false:fail(error,"native promoted child level publication failed after unlink");}
            else if(!world.set_native_out_of_world_level(*child,old_pose->level))return fail(error,"native promoted OOW level publication failed after unlink");
        }
    }
    if(!live(*child))return fail(error,"native hierarchy source expired after promotion");
    const auto detached=node(world,root),promoted=node(world,*child);
    const auto* child_body=body(world,*child);
    if(!detached||detached->raw_slot14!=-1||detached->raw_parent26!=0||
       world.native_slot0(root)!=std::optional<Ref>{Ref{}}||!promoted||!child_body||
       !reciprocal(world,*promoted,resolve_parent(world,promoted->raw_parent26),error))
        return fail(error,"native promotion postcondition failed after mutation");
    if(saved->parent){
        if(promoted->raw_slot14!=saved->slot||promoted->raw_parent26!=saved->parent->id)
            return fail(error,"native promoted child lost prior parent/slot publication");
    }else if(promoted->raw_slot14!=-1||promoted->raw_parent26!=0)
        return fail(error,"native promoted child is not unparented");
    const auto final_native=world.native_position(*child);
    if(writes_level&&(!final_native||final_native->level!=old_pose->level))
        return fail(error,"native promoted child level owner did not publish source level");
    return true;
}
bool restore_native_hierarchy_in_world(odh::World& world,Ref root,
    const NativeHierarchyRestoreDestination& destination,const std::function<bool()>& authority,
    std::string& error,const NativeHierarchyRestoreServices& services){
    error.clear();
    const auto live=[&](Ref reference){return authority&&authority()&&body(world,root)&&
        body(world,reference)&&(!destination.parent||body(world,*destination.parent));};
    if(!live(root))return fail(error,"native restore source or receiver unavailable");
    if(destination.position.x<0||destination.position.y<0||destination.position.level<1||
       (destination.parent&&(destination.logical_slot<0||destination.logical_slot>32766)))
        return fail(error,"native restore destination is outside the admitted in-world profile");
    const auto raw_slot=destination.parent?destination.logical_slot+1:-1;
    const auto physical_slot=destination.parent?std::size_t(destination.logical_slot):std::size_t{0};
    if((destination.parent&&!services.physical_mutation)||(!destination.parent&&(!services.floor_head||!services.publish_floor_head)))
        return fail(error,"native restore actual destination owners unavailable");
    const auto occupant=[&]()->std::optional<Ref>{
        if(!live(root))return {};
        if(!destination.parent){
            const auto found=services.floor_head(destination.position);
            if(!live(root)||!found||found->id<0||(!found->id&&found->generation)||
               (found->id&&!body(world,*found)))return {};
            return found;
        }
        odh::VmFrame frame;frame.context.caller=destination.parent->id;
        if(!world.bind_context_live(frame.context))return {};
        const auto count=world.read(frame,3,67);
        if(!count||*count<0||physical_slot>=std::size_t(*count))return {};
        const auto id=world.read(frame,20,static_cast<std::int16_t>(physical_slot));
        if(!id||*id<0)return {};
        if(!*id)return Ref{};
        const auto* e=world.find(*id);if(!e)return {};
        return Ref{e->id,e->generation};
    };
    const auto occupied=occupant();
    if(!occupied)return fail(error,"native restore destination occupancy is unknown");
    // Native's occupied==self branch returns success without even publishing
    // a level. Do not force an unlink, clear a chain, or invent a scalar move.
    if(*occupied==root)return true;
    const auto receiver=capture_native_direct_hierarchy_snapshot(world,root,authority,error);
    if(!receiver)return false;
    const auto receiver_slot0=world.native_slot0(root);
    const auto* root_body=body(world,root);
    if(!root_body||receiver->parent||root_body->attachment||receiver->chain.back().raw_parent26!=0||
       (receiver->slot!=-1&&receiver->slot!=0)||receiver_slot0!=std::optional<Ref>{Ref{}})
        return fail(error,"native restore receiver must be unlinked with known-empty slot zero");
    const bool receiver_in_world=root_body->position.has_value();
    if(destination.parent){
        std::set<Ref> seen;auto ancestor=destination.parent;
        while(ancestor){
            if(*ancestor==root||!seen.insert(*ancestor).second)
                return fail(error,"native restore destination would create a hierarchy cycle");
            const auto proof=capture_native_direct_hierarchy_snapshot(world,*ancestor,authority,error);
            if(!proof)return false;ancestor=proof->parent;
        }
    }
    const auto receiver_pose=world.native_position(root);
    if(!receiver_pose)return fail(error,"native restore receiver scalar level is unknown");
    std::optional<NativeHierarchySnapshot> displaced;
    std::optional<odh::EntityPosition> displaced_pose;
    std::optional<Ref> displaced_slot0;
    bool displaced_in_world=false;
    if(occupied->id){
        displaced=capture_native_direct_hierarchy_snapshot(world,*occupied,authority,error);
        if(!displaced)return false;
        displaced_pose=world.native_position(*occupied);displaced_slot0=world.native_slot0(*occupied);
        if(!displaced_pose||!displaced_slot0)return fail(error,"native displaced body scalar/backing ownership unavailable");
        const auto* displaced_body=body(world,*occupied);if(!displaced_body)return fail(error,"native displaced body expired");
        displaced_in_world=displaced_body->position.has_value();
        if(destination.parent){
            if(displaced->parent!=destination.parent||displaced->slot!=raw_slot)
                return fail(error,"native displaced body differs from the exact physical destination");
        }else if(displaced->parent||displaced->chain.back().raw_parent26!=0||
                 (displaced->slot!=-1&&displaced->slot!=0)||
                 displaced_pose->x/16!=destination.position.x/16||displaced_pose->y/16!=destination.position.y/16||
                 displaced_pose->level!=destination.position.level)
            return fail(error,"native displaced floor head differs from the exact source tile");
        // Authenticate the retained chain, rather than deriving one from nearby
        // objects. Its descendant identities must remain unchanged by32984.
        std::set<Ref> seen{root};auto current=*occupied;
        for(;;){
            if(!seen.insert(current).second)return fail(error,"native displaced slot-zero chain cycles through the receiver");
            if(!live(current))return fail(error,"native displaced chain source expired");
            const auto next=world.native_slot0(current);
            if(!next)return fail(error,"native displaced slot-zero chain backing is unknown");
            if(!next->id){if(next->generation)return fail(error,"native empty slot-zero reference is not canonical");break;}
            const auto proof=capture_native_direct_hierarchy_snapshot(world,*next,authority,error);
            if(!proof||proof->parent!=std::optional<Ref>{current}||proof->slot!=0)
                return fail(error,"native displaced chain lost reciprocal ownership");
            current=*next;
        }
    }
    // Native327d9 changes level after publishing hierarchy. Admission must know
    // the actual current level and guard any represented in-world scalar write.
    if(((receiver_in_world&&receiver_pose->level!=destination.position.level)||
        (occupied->id&&displaced_in_world&&displaced_pose->level!=destination.position.level))&&!services.publish_level)
        return fail(error,"native restore level publication owner unavailable");
    if(!native_hierarchy_snapshot_still_matches(world,*receiver,authority,error)||
       (displaced&&!native_hierarchy_snapshot_still_matches(world,*displaced,authority,error))||
       world.native_slot0(root)!=receiver_slot0||(occupied->id&&world.native_slot0(*occupied)!=displaced_slot0)||
       occupant()!=occupied)
        return fail(error,"native restore destination/source changed before mutation");
    const auto guard=[&](Ref receiver,const std::optional<Ref>& parent,std::size_t slot,
                         const std::function<bool()>& operation){
        struct Call{bool active=true,entered=false,accepted=false,repeated=false;std::function<bool()> operation;};
        const auto call=std::make_shared<Call>();call->operation=operation;
        struct End{std::shared_ptr<Call> call;~End(){call->active=false;call->operation={};}}end{call};
        const bool accepted=services.physical_mutation(receiver,parent,slot,[call]{
            if(!call->active)return false;if(call->entered){call->repeated=true;return false;}
            call->entered=true;return call->accepted=call->operation();
        });
        return accepted&&call->entered&&call->accepted&&!call->repeated&&live(receiver);
    };
    const auto level=[&](Ref receiver,std::int16_t value){
        if(!live(receiver))return false;
        const auto before=world.native_position(receiver);const auto* e=body(world,receiver);
        if(!before||!e)return false;
        if(before->level==value)return true;
        if(e->position){if(!services.publish_level(receiver,value,error))return false;}
        else if(!world.set_native_out_of_world_level(receiver,value))return false;
        if(!live(receiver))return false;
        const auto after=world.native_position(receiver);
        return after&&after->level==value&&after->x==before->x&&after->y==before->y&&after->direction==before->direction;
    };
    // 32984 on the displaced body is deliberately NOT the OOW helper: its
    // own slot-zero child remains attached and is not promoted to the old head.
    if(displaced){
        if(!native_hierarchy_snapshot_still_matches(world,*displaced,authority,error))return false;
        if(destination.parent){
            const std::function<bool()> operation=[&]{return live(*occupied)&&
                occupant()==occupied&&native_hierarchy_snapshot_still_matches(world,*displaced,authority,error)&&
                world.detach(destination.parent->id,physical_slot);};
            if(!guard(*occupied,destination.parent,physical_slot,operation))
                return fail(error,"native displaced physical unlink failed; candidate may be partial");
        }else if(!services.publish_floor_head(destination.position,*occupied,{},error)||!live(*occupied)||occupant()!=std::optional<Ref>{Ref{}}||
                 !native_hierarchy_snapshot_still_matches(world,*displaced,authority,error))
            return fail(error,"native displaced floor unlink failed; candidate may be partial");
        if(!live(*occupied)||!world.declare_object_word(occupied->id,14,-1)||!world.declare_object_word(occupied->id,26,0))
            return fail(error,"native displaced raw hierarchy unlink failed after backing publication");
    }
    if(!live(root)||occupant()!=std::optional<Ref>{Ref{}}||world.native_slot0(root)!=receiver_slot0)
        return fail(error,"native destination changed after displaced unlink");
    if(destination.parent){
        const std::function<bool()> operation=[&]{return live(root)&&occupant()==std::optional<Ref>{Ref{}}&&
            native_hierarchy_snapshot_still_matches(world,*receiver,authority,error)&&world.native_slot0(root)==receiver_slot0&&
            world.attach(destination.parent->id,physical_slot,root.id)&&world.declare_object_word(root.id,14,static_cast<std::int16_t>(raw_slot))&&
            world.declare_object_word(root.id,26,destination.parent->id);};
        if(!guard(root,destination.parent,physical_slot,operation))
            return fail(error,"native receiver physical insertion failed after displaced unlink");
    }else if(!services.publish_floor_head(destination.position,{},root,error)||!live(root)||occupant()!=std::optional<Ref>{root}||
             !native_hierarchy_snapshot_still_matches(world,*receiver,authority,error)||
             !world.declare_object_word(root.id,14,-1)||!world.declare_object_word(root.id,26,0))
        return fail(error,"native receiver floor insertion failed after displaced unlink");
    if(!level(root,destination.position.level))return fail(error,"native receiver level publication failed after insertion");
    if(displaced){
        if(!live(*occupied)||world.native_slot0(root)!=std::optional<Ref>{Ref{}}||
           !world.declare_object_word(occupied->id,14,0)||!world.declare_object_word(occupied->id,26,root.id)||
           !world.publish_native_slot0(root,*occupied)||!level(*occupied,destination.position.level))
            return fail(error,"native displaced body insertion under raw slot zero failed");
    }
    if(!live(root)||occupant()!=std::optional<Ref>{root})return fail(error,"native restore destination publication expired");
    const auto final=node(world,root);const auto* final_body=body(world,root);
    const auto final_pose=world.native_position(root);
    const bool same_root_xyz=final_body&&final_body->position.has_value()==receiver->root_position.has_value()&&
        (!receiver->root_position||(final_body->position->x==receiver->root_position->x&&
            final_body->position->y==receiver->root_position->y&&final_body->position->direction==receiver->root_position->direction));
    if(!final||final->raw_slot14!=raw_slot||final->raw_parent26!=(destination.parent?destination.parent->id:0)||
       !reciprocal(world,*final,destination.parent,error)||!same_root_xyz||!final_pose||final_pose->level!=destination.position.level||
       world.native_slot0(root)!=std::optional<Ref>{occupied->id?*occupied:Ref{}})
        return fail(error,"native receiver hierarchy/XYZ postcondition failed");
    if(displaced){
        const auto final_displaced=node(world,*occupied);const auto after=world.native_position(*occupied);
        if(!final_displaced||final_displaced->raw_slot14!=0||final_displaced->raw_parent26!=root.id||
           !reciprocal(world,*final_displaced,std::optional<Ref>{root},error)||
           world.native_slot0(*occupied)!=displaced_slot0||!after||after->x!=displaced_pose->x||after->y!=displaced_pose->y||
           after->direction!=displaced_pose->direction||after->level!=destination.position.level)
            return fail(error,"native displaced chain/XYZ postcondition failed");
    }
    return true;
}
} // namespace odh_host
