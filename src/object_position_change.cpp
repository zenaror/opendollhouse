// SPDX-License-Identifier: MPL-2.0
// Native source port: FreeSO VMMultitileGroup.cs159-324,
// VMGameObject.cs320-442, VMEntity.cs1472-1513, pinned 7e76c196.
#include "odh/object_position_change.hpp"
#include <limits>
#include <set>
#include <algorithm>
#include <memory>
#include <map>
namespace odh {
namespace {
struct PositionLifecycle {
    World& world;const ObjectPositionChangeServices& services;
    ObjectPositionIdentity root;const std::function<bool()>& authority;
    struct Link {ObjectPositionIdentity parent,child;std::size_t slot;};
    std::vector<Link> links;
    std::string* diagnostic=nullptr;
    bool fail(const char* stage,ObjectPositionIdentity id)const {
        if(diagnostic&&diagnostic->empty())*diagnostic=std::string(stage)+" root="+std::to_string(root.id)+
            " root_generation="+std::to_string(root.generation)+" entity="+std::to_string(id.id)+
            " generation="+std::to_string(id.generation);
        return false;
    }
    const Entity* entity(ObjectPositionIdentity id)const {
        if(!authority||!authority()){fail("authority",id);return nullptr;}
        const auto* e=world.find(id.id);
        if(!e||e->generation!=id.generation||e->ui_ghost){fail("live identity",id);return nullptr;}
        return e;
    }
    bool admit(ObjectPositionIdentity id)const {
        const auto* e=entity(id);if(!e)return false;
        if(e->kind==EntityKind::avatar)return (e->entry_points&&e->object_words.count(29))||fail("avatar admission",id);
        if(e->kind!=EntityKind::object||!services.admit_single_ordinary)return fail("ordinary admission unavailable",id);
        const auto yes=services.admit_single_ordinary(id);
        return (yes&&*yes&&entity(id))||fail(yes?"ordinary admission rejected":"ordinary admission unknown",id);
    }
    bool capture(){
        if(!services.unregister_position||!services.register_position||!services.room_at)return fail("lifecycle services unavailable",root);
        if(!admit(root))return false;
        std::set<std::int16_t> seen{root.id};
        std::function<bool(ObjectPositionIdentity)> visit=[&](auto id){
            const auto* e=entity(id);if(!e)return false;const auto slots=e->slots;
            for(std::size_t slot=0;slot<slots.size();++slot)if(slots[slot]){
                const auto* child=world.find(slots[slot]);if(!child||!child->attachment||
                   child->attachment->container!=id.id||child->attachment->slot!=slot||
                   !seen.insert(child->id).second||seen.size()>4096)return fail("contained tree identity/reciprocity/budget",id);
                const ObjectPositionIdentity next{child->id,child->generation};
                if(!admit(next))return false;
                links.push_back({id,next,slot});if(!visit(next))return false;
            }return true;};
        return visit(root)&&matches();
    }
    bool matches()const {
        std::set<std::int16_t> seen{root.id};if(!entity(root))return false;
        for(const auto& link:links){const auto* p=entity(link.parent);const auto* c=entity(link.child);
            if(!p||!c||link.slot>=p->slots.size()||p->slots[link.slot]!=c->id||!c->attachment||
               c->attachment->container!=p->id||c->attachment->slot!=link.slot||!seen.insert(c->id).second)return fail("contained tree changed",link.child);}
        for(const auto id:seen){const auto* e=world.find(id);for(std::size_t slot=0;slot<e->slots.size();++slot)if(e->slots[slot]&&
            std::none_of(links.begin(),links.end(),[&](const auto& link){return link.parent.id==id&&link.slot==slot&&link.child.id==e->slots[slot];}))return fail("contained tree gained child",{id,e->generation});}
        return true;
    }
    bool pre(bool room_change){
        if(!matches())return false;
        const auto* e=entity(root);const auto old=e->attachment;
        if(!services.unregister_position(root,room_change))return fail("Pre unregister",root);
        if(!matches())return false;
        e=entity(root);
        if(old){if(!e->attachment||e->attachment->container!=old->container||e->attachment->slot!=old->slot)return false;
            const auto parent=world.attachment_parent({root.id,root.generation});
            if(!parent)return fail("Pre container identity unavailable",root);
            const bool detached=services.detach_container?
                services.detach_container(root,{parent->id,parent->generation},old->slot):
                (world.is_live_reference(*parent)&&world.detach(parent->id,old->slot));
            return detached&&matches();}
        if(!e->position||e->kind==EntityKind::avatar)return true;
        const auto exclusive=e->object_words.find(63);if(exclusive==e->object_words.end()||!services.wall_use||
           !services.wall_use(root,false,(exclusive->second&2)!=0)||!matches())return false;
        e=entity(root);const auto category=e->object_words.find(59);if(category==e->object_words.end())return false;
        if(category->second==8&&(!e->position||!services.object_supported||!services.object_supported(root,*e->position,false)||!matches()))return false;
        return true;
    }
    std::optional<std::uint16_t> set_room(ObjectPositionIdentity id){
        const auto* e=entity(id);if(!e)return {};
        const auto room=e->position?services.room_at(*e->position):std::optional<std::uint16_t>{0};
        if(!room||*room>32768U||!e->object_words.count(29)){fail("Room resolution/word29",id);return {};}
        VmFrame frame;frame.context.caller=id.id;
        if(!world.write(frame,3,29,static_cast<std::int16_t>(static_cast<std::int32_t>(*room)-1))){fail("Room29 publication",id);return {};}
        if(!matches())return {};
        return room;
    }
    bool post(ObjectPositionIdentity id,bool room_change,bool suppress_root_entry9=false){
        if(!matches())return false;
        const auto* e=entity(id);const bool avatar=e->kind==EntityKind::avatar;
        // Avatar sets Room before propagating contained items; objects after.
        std::optional<std::uint16_t> room;
        if(avatar){room=set_room(id);if(!room)return false;}
        for(const auto& link:links)if(link.parent.id==id.id){
            if(!services.unregister_position(link.child,room_change))return fail("child unregister",link.child);
            if(!matches())return false;
            if(!world.refresh_contained_position(link.child.id))return fail("child pose refresh",link.child);
            if(!post(link.child,room_change))return false;}
        if(!avatar)room=set_room(id);
        if(!room)return false;
        if(!services.register_position(id,*room,room_change))return fail("Post register",id);
        if(!matches())return false;
        e=entity(id);if(e->attachment||!e->position)return true;
        if(!avatar){
            const auto exclusive=e->object_words.find(63);if(exclusive==e->object_words.end()||!services.wall_use||
               !services.wall_use(id,true,(exclusive->second&2)!=0)||!matches())return fail("Post wall use",id);
            e=entity(id);const auto category=e->object_words.find(59);if(category==e->object_words.end())return fail("Post category missing",id);
            if(category->second==8&&(!e->position||!services.object_supported||!services.object_supported(id,*e->position,true)||!matches()))return fail("Post support",id);
            e=entity(id);if(!e->entry_points||e->entry_points->size()<=9)return fail("Post entry points missing",id);
            if(e->entry_points->at(8).action&&(!services.dynamic_multitile||!services.dynamic_multitile(id)||!matches()))return fail("Post EP8",id);
        }
        if(suppress_root_entry9)return matches();
        e=entity(id);if(!e->entry_points||e->entry_points->size()<=9)return fail("Post EP9 table missing",id);
        return !e->entry_points->at(9).action||(services.entry_point9&&services.entry_point9(id)&&matches())||fail("Post EP9",id);
    }
};
// VM.Load is a post-position replay, not ChangePosition. In particular its
// general loaded admission does not inherit ordinary singleton move limits,
// and noEntryPoint suppresses only VMEntity's final placement entry point.
struct LoadedPositionLifecycle {
    World& world;const ObjectPositionChangeServices& services;
    const std::function<bool()>& authority;
    std::set<std::int16_t> active;
    std::size_t calls=0;
    const Entity* entity(ObjectPositionIdentity id)const {
        if(!authority||!authority())return nullptr;
        const auto* e=world.find(id.id);return e&&e->generation==id.generation?e:nullptr;
    }
    bool same_child(ObjectPositionIdentity parent,ObjectPositionIdentity child,std::size_t slot)const {
        const auto* p=entity(parent);const auto* c=entity(child);
        return p&&c&&slot<p->slots.size()&&p->slots[slot]==child.id&&c->attachment&&
            c->attachment->container==parent.id&&c->attachment->slot==slot;
    }
    bool post(ObjectPositionIdentity id){
        if(++calls>4096||!active.insert(id.id).second)return false;
        struct Pop {std::set<std::int16_t>& active;std::int16_t id;~Pop(){active.erase(id);}} pop{active,id.id};
        const auto* e=entity(id);
        if(!e||!services.admit_loaded_entity)return false;
        const auto admitted=services.admit_loaded_entity(id);
        e=entity(id);if(!admitted||!*admitted||!e)return false;
        const bool avatar=e->kind==EntityKind::avatar;
        if(!avatar&&e->kind!=EntityKind::object)return false;
        // Avatar returns immediately for GhostImage; GameObject returns only
        // after propagating its children. Keep these distinct source branches.
        if(avatar&&e->ui_ghost)return true;
        std::optional<std::uint16_t> room;
        const auto set_room=[&]()->bool{
            const auto* current=entity(id);if(!current||!services.room_at||!current->object_words.count(29))return false;
            room=current->position?services.room_at(*current->position):std::optional<std::uint16_t>{0};
            if(!room||*room>32768U||!entity(id))return false;
            VmFrame frame;frame.context.caller=id.id;
            return world.write(frame,3,29,static_cast<std::int16_t>(static_cast<std::int32_t>(*room)-1))&&entity(id);
        };
        if(avatar&&!set_room())return false;
        // Do not freeze the whole tree or impose a global visited-once rule.
        // The reference reads Contained[i] on each loop iteration. Mutations
        // made by earlier actual callbacks affect the later child traversal.
        for(std::size_t slot=0;;++slot){
            e=entity(id);if(!e)return false;if(slot>=e->slots.size())break;
            const auto child_id=e->slots[slot];if(!child_id)continue;
            const auto* child=world.find(child_id);if(!child)return false;
            const ObjectPositionIdentity next{child_id,child->generation};
            if(!same_child(id,next,slot))return false;
            if((avatar||!child->ui_ghost)&&(!services.unregister_position||
               !services.unregister_position(next,true)||!same_child(id,next,slot)))return false;
            if(!world.refresh_contained_position(child_id)||!same_child(id,next,slot)||!post(next))return false;
        }
        e=entity(id);if(!e)return false;if(!avatar&&e->ui_ghost)return true;
        if(!avatar&&!set_room())return false;
        if(!room||!services.register_position||!services.register_position(id,*room,true)||!entity(id))return false;
        e=entity(id);if(e->attachment||!e->position)return true;
        if(!avatar){
            const auto flags=e->object_words.find(40);if(flags==e->object_words.end())return false;
            if((static_cast<std::uint16_t>(flags->second)&0xc000U)&&
               (!services.architectural_wall_style||!services.architectural_wall_style(id)||!entity(id)))return false;
            e=entity(id);const auto exclusive=e->object_words.find(63);
            if(exclusive==e->object_words.end()||!services.wall_use||
               !services.wall_use(id,true,(exclusive->second&2)!=0)||!entity(id))return false;
            e=entity(id);const auto category=e->object_words.find(59);if(category==e->object_words.end())return false;
            if(category->second==8&&(!e->position||!services.object_supported||
               !services.object_supported(id,*e->position,true)||!entity(id)))return false;
            e=entity(id);if(!e->entry_points||e->entry_points->size()<=8)return false;
            if(e->entry_points->at(8).action&&(!services.dynamic_multitile||
               !services.dynamic_multitile(id)||!entity(id)))return false;
        }
        return entity(id)!=nullptr; // noEntryPoint=true suppresses only base EP9.
    }
};
}
bool ObjectPositionChange::pre_position_change(ObjectPositionIdentity id,bool change,const std::function<bool()>& authority){
    diagnostic_.clear();if(executing_){diagnostic_="lifecycle reentry";return false;}
    executing_=true;struct Reset{bool& v;~Reset(){v=false;}} reset{executing_};
    try{PositionLifecycle lifecycle{world_,services_,id,authority,{},&diagnostic_};return lifecycle.capture()&&lifecycle.pre(change);}
    catch(const std::exception& error){diagnostic_="Pre exception: "+std::string(error.what());return false;}
    catch(...){diagnostic_="Pre unknown exception";return false;}
}
bool ObjectPositionChange::position_change(ObjectPositionIdentity id,bool change,const std::function<bool()>& authority){
    diagnostic_.clear();if(executing_){diagnostic_="lifecycle reentry";return false;}
    executing_=true;struct Reset{bool& v;~Reset(){v=false;}} reset{executing_};
    try{PositionLifecycle lifecycle{world_,services_,id,authority,{},&diagnostic_};return lifecycle.capture()&&lifecycle.post(id,change);}
    catch(const std::exception& error){diagnostic_="Post exception: "+std::string(error.what());return false;}
    catch(...){diagnostic_="Post unknown exception";return false;}
}
bool ObjectPositionChange::loaded_position_change(ObjectPositionIdentity id,const std::function<bool()>& authority){
    if(executing_)return false;
    executing_=true;struct Reset{bool& v;~Reset(){v=false;}} reset{executing_};
    try{
        LoadedPositionLifecycle lifecycle{world_,services_,authority,{},0};
        const auto* root=lifecycle.entity(id);
        return root&&!root->attachment&&lifecycle.post(id);
    }catch(...){return false;}
}
ObjectPositionChangeResult ObjectPositionChange::apply(const ObjectPositionValidRequest& request){return services_.group?apply_group(request,true):apply_single(request,true);}
ObjectPositionChangeResult ObjectPositionChange::apply_existing(const ObjectPositionValidRequest& request){
    if(executing_||!request.source_still_matches||!request.source_still_matches()||request.direction>7||request.flags>15||
       request.candidate.x<INT16_MIN||request.candidate.x>INT16_MAX||request.candidate.y<INT16_MIN||request.candidate.y>INT16_MAX||
       request.candidate.level<INT8_MIN||request.candidate.level>INT8_MAX){ObjectPositionChangeResult out;out.diagnostic="SetPosition invocation authority or bounds invalid";return out;}
    std::vector<ObjectPositionIdentity> previous;
    if(services_.split_dynamic_group){
        executing_=true;struct EndSplit{bool&active;~EndSplit(){active=false;}}end{executing_};
        const auto split=services_.split_dynamic_group(request.target);
        if(!split){ObjectPositionChangeResult out;out.diagnostic="dynamic SetPosition separation unavailable";return out;}
        previous=*split;
    }
    auto result=apply_existing_group(request);
    for(const auto id:previous)if(!services_.update_dynamic_group_flags||!services_.update_dynamic_group_flags(id)){
        result.status=ObjectPositionChangeStatus::unsupported;result.diagnostic="old dynamic group flags did not complete after SetPosition";break;
    }
    return result;
}
ObjectPositionChangeResult ObjectPositionChange::apply_existing_group(const ObjectPositionValidRequest& request){return services_.group?apply_group(request,false):apply_single(request,false);}
ObjectPositionChangeResult ObjectPositionChange::apply_group(const ObjectPositionValidRequest& request,bool require_new){
    ObjectPositionChangeResult result;
    const auto fail=[&](const char* reason){result.diagnostic=reason;return result;};
    if(executing_||!request.source_still_matches||request.direction>7||request.flags>15||
       request.candidate.x<INT16_MIN||request.candidate.x>INT16_MAX||request.candidate.y<INT16_MIN||request.candidate.y>INT16_MAX||
       request.candidate.level<INT8_MIN||request.candidate.level>INT8_MAX)
        return fail("group placement requires bounded real source authority");
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
    if(!services_.group||!services_.admit_group_member||!services_.story_count||!services_.room_at||
       !services_.unregister_position||!services_.register_position)return fail("group lifecycle capabilities are unavailable");
    try{
        if(!request.source_still_matches())return fail("group source authority expired");
        const auto selected=services_.group(request.target);
        if(selected&&selected->native_head)return apply_native_group(request,require_new);
        if(!selected||(!selected->definition_guid&&!services_.authenticate_imported_group&&!services_.authenticate_live_group)||selected->base_id<=0||selected->members.empty()||selected->members.size()>64)
            return fail("complete bounded physical group is unavailable");
        const ObjectGroup group=*selected;bool allow_association_transition=false;
        const auto same_group=[&](const ObjectGroup& other){
            if(other.kind!=group.kind||other.definition_guid!=group.definition_guid||other.multitile!=group.multitile||other.base_id!=group.base_id||
               other.members.size()!=group.members.size())return false;
            for(std::size_t n=0;n<group.members.size();++n){const auto& a=group.members[n];const auto& b=other.members[n];
                if(a.id!=b.id||a.generation!=b.generation||a.offset_x!=b.offset_x||a.offset_y!=b.offset_y||a.offset_level!=b.offset_level)return false;}
            return true;
        };
        const auto valid=[&]{
            if(!request.source_still_matches())return false;
            const auto current=services_.group(request.target);if(!current||!request.source_still_matches())return false;
            if(!same_group(*current)&&(!allow_association_transition||current->kind!=ObjectGroupKind::live_association||
                !services_.authenticate_live_group||!services_.authenticate_live_group(*current)))return false;
            if(group.kind==ObjectGroupKind::live_association){
                if(!services_.authenticate_live_group||!services_.authenticate_live_group(*current)||!request.source_still_matches())return false;
            }else if(!group.definition_guid&&(!services_.authenticate_imported_group||
               !services_.authenticate_imported_group(group)||!request.source_still_matches()))return false;
            for(const auto& member:group.members){const auto* e=world_.find(member.id);
                if(!e||e->generation!=member.generation||e->kind!=EntityKind::object||e->dead||e->ui_ghost||!e->definition||
                   (group.definition_guid&&(group.multitile?e->definition->master_guid!=std::optional<std::uint32_t>{group.definition_guid}:
                    e->definition->guid!=group.definition_guid||e->definition->master_guid.has_value())))return false;}
            return true;
        };
        std::set<std::int16_t> physical_ids;bool target_present=false;const ObjectGroupMember* base=nullptr;
        for(const auto& member:group.members){if(member.id<=0||!member.generation||!physical_ids.insert(member.id).second)
                return fail("group member identity is invalid or duplicated");
            if(member.id==request.target.id&&member.generation==request.target.generation)target_present=true;
            if(member.id==group.base_id)base=&member;
        }
        if(!base||!target_present||(group.kind==ObjectGroupKind::constructed&&!group.multitile&&group.members.size()!=1)||!valid())
            return fail("group resource/base/target proof differs from World");
        ObjectPositionChangeServices local_services=services_;
        local_services.admit_single_ordinary=[&](ObjectPositionIdentity id)->std::optional<bool>{
            const auto member=std::find_if(group.members.begin(),group.members.end(),[&](const auto& m){return m.id==id.id&&m.generation==id.generation;});
            return member!=group.members.end()?services_.admit_group_member(id):
                services_.admit_single_ordinary?services_.admit_single_ordinary(id):std::nullopt;
        };
        const std::function<bool()> authority=valid;
        struct MemberState {
            ObjectPositionIdentity identity,container;
            std::optional<EntityPosition> original;
            std::optional<Attachment> attachment;
            EntityPosition candidate{};bool detached=false,room_change=false;
            ObjectPositionValidResult placement;
            std::unique_ptr<PositionLifecycle> lifecycle;
        };
        std::vector<MemberState> states;states.reserve(group.members.size());
        std::set<std::int16_t> tree_ids=physical_ids;
        for(const auto& member:group.members){
            const auto* e=world_.find(member.id);
            if(require_new&&(e->position||e->attachment))return fail("new physical group has a positioned or contained member");
            MemberState state;state.identity={member.id,member.generation};state.original=e->position;state.attachment=e->attachment;
            state.lifecycle=std::make_unique<PositionLifecycle>(PositionLifecycle{world_,local_services,state.identity,authority,{}});
            if(!state.lifecycle->capture()||!valid())return fail("group member or contained tree admission failed");
            for(const auto& link:state.lifecycle->links)if(!tree_ids.insert(link.child.id).second||tree_ids.size()>4096)
                return fail("group containment trees overlap or exceed the aggregate bound");
            if(state.attachment){const auto parent=world_.attachment_parent({member.id,member.generation});
                const auto* container=parent?world_.resolve_reference(*parent):nullptr;
                if(!container||physical_ids.count(container->id)||!services_.restore_container||
                   state.attachment->slot>=container->slots.size()||container->slots[state.attachment->slot]!=member.id)
                    return fail("group member original container restoration is unavailable");
                state.container={container->id,container->generation};}
            states.push_back(std::move(state));
        }
        const auto unchanged=[&](const MemberState& state){const auto* e=world_.find(state.identity.id);
            if(!e||e->generation!=state.identity.generation||!(e->position==state.original))return false;
            if(state.detached)return !e->attachment;
            return bool(e->attachment)==bool(state.attachment)&&(!e->attachment||
                (e->attachment->container==state.attachment->container&&e->attachment->slot==state.attachment->slot));};
        const auto all_unchanged=[&]{return std::all_of(states.begin(),states.end(),unchanged);};
        const auto phase=[&](MemberState& state,ObjectPositionChangePhase stage,const std::function<bool()>& operation){
            if(!services_.lifecycle_phase)return operation()&&valid();
            struct Lease{bool active=true,completed=false;unsigned calls=0;};auto lease=std::make_shared<Lease>();
            struct Expire{std::shared_ptr<Lease> p;~Expire(){p->active=false;}} expire{lease};
            const std::function<bool()> once=[lease,valid,operation]{if(!lease->active||++lease->calls!=1||!valid())return false;
                lease->completed=operation();return lease->completed;};
            const bool accepted=services_.lifecycle_phase(state.identity,stage,once);lease->active=false;
            return accepted&&lease->calls==1&&lease->completed&&valid();
        };
        const auto stories=services_.story_count();if(!stories||*stories<1||!valid()||!all_unchanged())return fail("group story/source state is unavailable");
        if(request.candidate.level>*stories){result.status=ObjectPositionChangeStatus::blocked;
            result.placement.status=ObjectPositionValidStatus::blocked;result.placement.placement_error=18;return result;}
        const bool oow=request.candidate.x==-32768&&request.candidate.y==-32768&&request.candidate.level==1;
        const auto short_word=[](std::int32_t value){const auto bits=static_cast<std::uint16_t>(value);
            return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);};
        const auto level_word=[](std::int32_t value){const auto bits=static_cast<std::uint8_t>(value);
            return static_cast<std::int16_t>(bits<128?static_cast<std::int16_t>(bits):static_cast<std::int16_t>(bits)-256);};
        for(std::size_t n=0;n<states.size();++n){auto& state=states[n];const auto& member=group.members[n];
            auto dx=static_cast<std::int32_t>(member.offset_x)-base->offset_x,dy=static_cast<std::int32_t>(member.offset_y)-base->offset_y;
            const auto original_dx=dx;
            switch(request.direction){case 2:dx=-dy;dy=original_dx;break;case 4:dx=-dx;dy=-dy;break;case 6:dx=dy;dy=-original_dx;break;default:break;}
            state.candidate=oow?EntityPosition{-32768,-32768,1,request.direction}:
                EntityPosition{short_word(request.candidate.x+dx),short_word(request.candidate.y+dy),
                    level_word(request.candidate.level+member.offset_level),request.direction};
        }
        // Source checks in member order, immediately after each member's Pre.
        // No scalar position is published until EVERY check has succeeded.
        for(std::size_t n=0;n<states.size();++n){auto& state=states[n];
            const auto old_room=state.original?services_.room_at(*state.original):std::optional<std::uint16_t>{0};
            const auto next_room=oow?std::optional<std::uint16_t>{0}:services_.room_at(state.candidate);
            if(!old_room||!next_room||!valid()||!all_unchanged())return fail("group member source room/pose is unavailable");
            state.room_change=oow||state.attachment.has_value()||*old_room!=*next_room;
            if(!state.lifecycle->admit(state.identity)||!valid()||!all_unchanged())
                return fail("earlier check changed the next member's admitted lifecycle profile");
            result.effects_started=true;
            if(!phase(state,ObjectPositionChangePhase::pre,[&]{return state.lifecycle->pre(state.room_change);}))
                return fail("group member PrePositionChange is incomplete");
            state.detached=state.attachment.has_value();
            if(!valid()||!all_unchanged())return fail("group Pre changed a captured member pose");
            if(oow){state.placement.status=ObjectPositionValidStatus::success;state.placement.placement_error=-1;}
            else state.placement=placement_.evaluate({state.identity,state.candidate,request.direction,request.flags,authority});
            if(!valid()||!all_unchanged())return fail("group check changed membership, identity or captured pose");
            if(state.placement.status==ObjectPositionValidStatus::unsupported)return fail("group member PositionValid is incomplete");
            if(!state.lifecycle->admit(state.identity)||!state.lifecycle->matches()||!valid()||!all_unchanged())return fail("group check changed admitted member profile");
            if(state.placement.status==ObjectPositionValidStatus::blocked){
                result.placement=state.placement;
                // Restore only the already-Pre'd prefix. Script effects and
                // later untouched members retain their actual source state.
                for(std::size_t j=0;j<=n;++j){auto& old=states[j];
                    if(old.attachment){if(!services_.restore_container(old.identity,old.container,old.attachment->slot)||!valid())
                            return fail("failed group placement cannot restore original member slot");
                        old.detached=false;
                        const auto* parent=world_.resolve_reference({old.container.id,old.container.generation});const auto* child=world_.find(old.identity.id);
                        if(!parent||!child||!child->attachment||child->attachment->container!=parent->id||
                           child->attachment->slot!=old.attachment->slot||old.attachment->slot>=parent->slots.size()||parent->slots[old.attachment->slot]!=child->id)
                            return fail("group old-slot restoration is not reciprocal");}
                    allow_association_transition=true;
                    if(!phase(old,ObjectPositionChangePhase::post,[&]{return old.lifecycle->post(old.identity,old.room_change);}))
                        return fail("failed group placement prefix PositionChange is incomplete");
                }
                result.status=ObjectPositionChangeStatus::blocked;return result;
            }
            const auto* checked=world_.find(state.identity.id);
            if(state.placement.support_slot&&!checked->static_footprint&&!state.room_change){
                if(!services_.unregister_support_footprint||!services_.unregister_support_footprint(state.identity)||
                   !valid()||!all_unchanged())return fail("same-room dynamic support footprint unregister is unavailable");
            }
        }
        // A later part's EP5 may have changed an earlier part's runtime flags.
        // Re-authenticate the entire physical cohort before any scalar publish.
        for(auto& state:states)if(!state.lifecycle->admit(state.identity)||!state.lifecycle->matches()||
            !valid()||!all_unchanged())return fail("group checks changed the admitted physical cohort");
        for(auto& state:states){const auto* e=world_.find(state.identity.id);
            const auto notches=e->object_words.find(23),heading=e->object_words.find(1);
            if(notches==e->object_words.end()||heading==e->object_words.end())return fail("group member direction words are unavailable");
            auto direction=static_cast<std::int16_t>(request.direction);if(notches->second>1)direction-=direction%notches->second;
            state.candidate.direction=static_cast<std::uint8_t>(direction);
            const auto destination=oow?std::optional<EntityPosition>{}:std::optional<EntityPosition>{state.candidate};
            bool published=false;
            if(services_.publish_position)published=services_.publish_position(state.identity,destination,state.candidate.direction);
            else {VmFrame frame;frame.context.caller=state.identity.id;
                published=world_.write(frame,3,1,direction)&&valid()&&world_.place_individual_on_floor(state.identity.id,destination,state.candidate.direction);}
            if(!published)return fail("group SetIndivPosition failed after completed checks");
            result.position_published=true;if(!valid())return fail("group publication changed source authority");
            e=world_.find(state.identity.id);
            if(!e||e->generation!=state.identity.generation||!(e->position==destination)||e->attachment||e->object_words.at(1)!=direction)
                return fail("group scalar publisher did not publish exact member pose");
            if(state.placement.support_slot){
                if(!services_.place_in_slot0||!services_.place_in_slot0(state.identity,state.placement.object)||!valid())
                    return fail("group member support-slot publication is incomplete");
                const auto* support=world_.find(state.placement.object.id);e=world_.find(state.identity.id);
                if(!support||support->generation!=state.placement.object.generation||!e->attachment||
                   e->attachment->container!=support->id||e->attachment->slot!=0||support->slots.empty()||support->slots[0]!=e->id||
                   !e->position||!support->position||e->position->x!=support->position->x||
                   e->position->y!=support->position->y||e->position->level!=support->position->level)
                    return fail("group member support-slot publication is not reciprocal");
            }
        }
        allow_association_transition=true;
        for(auto& state:states)if(!phase(state,ObjectPositionChangePhase::post,[&]{return state.lifecycle->post(state.identity,state.room_change);}))
            return fail("group recursive PositionChange is incomplete");
        result.placement.status=ObjectPositionValidStatus::success;result.placement.placement_error=-1;
        result.status=ObjectPositionChangeStatus::success;return result;
    }catch(...){return fail("group position callback failed; completed source effects retained");}
}
ObjectPositionChangeResult ObjectPositionChange::apply_native_group(const ObjectPositionValidRequest& request,bool require_new){
    return run_native_stages(request,require_new,false,false,[&](NativeObjectPositionStages& stages){
        if(!stages.pre())return ObjectPositionChangeStatus::unsupported;
        const auto rotation=stages.rotate_to(request.direction);if(!rotation)return ObjectPositionChangeStatus::unsupported;
        const auto check=stages.check54();if(check.status==ObjectPositionValidStatus::unsupported)return ObjectPositionChangeStatus::unsupported;
        if(check.status==ObjectPositionValidStatus::blocked){
            if(!stages.rollback(*rotation)||!stages.restore_after_refusal())return ObjectPositionChangeStatus::unsupported;
            return ObjectPositionChangeStatus::blocked;
        }
        return stages.publish58()?ObjectPositionChangeStatus::success:ObjectPositionChangeStatus::unsupported;
    });
}
ObjectPositionChangeResult ObjectPositionChange::with_native_position_stages(const ObjectPositionValidRequest& request,
        bool original_class,bool captured_outside,const std::function<ObjectPositionChangeStatus(NativeObjectPositionStages&)>& driver){
    ObjectPositionChangeResult rejected;
    if(executing_||!driver||!request.source_still_matches||request.direction>7||request.flags>15||
       request.candidate.x<INT16_MIN||request.candidate.x>INT16_MAX||request.candidate.y<INT16_MIN||request.candidate.y>INT16_MAX||
       request.candidate.level<INT8_MIN||request.candidate.level>INT8_MAX){rejected.diagnostic="native stage invocation authority/bounds invalid";return rejected;}
    executing_=true;struct End{bool& active;~End(){active=false;}}end{executing_};
    if(!services_.group||!services_.admit_group_member||!services_.story_count||!services_.room_at||
       !services_.unregister_position||!services_.register_position){rejected.diagnostic="native stage actual positioning services unavailable";return rejected;}
    return run_native_stages(request,false,original_class,captured_outside,driver);
}
ObjectPositionChangeResult ObjectPositionChange::run_native_stages(const ObjectPositionValidRequest& request,bool require_new,
        bool original_class,bool captured_outside,const std::function<ObjectPositionChangeStatus(NativeObjectPositionStages&)>& driver){
    ObjectPositionChangeResult result;
    const auto fail=[&](const char* why){result.diagnostic=why;return result;};
    const WorldEntityReference selected{request.target.id,request.target.generation};
    if(!services_.begin_native_rotation||!services_.rollback_native_rotation||!services_.native_current_offset||
       !services_.native_group_head||!services_.native_group_next)return fail("native placement requires its actual group rotation owner");
    const auto valid=[&]{
        if(!request.source_still_matches()||!world_.is_live_reference(selected))return false;
        const auto group=services_.group(request.target);
        if(!group||!group->native_head||group->members.empty()||group->members.size()>64||
           group->native_head->id!=group->members.front().id||group->native_head->generation!=group->members.front().generation)return false;
        const auto actual_head=services_.native_group_head(selected);
        if(actual_head.status!=NativeObjectGroupLinkStatus::member||actual_head.reference!=*group->native_head)return false;
        if(group->kind==ObjectGroupKind::live_association){if(!services_.authenticate_live_group||!services_.authenticate_live_group(*group))return false;}
        else if(!group->definition_guid&&(!services_.authenticate_imported_group||!services_.authenticate_imported_group(*group)))return false;
        for(std::size_t i=0;i<group->members.size();++i){const auto& member=group->members[i];
            const WorldEntityReference ref{member.id,member.generation};const auto* body=world_.resolve_reference(ref);
            if(!body||!world_.is_live_reference(ref)||body->kind!=EntityKind::object||body->dead||body->ui_ghost||!member.native||
               member.native->next!=(i+1<group->members.size()?WorldEntityReference{group->members[i+1].id,group->members[i+1].generation}:WorldEntityReference{}))return false;
            const auto offset=services_.native_current_offset(ref);
            if(!offset||offset->x!=member.native->current.x||offset->y!=member.native->current.y||offset->level!=member.native->current.level)return false;
        }
        return request.source_still_matches();
    };
    const std::function<bool()> authority=valid;
    auto local=services_;local.admit_single_ordinary=[&](auto id)->std::optional<bool>{
        const auto group=services_.group(request.target);
        if(group&&std::any_of(group->members.begin(),group->members.end(),[&](const auto& m){return m.id==id.id&&m.generation==id.generation;}))
            return services_.admit_group_member(id);
        return services_.admit_single_ordinary?services_.admit_single_ordinary(id):std::nullopt;
    };
    struct State {
        ObjectPositionIdentity id,container;std::optional<EntityPosition> original;std::optional<Attachment> attachment;
        std::unique_ptr<PositionLifecycle> lifecycle;ObjectPositionValidResult placement;bool detached=false,room_change=false,checked=false;
    };
    std::map<WorldEntityReference,State> states;
    const bool oow=request.candidate.x==-32768&&request.candidate.y==-32768&&request.candidate.level==1;
    const auto state_for=[&](WorldEntityReference ref,bool post_publication=false)->State*{
        auto found=states.find(ref);if(found!=states.end())return &found->second;
        if(states.size()>=64||!valid())return nullptr;const auto* body=world_.resolve_reference(ref);
        if(!body||!world_.is_live_reference(ref)||!body->object_words.count(1))return nullptr;
        State state;state.id={ref.id,ref.generation};state.original=body->position;state.attachment=body->attachment;
        if(require_new&&!post_publication&&(state.original||state.attachment))return nullptr;
        if(state.attachment){const auto parent=world_.attachment_parent(ref);if(!parent||!services_.restore_container)return nullptr;state.container={parent->id,parent->generation};}
        state.lifecycle=std::make_unique<PositionLifecycle>(PositionLifecycle{world_,local,state.id,authority,{}});
        if(!state.lifecycle->capture())return nullptr;return &states.emplace(ref,std::move(state)).first->second;
    };
    const auto same_xyz=[&](const State& state){const auto* body=world_.resolve_reference({state.id.id,state.id.generation});
        return body&&body->position.has_value()==state.original.has_value()&&(!body->position||
            (body->position->x==state.original->x&&body->position->y==state.original->y&&body->position->level==state.original->level));};
    const auto phase=[&](State& state,ObjectPositionChangePhase stage,const std::function<bool()>& operation){
        if(!services_.lifecycle_phase)return operation()&&valid();
        struct Lease{bool active=true,completed=false;unsigned calls=0;};auto lease=std::make_shared<Lease>();
        struct End{std::shared_ptr<Lease> lease;~End(){lease->active=false;}}end{lease};
        const auto once=[&,lease]{if(!lease->active||++lease->calls!=1||!valid())return false;lease->completed=operation();return lease->completed;};
        return services_.lifecycle_phase(state.id,stage,once)&&lease->calls==1&&lease->completed&&valid();
    };
    const auto each=[&](const std::function<bool(WorldEntityReference)>& operation){
        const auto head=services_.native_group_head(selected);if(head.status!=NativeObjectGroupLinkStatus::member)return false;
        auto cursor=head.reference;std::set<WorldEntityReference> seen;
        for(;;){if(!valid()||!seen.insert(cursor).second||seen.size()>64||!operation(cursor))return false;
            const auto next=services_.native_group_next(cursor);if(next.status==NativeObjectGroupLinkStatus::end)return valid();
            if(next.status!=NativeObjectGroupLinkStatus::member)return false;cursor=next.reference;}
    };
    const auto candidate=[&](WorldEntityReference ref)->std::optional<EntityPosition>{
        const auto origin=services_.native_current_offset(selected),offset=services_.native_current_offset(ref);
        const auto* body=world_.resolve_reference(ref);if(!origin||!offset||!body||!body->object_words.count(1))return {};
        const auto word=[](std::int64_t v){const auto b=std::uint16_t(v);return std::int16_t(b<32768?std::int32_t(b):std::int32_t(b)-65536);};
        const auto byte=[](std::int64_t v){const auto b=std::uint8_t(v);return std::int16_t(b<128?std::int16_t(b):std::int16_t(b)-256);};
        const auto heading=std::uint8_t(std::uint16_t(body->object_words.at(1))&7U);
        if(oow)return EntityPosition{-32768,-32768,1,heading};
        return EntityPosition{word(std::int64_t(request.candidate.x)+(std::int64_t(offset->x)-origin->x)*16),
            word(std::int64_t(request.candidate.y)+(std::int64_t(offset->y)-origin->y)*16),
            byte(std::int64_t(request.candidate.level)+offset->level-origin->level),heading};
    };
    try{
        if(!valid())return fail("native group source metadata is unavailable");
        if(original_class&&(!services_.native_remove_entry10||!services_.native_group_removed||!services_.native_group_added||!services_.entry_point9))
            return fail("original native class entry/World notification authority unavailable");
        bool pre_done=false,finished=false,publish_done=false,restore_done=false,had_refusal=false;unsigned checks=0;ObjectPositionValidStatus latest=ObjectPositionValidStatus::unsupported;
        const auto publish_member=[&](WorldEntityReference ref){auto* state=state_for(ref);const auto pose=candidate(ref);if(!state||!pose||!state->checked||!same_xyz(*state))return false;
            const auto destination=oow?std::optional<EntityPosition>{}:std::optional<EntityPosition>{*pose};
            bool published=false;if(services_.publish_position)published=services_.publish_position(state->id,destination,pose->direction);
            else published=world_.place_individual_on_floor(ref.id,destination,pose->direction);
            if(!published||!valid())return false;result.position_published=true;
            const auto* body=world_.resolve_reference(ref);if(!body||!(body->position==destination)||body->attachment||
                !body->object_words.count(1)||body->object_words.at(1)!=pose->direction)return false;
            if(state->placement.support_slot&&(!services_.place_in_slot0||!services_.place_in_slot0(state->id,state->placement.object)))return false;
            return true;
        };
        NativeObjectPositionStages stages;
        stages.pre_=[&]{
            if(pre_done||finished)return false;
            const auto stories=services_.story_count();if(!stories||*stories<1||!valid())return false;
            if(!original_class&&request.candidate.level>*stories){result.placement.status=ObjectPositionValidStatus::blocked;result.placement.placement_error=18;return false;}
            // Source virtual50: all EP10 before any member removal.
            if(original_class&&!each([&](auto ref){result.effects_started=true;return services_.native_remove_entry10({ref.id,ref.generation});}))return false;
            if(!each([&](auto ref){auto* state=state_for(ref);if(!state)return false;
            const auto old=state->original?services_.room_at(*state->original):std::optional<std::uint16_t>{0};
            const auto next=oow?std::optional<std::uint16_t>{0}:services_.room_at(request.candidate);if(!old||!next)return false;
            // Native rotation invokes its source Pre virtual with argument1.
            // All real members leave/rejoin room/footprint registration.
            state->room_change=true;result.effects_started=true;
            if(!phase(*state,ObjectPositionChangePhase::pre,[&]{return state->lifecycle->pre(state->room_change);}))return false;
            state->detached=state->attachment.has_value();return same_xyz(*state);}))return false;
            if(original_class&&!services_.native_group_removed(request.target))return false;
            pre_done=true;return valid();
        };
        stages.rotate_=[&](std::uint8_t heading)->std::optional<NativeObjectGroupRotation>{
            latest=ObjectPositionValidStatus::unsupported;for(auto& item:states)item.second.checked=false;
            if(!pre_done||finished||heading>7||!valid())return {};
            result.effects_started=true;const auto token=services_.begin_native_rotation(request.target,heading);
            if(!token||token->selected!=selected||!valid())return {};
            latest=ObjectPositionValidStatus::unsupported;for(auto& item:states)item.second.checked=false;
            return token;
        };
        stages.rollback_=[&](const NativeObjectGroupRotation& token){
            latest=ObjectPositionValidStatus::unsupported;for(auto& item:states)item.second.checked=false;
            if(!pre_done||finished||token.selected!=selected||!services_.rollback_native_rotation(token)||!valid())return false;
            latest=ObjectPositionValidStatus::unsupported;for(auto& item:states)item.second.checked=false;
            return true;
        };
        stages.check_=[&]{
            latest=ObjectPositionValidStatus::unsupported;for(auto& item:states)item.second.checked=false;
            if(!pre_done||finished||++checks>2||!valid())return ObjectPositionValidResult{};
            bool blocked=false;
        if(!each([&](auto ref){auto* state=state_for(ref);const auto pose=candidate(ref);if(!state||!pose||!same_xyz(*state))return false;
            ObjectPositionValidRequest check{state->id,*pose,pose->direction,request.flags,authority};check.heading_already_published=true;
            if(oow){state->placement.status=ObjectPositionValidStatus::success;state->placement.placement_error=-1;}
            else state->placement=placement_.evaluate(check);
            if(!valid()||!same_xyz(*state)||state->placement.status==ObjectPositionValidStatus::unsupported)return false;
            const auto* checked=world_.resolve_reference(ref);
            if(state->placement.support_slot&&checked&&!checked->static_footprint&&!state->room_change&&
               (!services_.unregister_support_footprint||!services_.unregister_support_footprint(state->id)))return false;
            state->checked=true;if(state->placement.status==ObjectPositionValidStatus::blocked){blocked=true;result.placement=state->placement;}
            return true;}))return ObjectPositionValidResult{};
            latest=blocked?ObjectPositionValidStatus::blocked:ObjectPositionValidStatus::success;had_refusal|=blocked;
            if(!blocked){result.placement={};result.placement.status=ObjectPositionValidStatus::success;result.placement.placement_error=-1;}
            return result.placement;
        };
        stages.restore_=[&]{
            if(!pre_done||finished||!had_refusal)return false;
            if(!each([&](auto ref){auto* state=state_for(ref);if(!state)return false;
                if(state->attachment){if(!services_.restore_container(state->id,state->container,state->attachment->slot))return false;state->detached=false;}
                return phase(*state,ObjectPositionChangePhase::post,[&]{return state->lifecycle->post(state->id,state->room_change);});}))
                return false;
            finished=true;restore_done=true;return valid();
        };
        stages.publish_=[&]{
            if(!pre_done||finished||latest!=ObjectPositionValidStatus::success)return false;
            if(original_class){
                // Source virtual58 posts each actual individual before moving
                // to live NEXT, but class EP9 is dispatched only after ALL.
                if(!each([&](auto ref){if(!publish_member(ref))return false;auto* state=state_for(ref,true);
                    return state&&phase(*state,ObjectPositionChangePhase::post,[&]{return state->lifecycle->post(state->id,state->room_change,true);});}))return false;
                if(!each([&](auto ref){return services_.entry_point9({ref.id,ref.generation});}))return false;
                if(captured_outside&&!services_.native_group_added(request.target))return false;
            }else{
                if(!each(publish_member))return false;
                if(!each([&](auto ref){auto* state=state_for(ref,true);return state&&phase(*state,ObjectPositionChangePhase::post,[&]{return state->lifecycle->post(state->id,state->room_change);});}))return false;
            }
            finished=true;publish_done=true;return valid();
        };
        result.status=driver(stages);
        if(result.status==ObjectPositionChangeStatus::success&&(!publish_done||!finished||latest!=ObjectPositionValidStatus::success||!valid())){
            result.status=ObjectPositionChangeStatus::unsupported;result.diagnostic="native stage driver reported success without certified publication";
        }
        if(result.status==ObjectPositionChangeStatus::blocked&&((!restore_done&&latest!=ObjectPositionValidStatus::blocked)||!valid())){
            result.status=ObjectPositionChangeStatus::unsupported;result.diagnostic="native stage driver reported an unproved refusal";
        }
        // Preserve absolute placement's pre-effect story refusal. Original
        // source Rotate does not use this absolute destination gate.
        if(!original_class&&!result.effects_started&&!pre_done&&result.placement.status==ObjectPositionValidStatus::blocked)
            result.status=ObjectPositionChangeStatus::blocked;
        if(result.status==ObjectPositionChangeStatus::unsupported&&result.diagnostic.empty())result.diagnostic="native staged operation incomplete; completed source effects retained";
        return result;
    }catch(...){return fail("native group lifecycle callback failed; completed source effects retained");}
}

