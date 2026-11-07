// SPDX-License-Identifier: MPL-2.0
#include "odh/object_group.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
namespace odh {
namespace {
constexpr std::size_t header_bytes=8,group_bytes=12,member_bytes=16;
bool native_metadata(const ObjectGroupSnapshot& s){return !s.retired_native_members.empty()||std::any_of(s.groups.begin(),s.groups.end(),[](const auto& g){return g.native_head.has_value();});}
std::int32_t signed32(std::uint32_t v){return v<=INT32_MAX?std::int32_t(v):std::int32_t(std::int64_t(v)-4294967296LL);}
std::int16_t signed16(std::uint32_t raw){return static_cast<std::int16_t>(raw<32768?static_cast<std::int32_t>(raw):static_cast<std::int32_t>(raw)-65536);}
bool provenance_text(const std::string& s,bool empty=false){return (empty||!s.empty())&&s.size()<=4096&&s.find('\0')==std::string::npos;}
void structure(const ObjectGroupSnapshot& snapshot,ObjectGroupLimits limits){
    if(snapshot.groups.size()>limits.groups||snapshot.groups.size()>UINT32_MAX)
        throw FormatError("object group count exceeds budget");
    std::size_t members=0;std::int16_t prior=0;std::set<std::int16_t> ids;
    const bool v3=native_metadata(snapshot);
    std::uint64_t bytes=header_bytes+(v3?4:0);
    std::set<WorldEntityReference> exact;
    for(const auto& group:snapshot.groups){
        const bool live=group.kind==ObjectGroupKind::live_association;
        if((!live&&group.kind!=ObjectGroupKind::constructed)||
           (live?group.definition_guid!=0:group.definition_guid==0)||group.base_id<=prior||group.members.empty()||
           group.members.size()>limits.members-members||group.members.size()>UINT32_MAX)
            throw FormatError("invalid or oversized object group");
        prior=group.base_id;members+=group.members.size();bool found_base=false;bytes+=group_bytes+(v3?1:0)+(group.native_head?10:0);
        if(group.native_head&&*group.native_head!=WorldEntityReference{group.members.front().id,group.members.front().generation})
            throw FormatError("native group head differs from authenticated member order");
        std::size_t native_index=0;
        if(!live&&!group.multitile&&group.members.size()!=1)
            throw FormatError("direct singleton group must have exactly one member");
        for(const auto& member:group.members){
            if(member.id<=0||!member.generation||!ids.insert(member.id).second)
                throw FormatError("invalid or repeated object group member identity");
            if(member.id==group.base_id)found_base=true;
            exact.insert({member.id,member.generation});
            if(member.native.has_value()!=group.native_head.has_value())throw FormatError("partial native group metadata");
            if(member.native){const auto expected=native_index+1<group.members.size()?WorldEntityReference{
                group.members[native_index+1].id,group.members[native_index+1].generation}:WorldEntityReference{};
                if(member.native->next!=expected)throw FormatError("native group NEXT differs from authenticated member order");}
            ++native_index;bytes+=member_bytes+(v3?1:0)+(member.native?34:0);
            if(live){
                if(!member.origin)throw FormatError("live association member lacks individual provenance");
                const auto& origin=*member.origin;
                if(static_cast<unsigned>(origin.source_kind)>1||!origin.guid||!origin.definition_chunk_id||
                   !provenance_text(origin.source)||!provenance_text(origin.asset,true)||
                   (origin.master_guid&&(!*origin.master_guid||*origin.master_guid==origin.guid)))
                    throw FormatError("invalid live association member provenance");
                bytes+=12+origin.source.size()+origin.asset.size()+(origin.master_guid?4:0);
            }else if(member.origin)throw FormatError("constructed group contains live association provenance");
            if(bytes>limits.payload_bytes)throw FormatError("object group payload exceeds byte budget");
        }
        if(!found_base)throw FormatError("object group base is absent from its ordered membership");
    }
    if(snapshot.retired_native_members.size()>limits.members-members)throw FormatError("native retired member budget exceeded");
    WorldEntityReference previous{};
    for(const auto& retired:snapshot.retired_native_members){
        if(retired.reference.id<=0||!retired.reference.generation||!(previous<retired.reference)||
           !exact.insert(retired.reference).second||retired.fields.next!=WorldEntityReference{})
            throw FormatError("invalid native retired member identity or unlink field");
        previous=retired.reference;bytes+=44;
    }
    if(bytes>limits.payload_bytes)throw FormatError("object group payload exceeds byte budget");
}
}
std::optional<NativeObjectGroupMember> native_object_group_constructor_fields(const ObjectDefinition& definition){
    // 4409f4..440a05/440b44 choose the native class from master_id.
    if(!definition.master_id)return {};
    if(definition.sub_index==-1)throw FormatError("native MT constructor cannot materialize an abstract master");
    if(!definition.level_offset)throw FormatError("native MT constructor level word is unavailable");
    const auto bits=std::uint16_t(definition.sub_index);
    const auto high=std::uint16_t(bits>>8);
    NativeObjectGroupMember result;
    // 4318eb: MOVSX WORD + SAR8 for X, ANDff for Y. Z is MOVSX WORD at4228d0.
    result.authored={high<128?std::int32_t(high):std::int32_t(high)-256,
        std::int32_t(bits&255),signed16(*definition.level_offset)};
    return result;
}
void validate_object_groups(const ObjectGroupSnapshot& snapshot,const WorldSnapshot& world,ObjectGroupLimits limits){
    structure(snapshot,limits);
    std::map<std::int16_t,const Entity*> entities;
    for(const auto& entity:world.entities)
        if(!entities.emplace(entity.id,&entity).second)throw FormatError("ambiguous World object group member ID");
    for(const auto& group:snapshot.groups)for(const auto& member:group.members){
        const auto found=entities.find(member.id);
        if(found==entities.end())throw FormatError("object group member is not in live World");
        const auto& entity=*found->second;
        if(entity.generation!=member.generation||entity.kind!=EntityKind::object||entity.dead||entity.ui_ghost||!entity.definition)
            throw FormatError("object group member generation/kind/resource is unavailable");
        if(group.kind==ObjectGroupKind::live_association){
            if(entity.definition->guid!=member.origin->guid||entity.definition->master_guid!=member.origin->master_guid)
                throw FormatError("live association member resource differs from World");
        }else if(group.multitile){
            if(entity.definition->master_guid!=std::optional<std::uint32_t>{group.definition_guid})
                throw FormatError("object group member has another master resource");
        }else if(entity.definition->guid!=group.definition_guid||entity.definition->master_guid)
            throw FormatError("direct singleton group has another resource or a master override");
    }
    for(const auto& retired:snapshot.retired_native_members){
        const auto body=std::find_if(world.retained_entities.begin(),world.retained_entities.end(),[&](const auto& e){return WorldEntityReference{e.id,e.generation}==retired.reference;});
        if(body==world.retained_entities.end()||!body->dead||body->kind!=EntityKind::object||!body->definition)
            throw FormatError("native retired group cursor lacks its exact retained object body");
    }
}
ObjectGroupSnapshot capture_object_groups(std::vector<ObjectGroup> groups,const WorldSnapshot& world,ObjectGroupLimits limits){
    std::sort(groups.begin(),groups.end(),[](const auto& a,const auto& b){return a.base_id<b.base_id;});
    ObjectGroupSnapshot snapshot{std::move(groups)};validate_object_groups(snapshot,world,limits);return snapshot;
}
ObjectGroupSnapshot capture_object_group_snapshot(ObjectGroupSnapshot snapshot,const WorldSnapshot& world,ObjectGroupLimits limits){
    std::sort(snapshot.groups.begin(),snapshot.groups.end(),[](const auto& a,const auto& b){return a.base_id<b.base_id;});
    validate_object_groups(snapshot,world,limits);return snapshot;
}
std::vector<std::uint8_t> encode_object_group_snapshot(const ObjectGroupSnapshot& snapshot,ObjectGroupLimits limits){
    structure(snapshot,limits);std::vector<std::uint8_t> bytes;
    const auto u16=[&](std::uint16_t v){bytes.push_back(static_cast<std::uint8_t>(v));bytes.push_back(static_cast<std::uint8_t>(v>>8));};
    const auto u32=[&](std::uint32_t v){for(unsigned n=0;n<4;++n)bytes.push_back(static_cast<std::uint8_t>(v>>(8*n)));};
    const auto u64=[&](std::uint64_t v){for(unsigned n=0;n<8;++n)bytes.push_back(static_cast<std::uint8_t>(v>>(8*n)));};
    const auto text=[&](const std::string& s){u16(static_cast<std::uint16_t>(s.size()));bytes.insert(bytes.end(),s.begin(),s.end());};
    const auto reference=[&](WorldEntityReference r){u16(std::uint16_t(r.id));u64(r.generation);};
    const auto native=[&](const NativeObjectGroupMember& m){for(const auto& o:{m.authored,m.current}){u32(std::uint32_t(o.x));u32(std::uint32_t(o.y));u32(std::uint32_t(o.level));}reference(m.next);};
    const bool v3=native_metadata(snapshot);
    const bool v2=std::any_of(snapshot.groups.begin(),snapshot.groups.end(),[](const auto& g){return g.kind==ObjectGroupKind::live_association;});
    u32(v3?3:v2?2:1);u32(static_cast<std::uint32_t>(snapshot.groups.size()));
    for(const auto& group:snapshot.groups){u32(group.definition_guid);bytes.push_back(group.multitile?1:0);bytes.push_back(static_cast<std::uint8_t>(group.kind));
        u16(static_cast<std::uint16_t>(group.base_id));u32(static_cast<std::uint32_t>(group.members.size()));
        if(v3){bytes.push_back(group.native_head?1:0);if(group.native_head)reference(*group.native_head);}
        for(const auto& member:group.members){u16(static_cast<std::uint16_t>(member.id));u64(member.generation);
            u16(static_cast<std::uint16_t>(member.offset_x));u16(static_cast<std::uint16_t>(member.offset_y));u16(static_cast<std::uint16_t>(member.offset_level));
            if(group.kind==ObjectGroupKind::live_association){const auto& origin=*member.origin;
                bytes.push_back(static_cast<std::uint8_t>(origin.source_kind));u32(origin.guid);bytes.push_back(origin.master_guid?1:0);
                if(origin.master_guid)u32(*origin.master_guid);u16(origin.definition_chunk_id);text(origin.source);text(origin.asset);}
            if(v3){bytes.push_back(member.native?1:0);if(member.native)native(*member.native);}
        }}
    if(v3){u32(std::uint32_t(snapshot.retired_native_members.size()));for(const auto& m:snapshot.retired_native_members){reference(m.reference);native(m.fields);}}
    return bytes;
}
ObjectGroupSnapshot decode_object_group_snapshot(const std::vector<std::uint8_t>& bytes,ObjectGroupLimits limits){
    if(bytes.size()>limits.payload_bytes)throw FormatError("object group payload exceeds byte budget");
    std::string raw(bytes.begin(),bytes.end());std::istringstream input(raw,std::ios::binary);const View view(input,raw.size());std::uint64_t cursor=0;
    const auto u16=[&](){const auto value=view.le16(cursor);cursor+=2;return value;};
    const auto u32=[&](){const auto value=view.le32(cursor);cursor+=4;return value;};
    const auto u64=[&](){std::uint64_t value=0;for(unsigned n=0;n<8;++n)value|=static_cast<std::uint64_t>(view.u8(cursor++))<<(8*n);return value;};
    const auto text=[&](){const auto size=u16();if(size>4096)throw FormatError("object group provenance text exceeds budget");
        std::string s;s.reserve(size);for(std::uint16_t i=0;i<size;++i)s.push_back(static_cast<char>(view.u8(cursor++)));return s;};
    const auto version=u32();if(version!=1&&version!=2&&version!=3)throw FormatError("unsupported object group snapshot version");
    const auto count=u32();if(count>limits.groups||count>(view.size()-cursor)/group_bytes)throw FormatError("invalid object group snapshot count");
    const auto reference=[&](){return WorldEntityReference{signed16(u16()),u64()};};
    const auto native=[&](){NativeObjectGroupMember m;for(auto* o:{&m.authored,&m.current}){o->x=signed32(u32());o->y=signed32(u32());o->level=signed32(u32());}m.next=reference();return m;};
    ObjectGroupSnapshot snapshot;snapshot.groups.reserve(count);std::size_t members=0;bool has_live=false;
    for(std::uint32_t n=0;n<count;++n){ObjectGroup group;group.definition_guid=u32();
        const auto flags=view.u8(cursor++),kind=view.u8(cursor++);if(flags>1||kind>(version==1?0:1))throw FormatError("invalid object group flags/kind");
        group.multitile=flags==1;group.kind=static_cast<ObjectGroupKind>(kind);has_live=has_live||group.kind==ObjectGroupKind::live_association;
        group.base_id=signed16(u16());const auto size=u32();
        if(version==3){const auto flag=view.u8(cursor++);if(flag>1)throw FormatError("invalid native head flag");if(flag)group.native_head=reference();}
        if(size>limits.members-members||size>(view.size()-cursor)/member_bytes)throw FormatError("invalid object group membership extent");
        members+=size;group.members.reserve(size);
        for(std::uint32_t i=0;i<size;++i){ObjectGroupMember member;member.id=signed16(u16());member.generation=u64();
            member.offset_x=signed16(u16());member.offset_y=signed16(u16());member.offset_level=signed16(u16());
            if(group.kind==ObjectGroupKind::live_association){ObjectGroupMemberOrigin origin;origin.source_kind=static_cast<ObjectGroupMemberSource>(view.u8(cursor++));origin.guid=u32();
                const auto master=view.u8(cursor++);if(master>1)throw FormatError("invalid object group member master flag");if(master)origin.master_guid=u32();
                origin.definition_chunk_id=u16();origin.source=text();origin.asset=text();member.origin=std::move(origin);}
            if(version==3){const auto flag=view.u8(cursor++);if(flag>1)throw FormatError("invalid native member flag");if(flag)member.native=native();}
            group.members.push_back(std::move(member));}
        snapshot.groups.push_back(std::move(group));
    }
    if(version==3){const auto count=u32();if(count>limits.members-members||count>(view.size()-cursor)/44)throw FormatError("invalid native retired extent");
        snapshot.retired_native_members.reserve(count);for(std::uint32_t i=0;i<count;++i)snapshot.retired_native_members.push_back({reference(),native()});}
    if(cursor!=view.size()||(version==2&&!has_live)||(version==3&&!native_metadata(snapshot)))throw FormatError("trailing or noncanonical object group snapshot bytes");structure(snapshot,limits);return snapshot;
}
}
