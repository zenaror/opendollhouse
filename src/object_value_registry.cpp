// SPDX-License-Identifier: MPL-2.0
// FreeSO7e76 VMMultitileGroup.BaseObject/Price and VMEntity.CurrentValue.
#include "odh/object_value_registry.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
namespace odh {
namespace {
bool same(ObjectTradeIdentity a,ObjectTradeIdentity b){return a.id==b.id&&a.generation==b.generation;}
bool text(const std::string& value,bool empty=false){return (empty||!value.empty())&&value.size()<=4096&&value.find('\0')==std::string::npos;}
void need(bool value,const char* message){if(!value)throw FormatError(message);}
bool live(const World& world,const SessionObjectValueMember& member){
    const auto& object=member.object;const auto* e=world.find(object.id);
    return e&&e->generation==object.generation&&e->kind==EntityKind::object&&!e->ui_ghost&&e->definition&&
        e->definition->guid==object.guid&&e->definition->master_guid==object.master_guid;
}
std::uint64_t next_revision(const SessionObjectTradeLedger& ledger){
    if(ledger.revision==std::numeric_limits<std::uint64_t>::max())throw FormatError("object value revision exhausted");return ledger.revision+1;
}
}
std::optional<ObjectTradeIdentity> object_value_source_base(const std::vector<SessionObjectValueMember>& members){
    if(members.empty())return {};
    if(members.size()==1)return ObjectTradeIdentity{members[0].object.id,members[0].object.generation};
    for(const auto& member:members){
        if(!member.lead_object)return {}; // An earlier unknown might be the first lead.
        if(*member.lead_object>0)return ObjectTradeIdentity{member.object.id,member.object.generation};
    }
    for(const auto& member:members)if(!member.offset_x&&!member.offset_y&&!member.offset_level)
        return ObjectTradeIdentity{member.object.id,member.object.generation};
    return ObjectTradeIdentity{members[0].object.id,members[0].object.generation};
}
void validate_object_value_groups(const SessionObjectTradeLedger& ledger,const WorldSnapshot& world){
    need(ledger.version==1||ledger.version==2,"unsupported object value ledger version");
    if(ledger.version==1){need(ledger.groups.empty()&&!ledger.revision,"legacy object value ledger has v2 state");return;}
    need(ledger.objects.empty(),"object value ledger has competing legacy/group owners");
    need(ledger.groups.size()<=4096,"object value group count exceeds budget");
    std::size_t members=0,bytes=0;std::uint64_t previous=0;std::set<std::int16_t> ids;
    const auto resource=[&](std::uint32_t guid,const std::string& source,const std::string& asset,std::uint16_t chunk){
        need(guid&&chunk&&text(source)&&text(asset,true),"object value resource provenance invalid");
        need(source.size()+asset.size()<=1048576-bytes,"object value provenance budget exceeded");bytes+=source.size()+asset.size();
    };
    for(const auto& group:ledger.groups){
        need(group.group_id>previous&&group.group_id<=ledger.revision&&group.revision&&group.revision<=ledger.revision,"object value group identity/revision invalid");previous=group.group_id;
        need(!group.members.empty()&&group.members.size()<=64&&group.members.size()<=4096-members,"object value membership budget invalid");members+=group.members.size();
        for(const auto& member:group.members){const auto& object=member.object;
            need(object.id>0&&object.generation&&ids.insert(object.id).second,"object value member identity repeated/invalid");
            resource(object.guid,object.source,object.asset,object.definition_chunk_id);
            need(!object.master_guid||(*object.master_guid&&*object.master_guid!=object.guid),"object value construction master invalid");
            need(member.wear_known||!member.wear,"unknown object value Wear contains a value");
            const auto e=std::find_if(world.entities.begin(),world.entities.end(),[&](const auto& e){return e.id==object.id;});
            need(e!=world.entities.end()&&e->generation==object.generation&&e->kind==EntityKind::object&&!e->ui_ghost&&e->definition&&
                e->definition->guid==object.guid&&e->definition->master_guid==object.master_guid,"object value member is absent, stale or differently sourced");
        }
        if(group.base){
            need(std::any_of(group.members.begin(),group.members.end(),[&](const auto& m){return same(*group.base,{m.object.id,m.object.generation});}),"object value BaseObject is not a member");
            const auto selected=object_value_source_base(group.members);
            need(!selected||same(*selected,*group.base),"object value BaseObject differs from source ordering/lead/offset");
        }
        const auto& origin=group.price_origin;
        need(static_cast<unsigned>(origin.kind)<=static_cast<unsigned>(SessionObjectPriceSource::constructed_catalog),"object value price source invalid");
        need(!group.initial_price||origin.kind!=SessionObjectPriceSource::unknown,"known object value lacks price provenance");
        if(origin.kind!=SessionObjectPriceSource::unknown)resource(origin.guid,origin.source,origin.asset,origin.definition_chunk_id);
        else need(!origin.guid&&origin.source.empty()&&origin.asset.empty()&&!origin.definition_chunk_id&&!origin.objd_sha256&&!origin.writer,
            "unknown object price source contains invented provenance");
        if(origin.objd_sha256)need(std::any_of(origin.objd_sha256->begin(),origin.objd_sha256->end(),[](auto b){return b!=0;}),"object value OBJD digest is empty");
        if(origin.kind==SessionObjectPriceSource::fresh_import_objd)
            need(origin.objd_sha256.has_value(),"fresh object value lacks exact OBJD digest");
        if(origin.kind==SessionObjectPriceSource::current_value_write)
            need(origin.writer&&origin.writer->id>0&&origin.writer->generation&&origin.writer->generation<=world.generation_sequence,
                "CurrentValue price origin lacks valid writer identity");
        else need(!origin.writer,"non-CurrentValue price source has writer identity");
    }
}
ObjectValueRegistry::ObjectValueRegistry(World& world,SessionObjectTradeLedger& ledger):world_(world),ledger_(ledger){
    validate_object_value_groups(ledger_,world_.snapshot());
}
ObjectValueRegistry::~ObjectValueRegistry(){if(bound_){world_.set_current_value_write_observer({});world_.set_current_value_read_resolver({});}}
bool ObjectValueRegistry::bind(){
    if(bound_)return true;
    if(!world_.set_current_value_read_resolver([this](const World&,WorldEntityReference id){
        const auto* group=query({id.id,id.generation});if(!group){
            for(const auto& value:ledger_.groups)for(const auto& member:value.members)
                if(same({id.id,id.generation},{member.object.id,member.object.generation}))return World::CurrentValueReadResult{true,{}};
            return World::CurrentValueReadResult{};
        }
        if(!group->initial_price)return World::CurrentValueReadResult{true,{}};
        const auto bits=static_cast<std::uint16_t>(static_cast<std::uint32_t>(*group->initial_price)&65535U);
        const auto value=bits<=32767?static_cast<std::int16_t>(bits):static_cast<std::int16_t>(std::int32_t(bits)-65536);
        return World::CurrentValueReadResult{true,value};
    }))return false;
    if(!world_.set_current_value_write_observer([this](const World&,WorldEntityReference id,std::int16_t before,std::int16_t after,const auto& store){
        return current_value_write(id,before,after,store);
    })){world_.set_current_value_read_resolver({});return false;}
    bound_=true;return true;
}
const SessionObjectValueGroup* ObjectValueRegistry::query(ObjectTradeIdentity id)const{
    if(ledger_.version!=2||!ledger_.objects.empty())return nullptr;
    for(const auto& group:ledger_.groups)for(const auto& member:group.members)
        if(same(id,{member.object.id,member.object.generation})){
            if(!std::all_of(group.members.begin(),group.members.end(),[&](const auto& m){return live(world_,m);}))return nullptr;
            return &group;
        }
    return nullptr;
}
ObjectTradeValue ObjectValueRegistry::refund(ObjectTradeIdentity id)const{
    const auto* group=query(id);
    if(!group||!group->base)return ts1_object_refund_value({},false,{});
    const auto base=std::find_if(group->members.begin(),group->members.end(),[&](const auto& member){return same(*group->base,{member.object.id,member.object.generation});});
    if(base==group->members.end())return ts1_object_refund_value({},false,{});
    return ts1_object_refund_value(group->initial_price,base->wear_known,base->wear);
}
bool ObjectValueRegistry::migrate_legacy_singletons(){
    if(ledger_.version==2)return ledger_.objects.empty();
    if(ledger_.version!=1||!ledger_.groups.empty()||ledger_.revision)return false;
    try{
        SessionObjectTradeLedger next;next.version=2;next.revision=std::max<std::uint64_t>(1,ledger_.objects.size());
        for(const auto& old:ledger_.objects){
            SessionObjectValueMember member;member.object=old.object;member.wear_known=true;member.wear=old.wear;
            SessionObjectValueGroup group;group.group_id=next.groups.size()+1;group.revision=1;group.members={member};
            group.base=ObjectTradeIdentity{old.object.id,old.object.generation};group.initial_price=old.initial_price;
            group.price_origin={SessionObjectPriceSource::legacy_v1,old.object.guid,old.object.source,old.object.asset,old.object.definition_chunk_id,{},{}};
            next.groups.push_back(std::move(group));
        }
        validate_object_value_groups(next,world_.snapshot());ledger_=std::move(next);return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::admit(SessionObjectValueGroup group){
    if(ledger_.version!=2||!ledger_.objects.empty())return false;
    try{
        auto next=ledger_;const auto revision=next_revision(next);
        if(!group.group_id){
            const auto last=next.groups.empty()?0:next.groups.back().group_id;
            if(last==UINT64_MAX)return false;group.group_id=std::max(last+1,revision);
        }
        group.revision=revision;next.revision=revision;next.groups.push_back(std::move(group));
        std::sort(next.groups.begin(),next.groups.end(),[](const auto& a,const auto& b){return a.group_id<b.group_id;});
        validate_object_value_groups(next,world_.snapshot());ledger_=std::move(next);return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::current_value_write(WorldEntityReference id,std::int16_t before,std::int16_t after,const std::function<bool()>& store){
    const auto* entity=world_.find(id.id);if(!entity||entity->generation!=id.generation)return false;
    const auto word=entity->object_words.find(41);if(word==entity->object_words.end()||word->second!=before)return false;
    const auto* group=query({id.id,id.generation});
    if(!group){
        for(const auto& value:ledger_.groups)for(const auto& member:value.members)
            if(same({id.id,id.generation},{member.object.id,member.object.generation}))return false;
        return store(); // Untracked legacy object does not acquire a false group price.
    }
    try{
        auto staged=*group;const auto revision=next_revision(ledger_);
        const auto member=std::find_if(staged.members.begin(),staged.members.end(),[&](const auto& m){return m.object.id==id.id;});
        staged.initial_price=std::int32_t(after);staged.revision=revision;
        staged.price_origin={SessionObjectPriceSource::current_value_write,member->object.guid,member->object.source,
            member->object.asset,member->object.definition_chunk_id,{},ObjectTradeIdentity{id.id,id.generation}};
        const auto index=static_cast<std::size_t>(group-ledger_.groups.data());
        if(!store())return false;
        using std::swap;swap(ledger_.groups[index],staged);ledger_.revision=revision;return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::set_wear(ObjectTradeIdentity id,bool known,std::optional<std::uint16_t> wear){
    if(!known&&wear)return false;const auto* group=query(id);if(!group)return false;
    try{const auto revision=next_revision(ledger_);auto staged=*group;
        for(auto& member:staged.members)if(same(id,{member.object.id,member.object.generation})){member.wear_known=known;member.wear=wear;}
        staged.revision=revision;const auto index=std::size_t(group-ledger_.groups.data());
        using std::swap;swap(ledger_.groups[index],staged);ledger_.revision=revision;return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::set_purchase_price(ObjectTradeIdentity id,std::int32_t price,SessionObjectPriceOrigin origin){
    const auto* group=query(id);
    if(!group||origin.kind!=SessionObjectPriceSource::purchase||origin.writer)return false;
    try{
        const auto index=std::size_t(group-ledger_.groups.data());auto next=ledger_;
        const auto revision=next_revision(next);
        auto& staged=next.groups[index];staged.initial_price=price;
        staged.price_origin=std::move(origin);staged.revision=revision;next.revision=revision;
        validate_object_value_groups(next,world_.snapshot());
        using std::swap;swap(ledger_,next);return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::split_member(ObjectTradeIdentity id,std::int16_t x,std::int16_t y,
        std::int16_t level,std::optional<std::uint16_t> lead){
    const auto* source=query(id);if(!source)return false;
    if(source->members.size()==1)return true; // Source SetPosition does not split an existing singleton.
    try{
        auto next=ledger_;const auto index=std::size_t(source-ledger_.groups.data());
        const auto revision=next_revision(next);auto& old=next.groups[index];
        const auto selected=std::find_if(old.members.begin(),old.members.end(),[&](const auto& m){return same(id,{m.object.id,m.object.generation});});
        if(selected==old.members.end()||next.groups.size()>=4096)return false;
        auto member=*selected;old.members.erase(selected);old.base=object_value_source_base(old.members);old.revision=revision;
        const auto last=next.groups.empty()?0:next.groups.back().group_id;if(last==UINT64_MAX)return false;
        SessionObjectValueGroup separated;separated.group_id=std::max(last+1,revision);
        separated.multitile=source->multitile;separated.initial_price=source->initial_price;separated.price_origin=source->price_origin;
        member.offset_x=x;member.offset_y=y;member.offset_level=level;member.lead_object=lead;
        separated.members.push_back(std::move(member));separated.base=id;separated.revision=revision;
        next.groups.push_back(std::move(separated));next.revision=revision;
        validate_object_value_groups(next,world_.snapshot());using std::swap;swap(ledger_,next);return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::combine_groups(ObjectTradeIdentity receiver,ObjectTradeIdentity donor,
        const std::vector<ObjectGroupMember>& offsets){
    const auto* into=query(receiver);const auto* from=query(donor);
    if(!into||!from||offsets.size()!=from->members.size()||offsets.size()>64)return false;
    for(std::size_t n=0;n<offsets.size();++n){const auto& supplied=offsets[n];const auto& original=from->members[n].object;
        if(supplied.id!=original.id||supplied.generation!=original.generation)return false;
        if(supplied.origin){const auto& o=*supplied.origin;if(static_cast<unsigned>(o.source_kind)>1||o.guid!=original.guid||o.master_guid!=original.master_guid||
            o.source!=original.source||o.asset!=original.asset||o.definition_chunk_id!=original.definition_chunk_id)return false;}
    }
    if(into==from)return true;
    if(into->members.size()>64-from->members.size())return false;
    try{
        auto next=ledger_;const auto into_index=std::size_t(into-ledger_.groups.data());
        const auto from_index=std::size_t(from-ledger_.groups.data());const auto revision=next_revision(next);
        auto& staged=next.groups[into_index];
        for(std::size_t n=0;n<offsets.size();++n){auto member=from->members[n];
            member.offset_x=offsets[n].offset_x;member.offset_y=offsets[n].offset_y;member.offset_level=offsets[n].offset_level;
            staged.members.push_back(std::move(member));}
        staged.base=object_value_source_base(staged.members);staged.revision=revision;
        next.groups.erase(next.groups.begin()+static_cast<std::ptrdiff_t>(from_index));next.revision=revision;
        validate_object_value_groups(next,world_.snapshot());using std::swap;swap(ledger_,next);return true;
    }catch(const FormatError&){return false;}
}
bool ObjectValueRegistry::retire_member(ObjectTradeIdentity id){
    if(ledger_.version!=2||!ledger_.objects.empty())return false;
    for(std::size_t index=0;index<ledger_.groups.size();++index){const auto& group=ledger_.groups[index];
        if(std::none_of(group.members.begin(),group.members.end(),[&](const auto& m){return same(id,{m.object.id,m.object.generation});}))continue;
        try{auto staged=group;const auto revision=next_revision(ledger_);
            staged.members.erase(std::remove_if(staged.members.begin(),staged.members.end(),[&](const auto& m){return same(id,{m.object.id,m.object.generation});}),staged.members.end());
            if(!std::all_of(staged.members.begin(),staged.members.end(),[&](const auto& m){return live(world_,m);}))return false;
            staged.base=object_value_source_base(staged.members);staged.revision=revision;
            if(staged.members.empty())ledger_.groups.erase(ledger_.groups.begin()+static_cast<std::ptrdiff_t>(index));
            else {using std::swap;swap(ledger_.groups[index],staged);}
            ledger_.revision=revision;return true;
        }catch(const FormatError&){return false;}
    }return false;
}
}