ObjectPositionChangeResult ObjectPositionChange::apply_single(const ObjectPositionValidRequest& request,bool require_new){
    ObjectPositionChangeResult result;
    const auto fail=[&](const char* why){result.diagnostic=why;return result;};
    if(executing_||!request.source_still_matches||request.direction>7||request.flags>15||
       request.candidate.x<INT16_MIN||request.candidate.x>INT16_MAX||request.candidate.y<INT16_MIN||request.candidate.y>INT16_MAX||
       request.candidate.level<INT8_MIN||request.candidate.level>INT8_MAX)
        return fail("new object placement needs bounded real factory authority");
    struct Guard{bool& value;Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
    const auto valid=[&](){if(!request.source_still_matches())return false;const auto* e=world_.find(request.target.id);
        return e&&e->generation==request.target.generation&&e->kind==EntityKind::object&&!e->ui_ghost;};
    const auto phase=[&](ObjectPositionChangePhase stage,const std::function<bool()>& operation){
        if(!services_.lifecycle_phase)return operation();
        struct State{bool active=true,completed=false;unsigned calls=0;};
        auto state=std::make_shared<State>();
        struct Expire{std::shared_ptr<State> state;~Expire(){state->active=false;}} expire{state};
        const std::function<bool()> scoped=[state,valid,operation]{
            if(!state->active||++state->calls!=1||!valid())return false;
            state->completed=operation();return state->completed;
        };
        const bool completed=services_.lifecycle_phase(request.target,stage,scoped);
        state->active=false;
        return completed&&state->calls==1&&state->completed&&valid();
    };
    const auto* original=world_.find(request.target.id);
    const auto original_position=original?original->position:std::optional<EntityPosition>{};
    const auto original_attachment=original?original->attachment:std::optional<Attachment>{};
    bool detached=false;
    PositionLifecycle lifecycle{world_,services_,request.target,request.source_still_matches,{}};
    const bool out_of_world=request.candidate.x==-32768&&request.candidate.y==-32768&&request.candidate.level==1;
    const auto pose_unchanged=[&](){const auto* e=world_.find(request.target.id);
        if(!e||e->position.has_value()!=original_position.has_value()||(e->position&&!(*e->position==*original_position)))return false;
        if(detached){if(e->attachment)return false;}
        else if(bool(e->attachment)!=bool(original_attachment)||(e->attachment&&
            (e->attachment->container!=original_attachment->container||e->attachment->slot!=original_attachment->slot)))return false;
        return true;};
    const auto field=[&](std::int16_t index)->std::optional<std::int16_t>{const auto* e=world_.find(request.target.id);if(!e)return {};
        const auto found=e->object_words.find(index);return found==e->object_words.end()?std::nullopt:std::optional<std::int16_t>{found->second};};
    const auto write=[&](std::int16_t index,std::int16_t value){VmFrame frame;frame.context.caller=request.target.id;return world_.write(frame,3,index,value);};
    try{
        if(!valid()||!services_.story_count||!services_.admit_single_ordinary||!services_.room_at||
           !services_.unregister_position||!services_.register_position)return fail("object change lifecycle capabilities are unavailable");
        const auto* e=world_.find(request.target.id);
        if(require_new&&(e->attachment||e->position))return fail("new object change requires an unattached out-of-world constructor");
        if(!lifecycle.capture()||!pose_unchanged())return fail("contained source object tree is unknown or changed");
        ObjectPositionIdentity old_container{};
        if(original_attachment){const auto parent=world_.attachment_parent({request.target.id,request.target.generation});
            const auto* container=parent?world_.resolve_reference(*parent):nullptr;
            if(!container||original_attachment->slot>=container->slots.size()||container->slots[original_attachment->slot]!=request.target.id||
               !services_.restore_container)return fail("original source container restoration is unavailable");
            old_container={container->id,container->generation};}
        const auto stories=services_.story_count();if(!stories||*stories<1||!valid()||!pose_unchanged())return fail("source story count is unavailable");
        if(request.candidate.level>*stories){result.status=ObjectPositionChangeStatus::blocked;
            result.placement.status=ObjectPositionValidStatus::blocked;result.placement.placement_error=18;return result;}
        const auto old_room=original_position?services_.room_at(*original_position):std::optional<std::uint16_t>{0};
        if(!old_room||!valid()||!pose_unchanged())return fail("original source room is unavailable");
        const auto candidate_room=out_of_world?std::optional<std::uint16_t>{0}:services_.room_at(request.candidate);
        if(!candidate_room||!valid()||!pose_unchanged())return fail("candidate source room is unavailable");
        const bool room_change=out_of_world||original_attachment.has_value()||*candidate_room!=*old_room; // no portal/container in this profile
        result.effects_started=true;
        if(!phase(ObjectPositionChangePhase::pre,[&]{return lifecycle.pre(room_change);})||!valid())return fail("source PrePositionChange is incomplete");
        detached=original_attachment.has_value();
        if(!pose_unchanged())return fail("source PrePositionChange changed original pose");
        if(out_of_world){result.placement.status=ObjectPositionValidStatus::success;result.placement.placement_error=-1;}
        else result.placement=placement_.evaluate(request);
        if(!valid())return fail("placement check changed its source authority");
        if(result.placement.status==ObjectPositionValidStatus::unsupported)return fail("source PositionValid is incomplete");
        // Real EP5 may change group membership, architectural flags or slot
        // contents. The initial ordinary-profile proof cannot authorize the
        // later PositionChange branches after those effects. Keep the effects
        // but refuse this bounded lifecycle before publishing its position.
        const auto after_check=services_.admit_single_ordinary(request.target);
        if(!after_check||!*after_check||!valid()||!pose_unchanged()||!lifecycle.matches())
            return fail("placement check changed the admitted ordinary single-object profile");
        if(!pose_unchanged())return fail("placement callback moved the object outside the admitted lifecycle");
        const bool accepted=result.placement.status==ObjectPositionValidStatus::success;
        if(accepted){const auto notches=field(23);if(!notches||!field(1))return fail("source object direction words are unavailable");
            auto direction=static_cast<std::int16_t>(request.direction);if(*notches>1)direction=static_cast<std::int16_t>(direction-direction%*notches);
            auto position=request.candidate;position.direction=static_cast<std::uint8_t>(direction);
            const auto desired=out_of_world?std::optional<EntityPosition>{}:std::optional<EntityPosition>{position};
            bool published=false;
            if(services_.publish_position)published=services_.publish_position(request.target,desired,position.direction);
            else{
                if(!write(1,direction)||!valid())return fail("SetIndivPosition direction publication failed");
                published=world_.place_individual_on_floor(request.target.id,desired,position.direction);
            }
            if(!published)
                return fail("SetIndivPosition position publication failed");
            result.position_published=true;if(!valid())return fail("published object source authority changed");
            const auto* physical=world_.find(request.target.id);
            if(!physical||physical->generation!=request.target.generation||physical->position.has_value()!=desired.has_value()||
               (desired&&!(*physical->position==*desired))||physical->attachment||field(1)!=std::optional<std::int16_t>{direction})
                return fail("SetIndivPosition publisher did not publish its exact source identity and pose");
            if(result.placement.support_slot){if(!services_.place_in_slot0||
                !services_.place_in_slot0(request.target,result.placement.object)||!valid())return fail("source slot0 placement is incomplete");
                const auto* target=world_.find(request.target.id);const auto* support=world_.find(result.placement.object.id);
                if(!support||support->generation!=result.placement.object.generation||!target->attachment||
                   target->attachment->container!=support->id||target->attachment->slot!=0||support->slots.empty()||support->slots[0]!=target->id||
                   !target->position||!support->position||target->position->x!=support->position->x||target->position->y!=support->position->y||target->position->level!=support->position->level)
                    return fail("source slot0 reciprocal publication differs from its result");}
        }
        else if(original_attachment){
            const auto* old=world_.resolve_reference({old_container.id,old_container.generation});
            if(!old||old->generation!=old_container.generation||
               !services_.restore_container(request.target,old_container,original_attachment->slot)||!valid())return fail("failed source placement could not restore original slot");
            const auto* restored=world_.find(request.target.id);old=world_.resolve_reference({old_container.id,old_container.generation});
            if(!restored->attachment||restored->attachment->container!=old_container.id||restored->attachment->slot!=original_attachment->slot||
               !old||old->generation!=old_container.generation||old->slots.size()<=original_attachment->slot||old->slots[original_attachment->slot]!=request.target.id)
                return fail("original slot restoration differs from its source identity");
        }
        if(!phase(ObjectPositionChangePhase::post,[&]{return lifecycle.post(request.target,room_change);}))return fail("source recursive PositionChange is incomplete");
        result.status=accepted?ObjectPositionChangeStatus::success:ObjectPositionChangeStatus::blocked;return result;
    }catch(...){return fail("source object position lifecycle callback threw; completed effects retained");}
}
} // namespace odh
