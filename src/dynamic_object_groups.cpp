// SPDX-License-Identifier: MPL-2.0
#include "odh/dynamic_object_groups.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace odh {
namespace {
bool same(ObjectPositionIdentity a,const ObjectGroupMember& b){return a.id==b.id&&a.generation==b.generation;}
ObjectTradeIdentity value_id(ObjectPositionIdentity id){return {id.id,id.generation};}
WorldEntityReference reference(const ObjectGroupMember& m){return {m.id,m.generation};}
// Native 422a6e/4236d0: signed 32-bit field arithmetic, cardinal rotation,
// odd headings leave XY zero. These are separate from managed VM offsets.
std::int32_t native_word(std::int64_t value){const auto bits=std::uint32_t(value);return bits<=INT32_MAX?std::int32_t(bits):std::int32_t(std::int64_t(bits)-4294967296LL);}
NativeObjectGroupOffset rotate_native(NativeObjectGroupOffset value,std::int16_t direction){
    const auto x=value.x,y=value.y;value.x=value.y=0;
    switch(std::uint16_t(direction)&7){case 0:value.x=x;value.y=y;break;case 2:value.x=native_word(-std::int64_t(y));value.y=x;break;
        case 4:value.x=native_word(-std::int64_t(x));value.y=native_word(-std::int64_t(y));break;case 6:value.x=y;value.y=native_word(-std::int64_t(x));break;}return value;
}
bool native_complete(const ObjectGroup& group){
    if(!group.native_head)return std::none_of(group.members.begin(),group.members.end(),[](const auto& m){return m.native.has_value();});
    if(group.members.empty()||*group.native_head!=reference(group.members.front()))return false;
    for(std::size_t n=0;n<group.members.size();++n)if(!group.members[n].native||group.members[n].native->next!=
        (n+1<group.members.size()?reference(group.members[n+1]):WorldEntityReference{}))return false;return true;
}
void relink_native(ObjectGroup& group){
    if(!group.native_head)return;
    if(group.members.empty()){group.native_head.reset();return;}
    group.native_head=reference(group.members.front());
    for(std::size_t n=0;n<group.members.size();++n)group.members[n].native->next=n+1<group.members.size()?reference(group.members[n+1]):WorldEntityReference{};
}
std::int16_t short_value(std::int64_t x){const auto b=static_cast<std::uint16_t>(x);return static_cast<std::int16_t>(b<32768?b:static_cast<std::int32_t>(b)-65536);}
std::int16_t byte_value(std::int32_t x){const auto b=static_cast<std::uint8_t>(x);return static_cast<std::int16_t>(b<128?b:static_cast<std::int16_t>(b)-256);}
int tile(std::int16_t x){return x>=0?x/16:-((-static_cast<int>(x)+15)/16);}
bool adjacent(const Entity& a,const Entity& b){
    if(!a.position||!b.position||!a.definition||!b.definition||a.definition->guid!=b.definition->guid)return false;
    const int dx=std::abs(tile(short_value(b.position->x-a.position->x))),dy=std::abs(tile(short_value(b.position->y-a.position->y)));
    // Pinned source does not filter by Level here.
    return dx<2&&dy<2&&dx+dy<2;
}
std::int16_t flags(const Entity& a,const Entity& b){
    constexpr double pi=3.14159265358979323846;
    double angle=std::atan2(static_cast<double>(b.position->x-a.position->x),static_cast<double>(a.position->y-b.position->y));
    angle=std::fmod(angle+2*pi,2*pi);
    const int direction=static_cast<int>(std::nearbyint(angle/pi*4));
    const auto heading=a.object_words.find(1);const int quarter=heading==a.object_words.end()||heading->second%2?0:heading->second/2;
    const int difference=((direction-quarter*2)%8+8)%8;
    return static_cast<std::int16_t>(1<<difference);
}
}
bool DynamicObjectGroups::current()const{return services_.authority&&services_.authority();}
bool DynamicObjectGroups::fail(const char* why){diagnostic_=why;return false;}
void DynamicObjectGroups::changed(){std::sort(groups_.groups.begin(),groups_.groups.end(),[](const auto&a,const auto&b){return a.base_id<b.base_id;});if(services_.changed)services_.changed();}
ObjectGroup* DynamicObjectGroups::find(ObjectPositionIdentity id){
    ObjectGroup* found=nullptr;for(auto&g:groups_.groups)for(const auto&m:g.members)if(same(id,m)){if(found)return nullptr;found=&g;}return found;
}
std::optional<ObjectGroup> DynamicObjectGroups::prepare_association(ObjectPositionIdentity id){
    const auto reject=[&](const char* text)->std::optional<ObjectGroup>{fail(text);return {};};
    if(!current())return reject("dynamic association authority expired");
    auto* existing=find(id);std::optional<ObjectGroup> owned;
    if(!existing){if(!services_.source_group)return reject("dynamic source association unavailable");owned=services_.source_group(id);if(!owned)return reject("dynamic source association unknown");existing=&*owned;}
    const auto* value=values_.query(value_id(id));
    if(!value||!value->base||value->members.size()!=existing->members.size()||value->multitile!=existing->multitile||value->base->id!=existing->base_id||existing->members.empty()||existing->members.size()>64)
        return reject("dynamic physical/value association differs");
    // Source BaseObject is resource-defined; unknown legacy lead metadata
    // cannot be promoted into a new physical authority by a partial mutation.
    if(!object_value_source_base(value->members))return reject("dynamic source base metadata is incomplete");
    auto prepared=*existing;prepared.kind=ObjectGroupKind::live_association;prepared.definition_guid=0;
    for(std::size_t n=0;n<prepared.members.size();++n){auto&m=prepared.members[n];const auto&v=value->members[n];
        const auto*e=world_.find(m.id);if(!e||e->generation!=m.generation||e->kind!=EntityKind::object||e->ui_ghost||e->dead||!e->definition||v.object.id!=m.id||v.object.generation!=m.generation||v.offset_x!=m.offset_x||v.offset_y!=m.offset_y||v.offset_level!=m.offset_level)
            return reject("dynamic member identity or ordered offsets differ");
        auto origin=m.origin?m.origin:services_.origin?services_.origin({m.id,m.generation}):std::nullopt;
        if(!origin||origin->guid!=v.object.guid||origin->master_guid!=v.object.master_guid||origin->source!=v.object.source||origin->asset!=v.object.asset||origin->definition_chunk_id!=v.object.definition_chunk_id||e->definition->guid!=origin->guid||e->definition->master_guid!=origin->master_guid)
            return reject("dynamic member resource provenance differs");
        m.origin=std::move(origin);
    }
    if(!current())return reject("dynamic source changed during authentication");
    if(!native_complete(prepared))return reject("native group metadata is incomplete or unordered");
    return prepared;
}
const ObjectGroup* DynamicObjectGroups::find(ObjectPositionIdentity id)const{
    const ObjectGroup* found=nullptr;for(const auto& g:groups_.groups)for(const auto& m:g.members)if(same(id,m)){if(found)return nullptr;found=&g;}return found;
}
bool DynamicObjectGroups::collect_retired_native_members(){
    if(!current())return fail("native retired collection authority unavailable");
    auto& old=groups_.retired_native_members;
    old.erase(std::remove_if(old.begin(),old.end(),[&](const auto& m){return !world_.resolve_reference(m.reference);}),old.end());return true;
}
bool DynamicObjectGroups::bind_native_created_group(ObjectPositionIdentity target,WorldEntityReference head,
        const std::vector<NativeObjectGroupSourceMember>& rows){
    return bind_native_group(target,head,rows,ObjectGroupMemberSource::native_created);
}
bool DynamicObjectGroups::ensure(ObjectPositionIdentity id){
    auto prepared=prepare_association(id);if(!prepared)return false;
    auto* existing=find(id);
    if(!existing){if(groups_.groups.size()>=4096)return fail("dynamic association budget exceeded");
        for(const auto& g:groups_.groups)for(const auto& m:g.members)for(const auto& p:prepared->members)if(m.id==p.id)return fail("dynamic association overlaps saved membership");
        groups_.groups.push_back(std::move(*prepared));
    }else *existing=std::move(*prepared);
    changed();return current();
}
bool DynamicObjectGroups::bind_original_native_group(ObjectPositionIdentity target,WorldEntityReference head,
        const std::vector<NativeObjectGroupSourceMember>& rows){
    return bind_native_group(target,head,rows,ObjectGroupMemberSource::original_import);
}
bool DynamicObjectGroups::bind_native_group(ObjectPositionIdentity target,WorldEntityReference head,
        const std::vector<NativeObjectGroupSourceMember>& rows,ObjectGroupMemberSource source){
    auto prepared=prepare_association(target);if(!prepared)return false;
    if(rows.empty()||rows.size()>64||rows.size()!=prepared->members.size()||head!=rows.front().reference)
        return fail("native source head/order/extent is unavailable");
    for(std::size_t n=0;n<rows.size();++n){auto& member=prepared->members[n];
        if(reference(member)!=rows[n].reference||!world_.is_live_reference(rows[n].reference)||!member.origin||member.origin->source_kind!=source)
            return fail("native source order/origin differs from authenticated physical/value association");
        const NativeObjectGroupMember expected{rows[n].authored,rows[n].current,n+1<rows.size()?rows[n+1].reference:WorldEntityReference{}};
        if(member.native){const auto equal=[](const auto& a,const auto& b){return a.x==b.x&&a.y==b.y&&a.level==b.level;};
            if(!equal(member.native->authored,expected.authored)||!equal(member.native->current,expected.current)||member.native->next!=expected.next)
                return fail("native source binding conflicts with existing owned fields");}
        member.native=expected;
    }
    if(prepared->native_head&&*prepared->native_head!=head)return fail("native source binding conflicts with owned head");
    prepared->native_head=head;
    auto staged=groups_;bool replaced=false;
    for(auto& group:staged.groups)if(std::any_of(group.members.begin(),group.members.end(),[&](const auto& m){return same(target,m);})){
        group=*prepared;replaced=true;break;}
    if(!replaced)staged.groups.push_back(*prepared);
    std::sort(staged.groups.begin(),staged.groups.end(),[](const auto& a,const auto& b){return a.base_id<b.base_id;});
    try{validate_object_groups(staged,world_.snapshot());}catch(const std::exception& e){diagnostic_=e.what();return false;}
    if(!current())return fail("native source binding authority expired before publication");
    groups_=std::move(staged);changed();return current();
}
NativeObjectGroupLink DynamicObjectGroups::group_head(WorldEntityReference target)const{
    if(!current()||!world_.resolve_reference(target))return {};
    const auto* group=find({target.id,target.generation});
    if(group&&group->native_head&&native_complete(*group))return {NativeObjectGroupLinkStatus::member,*group->native_head};
    for(const auto& old:groups_.retired_native_members)if(old.reference==target&&!world_.is_live_reference(target))
        return {NativeObjectGroupLinkStatus::member,target};
    return {};
}
NativeObjectGroupLink DynamicObjectGroups::group_next(WorldEntityReference target)const{
    if(!current()||!world_.resolve_reference(target))return {};
    const auto* group=find({target.id,target.generation});
    if(group&&group->native_head&&native_complete(*group))for(const auto& member:group->members)if(reference(member)==target){
        const auto next=member.native->next;
        if(!next.id)return {NativeObjectGroupLinkStatus::end,{}};
        if(!world_.resolve_reference(next))return {};return {NativeObjectGroupLinkStatus::member,next};
    }
    for(const auto& old:groups_.retired_native_members)if(old.reference==target&&!world_.is_live_reference(target)&&old.fields.next==WorldEntityReference{})
        return {NativeObjectGroupLinkStatus::end,{}};
    return {};
}
std::optional<WorldEntityReference> DynamicObjectGroups::resolve_interaction_group(WorldEntityReference selected){
    diagnostic_.clear();
    const auto unavailable=[&](const char* why)->std::optional<WorldEntityReference>{fail(why);return {};};
    const auto word=[&](WorldEntityReference ref,std::int16_t index)->std::optional<std::int16_t>{
        if(!current())return {};
        const auto* body=world_.resolve_reference(ref);if(!body||!body->definition)return {};
        const auto guid=body->definition->guid;
        // This frame is a generation-bound bank address, not another VM or an
        // execution context. Scope21 uses the actual World resource provider.
        VmFrame address;address.context.entity_references_bound=true;
        if(!vm_context_set_reference(address.context,VmEntityRole::stack_object,ref))return {};
        const auto value=world_.read(address,21,index);
        body=world_.resolve_reference(ref);
        if(!current()||!body||!body->definition||body->definition->guid!=guid)return {};
        return value;
    };
    try{
        const auto master=word(selected,10);
        if(!master)return unavailable("interaction group selected identity/MasterID resource word unavailable");
        if(!*master)return selected; // Source returns before reading group8.
        const auto group=word(selected,8);
        if(!group)return unavailable("interaction group signed definition word8 unavailable");
        const auto* body=world_.resolve_reference(selected);
        if(!body)return unavailable("interaction group selected exact body disappeared");
        if(body->kind!=EntityKind::object)return selected; // Proven non-MT RTTI.
        const auto head=group_head(selected);
        if(head.status!=NativeObjectGroupLinkStatus::member)
            return unavailable("interaction group native MT ownership is unknown or invalid");
        // Legacy MT metadata remains explicitly unsupported even for the
        // trivial nonpositive branch; callers retain their old path separately.
        if(*group<=0)return selected;
        const auto negative=std::int16_t(-std::int32_t(*group));
        auto cursor=head.reference;std::set<WorldEntityReference> visited;
        for(;;){
            if(!current()||!visited.insert(cursor).second||visited.size()>64)
                return unavailable("interaction group native traversal expired, cycled or exceeded budget");
            const auto value=word(cursor,8);
            if(!value)return unavailable("interaction group member identity/resource word unavailable");
            const auto actual_head=group_head(selected);const auto next=group_next(cursor);
            if(actual_head.status!=NativeObjectGroupLinkStatus::member||actual_head.reference!=head.reference||
               next.status==NativeObjectGroupLinkStatus::unavailable)
                return unavailable("interaction group native membership changed or became invalid during resource read");
            if(*value==negative)return cursor;
            if(next.status==NativeObjectGroupLinkStatus::end)return selected;
            cursor=next.reference;
        }
    }catch(const std::exception& error){diagnostic_=std::string("interaction group resource lookup failed: ")+error.what();return {};}
}
std::optional<NativeObjectGroupOffset> DynamicObjectGroups::native_offset(WorldEntityReference target)const{
    if(!current()||!world_.resolve_reference(target))return {};
    const auto* group=find({target.id,target.generation});
    if(group&&group->native_head&&native_complete(*group))for(const auto& member:group->members)if(reference(member)==target)return member.native->current;
    for(const auto& old:groups_.retired_native_members)if(old.reference==target&&!world_.is_live_reference(target))return old.fields.current;
    return {};
}
bool DynamicObjectGroups::recompute_current_offsets(WorldEntityReference leader){
    if(!current()||!world_.is_live_reference(leader))return fail("native offset head identity unavailable");
    auto* group=find({leader.id,leader.generation});if(!group||!group->native_head||*group->native_head!=leader||!native_complete(*group))
        return fail("native offset recomposition requires actual source head");
    const auto* head=world_.resolve_reference(leader);const auto direction=head->object_words.find(1);
    if(direction==head->object_words.end())return fail("native offset head direction unavailable");
    auto staged=*group;const auto origin=staged.members.front().native->authored;
    for(auto& member:staged.members){if(!world_.is_live_reference(reference(member)))return fail("native offset member body unavailable");
        const auto a=member.native->authored;
        NativeObjectGroupOffset delta{native_word(std::int64_t(a.x)-origin.x),native_word(std::int64_t(a.y)-origin.y),native_word(std::int64_t(a.level)-origin.level)};
        member.native->current=rotate_native(delta,direction->second);}
    if(!current())return fail("native offset authority expired before publication");
    *group=std::move(staged);changed();return current();
}
bool DynamicObjectGroups::rotate_live_native(WorldEntityReference selected,std::uint8_t delta){
    if(delta>7||!services_.publish_native_direction)return fail("native raw heading publisher unavailable");
    const auto head=group_head(selected);if(head.status!=NativeObjectGroupLinkStatus::member)return fail("native rotation source head unavailable");
    // Authenticate the current complete chain before the first field write.
    auto cursor=head.reference;std::set<WorldEntityReference> checked;
    for(;;){
        const auto* body=world_.resolve_reference(cursor);
        if(!body||!world_.is_live_reference(cursor)||!checked.insert(cursor).second||checked.size()>64||!body->object_words.count(1))
            return fail("native rotation member heading identity unavailable");
        const auto next=group_next(cursor);if(next.status==NativeObjectGroupLinkStatus::end)break;
        if(next.status!=NativeObjectGroupLinkStatus::member)return fail("native rotation member link unavailable");cursor=next.reference;
    }
    cursor=head.reference;std::set<WorldEntityReference> visited;
    for(;;){
        if(!current()||!visited.insert(cursor).second||visited.size()>64)return fail("native rotation live traversal expired or exceeded budget");
        const auto* body=world_.resolve_reference(cursor);if(!body||!world_.is_live_reference(cursor))return fail("native rotation body disappeared");
        const auto word=body->object_words.find(1);if(word==body->object_words.end())return fail("native rotation heading disappeared");
        const auto direction=std::uint8_t((std::uint16_t(word->second)+delta)&7U);
        if(!services_.publish_native_direction(cursor,direction)||!current())return fail("native rotation raw heading publication failed");
        // Source reads next only after its heading/visual consumers complete.
        const auto next=group_next(cursor);if(next.status==NativeObjectGroupLinkStatus::end)break;
        if(next.status!=NativeObjectGroupLinkStatus::member)return fail("native rotation outgoing link unavailable after publication");cursor=next.reference;
    }
    const auto actual_head=group_head(selected);
    return actual_head.status==NativeObjectGroupLinkStatus::member&&recompute_current_offsets(actual_head.reference);
}
std::optional<NativeObjectGroupRotation> DynamicObjectGroups::begin_native_rotation(ObjectPositionIdentity target,std::uint8_t heading){
    const WorldEntityReference selected{target.id,target.generation};
    if(heading>7||!current()||!world_.is_live_reference(selected)){fail("native rotation selected heading identity unavailable");return {};}
    const auto head=group_head(selected);const auto* body=world_.resolve_reference(selected);
    if(head.status!=NativeObjectGroupLinkStatus::member||!body||!body->object_words.count(1)){
        fail("native rotation requires explicit actual source metadata");return {};}
    const auto delta=std::uint8_t((heading-std::uint16_t(body->object_words.at(1)))&7U);
    NativeObjectGroupRotation rotation{selected,head.reference,delta};
    // No rotation means no native88 call: retain imported current1a0 exactly.
    if(delta&&!rotate_live_native(selected,delta))return {};
    return rotation;
}
bool DynamicObjectGroups::rollback_native_rotation(const NativeObjectGroupRotation& rotation){
    if(rotation.delta>7||!current()||!world_.is_live_reference(rotation.selected))return fail("native rotation refusal lost selected identity");
    if(!rotation.delta)return true;
    // Source subtracts from CURRENT headings over CURRENT membership; restoring
    // a captured vector would erase authored callbacks or revive removed parts.
    return rotate_live_native(rotation.selected,std::uint8_t((8U-rotation.delta)&7U));
}
std::optional<std::vector<ObjectPositionIdentity>> DynamicObjectGroups::split_for_move(ObjectPositionIdentity id){
    const auto*e=world_.find(id.id);if(!current()||!e||e->generation!=id.generation||!e->entry_points||e->entry_points->size()<=8){fail("dynamic split entry-point authority unavailable");return {};}
    if(e->entry_points->at(8).action<256)return std::vector<ObjectPositionIdentity>{};
    if(!ensure(id))return {};auto* old=find(id);if(!old){fail("dynamic split lost source group");return {};}
    if(old->members.size()==1)return std::vector<ObjectPositionIdentity>{};
    const auto raw=services_.definition_offset?services_.definition_offset(id):std::nullopt;
    const auto* value=values_.query(value_id(id));if(!raw||!same(id,*raw)||!value){fail("dynamic split authored offsets unavailable");return {};}
    const auto vm=std::find_if(value->members.begin(),value->members.end(),[&](const auto&m){return m.object.id==id.id&&m.object.generation==id.generation;});
    if(vm==value->members.end()||!vm->lead_object){fail("dynamic split lead provenance unavailable");return {};}
    auto remaining_values=value->members;
    remaining_values.erase(std::remove_if(remaining_values.begin(),remaining_values.end(),[&](const auto&m){return m.object.id==id.id&&m.object.generation==id.generation;}),remaining_values.end());
    if(!object_value_source_base(remaining_values)){fail("dynamic split survivor base metadata is incomplete");return {};}
    ObjectGroup next;next.kind=ObjectGroupKind::live_association;next.multitile=old->multitile;next.base_id=id.id;
    auto member=*raw;const auto original_member=std::find_if(old->members.begin(),old->members.end(),[&](const auto&m){return same(id,m);});
    member.origin=original_member->origin;member.native=original_member->native;
    if(old->native_head){next.native_head=WorldEntityReference{id.id,id.generation};member.native->next={};}
    next.members={member};
    if(groups_.groups.size()>=4096||!values_.split_member(value_id(id),member.offset_x,member.offset_y,member.offset_level,vm->lead_object)){fail("dynamic split value transition rejected");return {};}
    old=find(id);old->members.erase(std::remove_if(old->members.begin(),old->members.end(),[&](const auto&m){return same(id,m);}),old->members.end());
    const auto remaining_id=old->members.front();const auto*left=values_.query({remaining_id.id,remaining_id.generation});if(!left||!left->base){fail("dynamic split remaining base unavailable");return {};}
    old->base_id=left->base->id;relink_native(*old);std::vector<ObjectPositionIdentity> remaining;for(const auto&m:old->members)remaining.push_back({m.id,m.generation});
    groups_.groups.push_back(std::move(next));changed();return remaining;
}
bool DynamicObjectGroups::erase_after_retire(ObjectPositionIdentity id){
    if(!current()||world_.is_live_reference({id.id,id.generation})||values_.query(value_id(id)))return fail("dynamic erase requires completed unique retirement");
    const auto* group=find(id);if(!group)return true;
    if(!native_complete(*group))return fail("native retirement group metadata invalid");
    auto staged=groups_;const auto target=WorldEntityReference{id.id,id.generation};
    staged.retired_native_members.erase(std::remove_if(staged.retired_native_members.begin(),staged.retired_native_members.end(),
        [&](const auto& m){return !world_.resolve_reference(m.reference);}),staged.retired_native_members.end());
    for(auto it=staged.groups.begin();it!=staged.groups.end();++it){
        auto member=std::find_if(it->members.begin(),it->members.end(),[&](const auto& m){return same(id,m);});if(member==it->members.end())continue;
        if(it->native_head&&world_.resolve_reference(target)){
            if(staged.retired_native_members.size()>=4096)return fail("native retired node budget exceeded");
            // Native 422909 clears parent190 at422940 and NEXT18c at422943.
            // Keep those exact fields only while World owns this exact retained body.
            auto fields=*member->native;fields.next={};
            const auto position=std::lower_bound(staged.retired_native_members.begin(),staged.retired_native_members.end(),target,
                [](const auto& m,auto r){return m.reference<r;});
            if(position!=staged.retired_native_members.end()&&position->reference==target)return fail("native retirement was already published");
            staged.retired_native_members.insert(position,{target,fields});
        }
        it->members.erase(member);
        if(it->members.empty())staged.groups.erase(it);
        else {const auto first=it->members.front();const auto* value=values_.query({first.id,first.generation});
            if(!value||!value->base)return fail("dynamic erase survivor base unavailable");it->base_id=value->base->id;relink_native(*it);}
        break;
    }
    std::sort(staged.groups.begin(),staged.groups.end(),[](const auto& a,const auto& b){return a.base_id<b.base_id;});
    // Validate actual body ownership and budgets before this owner's publication.
    try{validate_object_groups(staged,world_.snapshot());}catch(const std::exception& e){diagnostic_=e.what();return false;}
    groups_=std::move(staged);changed();return current();
}
bool DynamicObjectGroups::combine(ObjectPositionIdentity receiver,ObjectPositionIdentity donor){
    if(!ensure(receiver)||!ensure(donor))return false;auto*to=find(receiver);auto*from=find(donor);if(!to||!from)return fail("dynamic combine association disappeared");if(to==from)return true;
    if(to->members.size()+from->members.size()>64)return fail("dynamic combined association exceeds member budget");
    if(!native_complete(*to)||!native_complete(*from)||to->native_head.has_value()!=from->native_head.has_value())
        return fail("native combine cannot infer unknown source fields");
    const auto*base=world_.find(to->base_id);if(!base||!base->position)return fail("dynamic receiver base pose unavailable");
    const auto heading=base->object_words.find(1);if(heading==base->object_words.end())return fail("dynamic receiver direction unavailable");
    const auto receiver_base=to->base_id,donor_base=from->base_id;const auto direction=heading->second;auto members=from->members;
    for(auto&m:members){const auto*e=world_.find(m.id);if(!e||e->generation!=m.generation||!e->position)return fail("dynamic donor pose unavailable");
        // Pinned Combine discards Vector3.Transform's return, so offsets are
        // the raw world difference even when the receiver faces east/west.
        m.offset_x=short_value(e->position->x-base->position->x);m.offset_y=short_value(e->position->y-base->position->y);m.offset_level=byte_value(e->position->level-base->position->level);}
    if(to->native_head){
        const auto* native_head=world_.resolve_reference(*to->native_head);
        if(!native_head||!native_head->position)return fail("native receiver head pose unavailable");
        const auto found=std::find_if(to->members.begin(),to->members.end(),[&](const auto& m){return reference(m)==*to->native_head;});
        const auto native_direction=native_head->object_words.find(1);if(found==to->members.end()||!found->native||native_direction==native_head->object_words.end())return fail("native receiver head fields unavailable");
        for(auto& member:members){const auto* e=world_.resolve_reference(reference(member));if(!e||!e->position||!member.native)return fail("native donor body/fields unavailable");
            // 423416..42345f: current XY are world differences plus receiver
            // head's current offsets; authored XY inverse-rotate that result.
            // Native Combine does not assign either saved level field here.
            const auto floor16=[](std::int64_t v){return v>=0?v/16:-((-v+15)/16);};
            auto current=member.native->current;
            current.x=native_word(floor16(native_word(std::int64_t(e->position->x)-native_head->position->x+native_word(std::int64_t(found->native->current.x)*16))));
            current.y=native_word(floor16(native_word(std::int64_t(e->position->y)-native_head->position->y+native_word(std::int64_t(found->native->current.y)*16))));
            const auto authored=rotate_native(current,std::int16_t((8-(std::uint16_t(native_direction->second)&7))&7));
            member.native->current=current;member.native->authored.x=authored.x;member.native->authored.y=authored.y;
        }
    }
    const auto*receiver_values=values_.query(value_id(receiver));const auto*donor_values=values_.query(value_id(donor));
    if(!receiver_values||!donor_values)return fail("dynamic combine value cohort unavailable");
    auto combined_values=receiver_values->members;
    for(std::size_t n=0;n<members.size();++n){auto v=donor_values->members[n];v.offset_x=members[n].offset_x;v.offset_y=members[n].offset_y;v.offset_level=members[n].offset_level;combined_values.push_back(std::move(v));}
    if(!object_value_source_base(combined_values))return fail("dynamic combine resulting base metadata is incomplete");
    if(!values_.combine_groups(value_id(receiver),value_id(donor),members))return fail("dynamic combine shared-value transition rejected");
    to=find(receiver);auto combined=*to;combined.members.insert(combined.members.end(),members.begin(),members.end());
    const auto*v=values_.query(value_id(receiver));if(!v||!v->base)return fail("dynamic combined base unavailable");combined.base_id=v->base->id;relink_native(combined);
    groups_.groups.erase(std::remove_if(groups_.groups.begin(),groups_.groups.end(),[&](const auto&g){return g.base_id==donor_base||g.base_id==receiver_base;}),groups_.groups.end());
    groups_.groups.push_back(std::move(combined));
    for(const auto&m:members){const ObjectPositionIdentity id{m.id,m.generation};bool published=false;
        if(services_.publish_direction)published=services_.publish_direction(id,static_cast<std::uint8_t>(direction));
        else{VmFrame frame;frame.context.caller=m.id;published=world_.write(frame,3,1,direction);}
        if(!published)return fail("dynamic combine direction publication failed");}
    changed();return current();
}
bool DynamicObjectGroups::update(ObjectPositionIdentity id,bool merge){
    if(depth_==0){calls_=0;diagnostic_.clear();}if(++calls_>4096||depth_>=64)return fail("dynamic update execution budget exceeded");
    ++depth_;struct End{std::size_t&depth;~End(){--depth;}}end{depth_};return update_impl(id,merge);
}
bool DynamicObjectGroups::update_impl(ObjectPositionIdentity id,bool merge){
    if(!ensure(id))return false;const auto*e=world_.find(id.id);if(!e||!e->position)return fail("dynamic update position unavailable");
    // Preserve the existing pinned managed dynamic-adjacency enumeration.
    // This is separate from original native Load/group NEXT authority.
    // Freeze only identities before callbacks; do not copy all World banks.
    // Include ghosts just as the former snapshot enumeration did.
    std::vector<ObjectPositionIdentity> candidates;if(merge){
        for(std::int16_t cursor=0;;){const auto next=world_.next_entity(cursor,{},true);if(!next)break;cursor=*next;
            const auto* other=world_.find(cursor);if(other&&other->id!=id.id)candidates.push_back({other->id,other->generation});}
    }
    else{const auto*g=find(id);for(const auto&m:g->members)if(m.id!=id.id)candidates.push_back({m.id,m.generation});}
    std::int16_t mask=0;
    for(const auto other_id:candidates){e=world_.find(id.id);const auto*other=world_.find(other_id.id);if(!current()||!e||e->generation!=id.generation)return fail("dynamic update source changed");
        if(!other||other->generation!=other_id.generation||!adjacent(*e,*other))continue;
        if(merge){if(!ensure(other_id))return false;if(find(id)!=find(other_id)){
            const auto old=*find(other_id);if(!combine(id,other_id))return false;
            for(const auto&m:old.members)if(!update({m.id,m.generation},true))return false;
        }}
        e=world_.find(id.id);other=world_.find(other_id.id);if(!e||!other||!e->position||!other->position)return fail("dynamic update changed referenced position");mask=static_cast<std::int16_t>(mask|flags(*e,*other));
    }
    return services_.execute_entry8&&services_.execute_entry8(id,mask)&&current();
}
}
