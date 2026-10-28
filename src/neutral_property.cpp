#include "odh/neutral_property.hpp"
#include <algorithm>
#include <climits>
#include <map>
#include <set>

namespace odh {
namespace {
using Id=std::array<char,4>;
constexpr Id profile_id{{'N','P','R','P'}},meta_id{{'M','E','T','A'}},world_id{{'W','R','L','D'}},
    relation_id{{'R','E','L','A'}},ownership_id{{'N','O','W','N'}};
void require(bool ok,const char* message){if(!ok)throw FormatError(message);}
bool nonzero(const std::array<std::uint8_t,32>& hash){return std::any_of(hash.begin(),hash.end(),[](auto b){return b!=0;});}
class Writer {
public:
    explicit Writer(std::size_t limit):limit_(limit){}
    void number(std::uint64_t value,unsigned width) {
        require(width<=limit_-bytes.size(),"neutral property section exceeds byte limit");
        for(unsigned i=0;i<width;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(8*i)));
    }
    void count(std::size_t value){require(value<=4096,"neutral property count exceeds limit");number(value,4);}
    void hash(const std::array<std::uint8_t,32>& value){for(auto b:value)number(b,1);}
    void text(const std::string& value,bool empty=false) {
        require((empty || !value.empty()) && value.size()<=4096 && value.find('\0')==std::string::npos,
            "invalid neutral property resource text");
        number(value.size(),4);for(unsigned char c:value)number(c,1);
    }
    std::vector<std::uint8_t> bytes;
private:std::size_t limit_;
};
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes):bytes_(bytes){}
    std::uint64_t number(unsigned width) {
        require(width<=bytes_.size()-offset_,"truncated neutral property section");std::uint64_t value=0;
        for(unsigned i=0;i<width;++i)value|=static_cast<std::uint64_t>(bytes_[offset_++])<<(8*i);
        return value;
    }
    std::size_t count(){const auto value=number(4);require(value<=4096,"neutral property count exceeds limit");return static_cast<std::size_t>(value);}
    std::int16_t id(){const auto value=number(2);require(value>0 && value<=32767,"invalid neutral property ID");return static_cast<std::int16_t>(value);}
    std::array<std::uint8_t,32> hash(){std::array<std::uint8_t,32> result{};for(auto& b:result)b=static_cast<std::uint8_t>(number(1));return result;}
    bool flag(){const auto value=number(1);require(value<=1,"invalid neutral property flag");return value!=0;}
    std::string text(bool empty=false) {
        const auto size=count();require((empty || size) && size<=bytes_.size()-offset_,"invalid neutral property text length");
        std::string result(bytes_.begin()+static_cast<std::ptrdiff_t>(offset_),bytes_.begin()+static_cast<std::ptrdiff_t>(offset_+size));
        offset_+=size;require(result.find('\0')==std::string::npos,"invalid neutral property resource text");return result;
    }
    void end(){require(offset_==bytes_.size(),"neutral property section has trailing data");}
private:const std::vector<std::uint8_t>& bytes_;std::size_t offset_=0;
};
void write_identity(Writer& w,std::int16_t id,std::uint64_t generation,std::uint32_t guid) {
    w.number(static_cast<std::uint16_t>(id),2);w.number(generation,8);w.number(guid,4);
}
PropertyRetiredEntity read_identity(Reader& r) {
    const auto id=r.id();const auto generation=r.number(8);const auto guid=static_cast<std::uint32_t>(r.number(4));
    return {id,generation,guid};
}
void write_origins(Writer& w,const std::vector<SessionCreatedObject>& origins) {
    w.count(origins.size());for(const auto& o:origins){write_identity(w,o.id,o.generation,o.guid);w.text(o.source);w.text(o.asset,true);w.number(o.definition_chunk_id,2);}
}
std::vector<SessionCreatedObject> read_origins(Reader& r) {
    std::vector<SessionCreatedObject> result;const auto count=r.count();
    for(std::size_t n=0;n<count;++n) {
        const auto o=read_identity(r);const auto source=r.text();const auto asset=r.text(true);const auto chunk=static_cast<std::uint16_t>(r.number(2));
        result.push_back({o.id,o.generation,o.guid,source,asset,chunk});
    }return result;
}
void validate(const NeutralPropertyState& s,const NeutralPropertyLimits& limits) {
    require(s.house_number>0 && s.house_number<=98 && s.source_budget>=0 && s.source_lease.occupancy_epoch &&
        nonzero(s.source_lease.neighborhood_identity) && nonzero(s.source_lease.family_identity) &&
        nonzero(s.checkpoint_sha256) && nonzero(s.installed_house_sha256) && nonzero(s.expected_overlay_sha256),"unbound neutral property identity");
    require(s.pins.architecture==NeutralArchitectureProfile::installed_unchanged,"unsupported neutral property architecture profile");
    require((s.pins.expected_property_revision==0)==!s.pins.expected_property_head.has_value() &&
        (!s.pins.expected_property_head || nonzero(*s.pins.expected_property_head)),"inconsistent neutral property prior head/revision");
    require(s.reserved_ids.size()<=4096 && std::is_sorted(s.reserved_ids.begin(),s.reserved_ids.end()) &&
        std::adjacent_find(s.reserved_ids.begin(),s.reserved_ids.end())==s.reserved_ids.end() &&
        (s.reserved_ids.empty() || s.reserved_ids.front()>0),"invalid neutral property reservations");
    for(const auto id:limits.world.world.reserved_entity_ids)
        require(std::binary_search(s.reserved_ids.begin(),s.reserved_ids.end(),id),"neutral property omits required reader reservation");
    auto wl=limits.world.world;wl.reserved_entity_ids=s.reserved_ids;
    World world(wl,s.world.attribute_policy);require(world.restore_snapshot(s.world),"invalid neutral property World");
    RelationshipState relationships(limits.relationships.relationships);
    require(relationships.restore_snapshot(s.relationships),"invalid neutral property relationships");
    std::map<std::int16_t,const Entity*> live;std::map<std::uint64_t,std::int16_t> generations;
    for(const auto& e:s.world.entities) {
        require(e.kind==EntityKind::object && !e.ui_ghost && e.definition && e.definition->guid,"neutral property contains actor/ghost/unknown identity");
        live.emplace(e.id,&e);generations.emplace(e.generation,e.id);
    }
    require(s.groups.size()<=4096,"neutral property group budget exceeded");
    std::set<std::int16_t> covered;std::size_t members=0;
    for(const auto& group:s.groups) {
        require(!group.members.empty() && group.members.size()<=4096-members &&
            std::find(group.members.begin(),group.members.end(),group.leader)!=group.members.end(),"invalid neutral property group");
        require(group.master_guid?*group.master_guid!=0:group.members.size()==1,"invalid neutral property singleton/master shape");
        members+=group.members.size();for(const auto id:group.members)
            require(live.count(id) && covered.insert(id).second && live.at(id)->definition->master_guid==group.master_guid,
                "neutral property group coverage or identity mismatch");
    }
    require(covered.size()==live.size(),"incomplete neutral property groups");
    for(const auto& entry:s.relationships.sources)if(entry.matrix==RelationshipMatrix::object) {
        require(live.count(entry.id) && live.at(entry.id)->generation==entry.generation,"stale neutral object relationship source");
        for(const auto& pair:entry.pairs)require(pair.target>0 && pair.target<=32767 && live.count(static_cast<std::int16_t>(pair.target)),"stale neutral object relationship target");
    }
    std::map<std::int16_t,std::pair<std::uint64_t,std::uint32_t>> retired;
    const auto identity=[&](std::int16_t id,std::uint64_t generation,std::uint32_t guid) {
        require(id>0 && generation && generation<=s.world.generation_sequence && guid && !live.count(id) &&
            std::binary_search(s.reserved_ids.begin(),s.reserved_ids.end(),id),"invalid or unreserved neutral retirement identity");
        const auto found=retired.emplace(id,std::make_pair(generation,guid));
        require(found.second || found.first->second==std::make_pair(generation,guid),"conflicting neutral retirement origin");
        const auto gen=generations.emplace(generation,id);require(gen.second || gen.first->second==id,"neutral retirement generation collision");
    };
    const auto records=[&](const auto& values){require(values.size()<=4096,"neutral retirement count exceeds limit");std::int16_t previous=0;
        for(const auto& v:values){require(v.id>previous,"neutral retirement IDs not unique/ordered");previous=v.id;identity(v.id,v.generation,v.guid);}};
    records(s.removed_original_objects);records(s.retired_entities);records(s.retired_created_objects);
    std::int16_t previous=0;require(s.created_objects.size()<=4096,"neutral created count exceeds limit");
    for(const auto& o:s.created_objects) {
        require(o.id>previous && live.count(o.id) && live.at(o.id)->generation==o.generation &&
            live.at(o.id)->definition->guid==o.guid && !live.at(o.id)->definition->master_guid && o.definition_chunk_id,
            "neutral creation origin differs from live singleton");previous=o.id;
    }
    std::set<std::int16_t> original_ids;for(const auto& o:s.removed_original_objects)original_ids.insert(o.id);
    for(const auto& o:s.retired_created_objects)require(o.definition_chunk_id && !original_ids.count(o.id),"retired creation overlaps original removal");
}
} // namespace

std::vector<std::uint8_t> encode_neutral_property(const PreparedPropertyCheckpoint& p,
    const NeutralPropertyPins& pins,const NeutralPropertyLimits& limits) {
    NeutralPropertyState s;s.resources=p.resources();s.source_lease=p.lease();s.house_number=p.house_number();s.source_budget=p.budget();
    s.checkpoint_sha256=p.checkpoint_sha256();s.installed_house_sha256=p.installed_house_sha256();s.expected_overlay_sha256=p.expected_overlay_sha256();s.pins=pins;
    s.world=p.world().snapshot();s.relationships=p.relationships().snapshot();s.groups=p.groups();s.reserved_ids=p.world().reserved_entity_ids();
    s.created_objects=p.created_objects();s.retired_created_objects=p.retired_created_objects();s.removed_original_objects=p.removed_original_objects();s.retired_entities=p.retired_entities();
    validate(s,limits);
    Writer header(limits.archive.max_section_bytes);header.number(1,2);header.number(1,2); // format1, STRUCTURAL profile1
    header.number(static_cast<std::uint8_t>(pins.architecture),1);header.number(s.house_number,2);header.number(static_cast<std::uint32_t>(s.source_budget),4);
    header.hash(s.source_lease.neighborhood_identity);header.hash(s.source_lease.family_identity);header.number(s.source_lease.occupancy_epoch,8);header.number(s.source_lease.family_chunk_id,2);
    header.hash(s.checkpoint_sha256);header.hash(s.installed_house_sha256);header.hash(s.expected_overlay_sha256);
    header.number(pins.expected_property_revision,8);header.number(pins.expected_property_head.has_value()?1:0,1);if(pins.expected_property_head)header.hash(*pins.expected_property_head);
    Writer ownership(limits.archive.max_section_bytes);ownership.number(1,2);ownership.count(s.reserved_ids.size());for(auto id:s.reserved_ids)ownership.number(static_cast<std::uint16_t>(id),2);
    ownership.count(s.groups.size());for(const auto& g:s.groups) {
        ownership.number(static_cast<std::uint16_t>(g.leader),2);ownership.number(g.master_guid.has_value()?1:0,1);if(g.master_guid)ownership.number(*g.master_guid,4);
        ownership.count(g.members.size());for(auto id:g.members)ownership.number(static_cast<std::uint16_t>(id),2);
    }
    write_origins(ownership,s.created_objects);write_origins(ownership,s.retired_created_objects);
    ownership.count(s.removed_original_objects.size());for(const auto& o:s.removed_original_objects)write_identity(ownership,o.id,o.generation,o.guid);
    ownership.count(s.retired_entities.size());for(const auto& o:s.retired_entities)write_identity(ownership,o.id,o.generation,o.guid);
    auto world_limits=limits.world;world_limits.world.reserved_entity_ids=s.reserved_ids;
    SessionArchive archive;archive.sections={{profile_id,true,std::move(header.bytes)}, {meta_id,true,encode_session_resource_identity(s.resources)},
        {world_id,true,encode_world_snapshot(s.world,world_limits)}, {relation_id,true,encode_relationship_snapshot(s.relationships,limits.relationships)},
        {ownership_id,true,std::move(ownership.bytes)}};
    return encode_session_archive(archive,limits.archive);
}

NeutralPropertyState decode_neutral_property(const std::vector<std::uint8_t>& bytes,
    const SessionResourceIdentity& resources,const std::array<std::uint8_t,32>& house,const NeutralPropertyLimits& limits) {
    const auto archive=decode_session_archive(bytes,limits.archive);
    require(archive.sections.size()==5,"neutral property requires exactly five sections");
    std::map<Id,const std::vector<std::uint8_t>*> parts;
    for(const auto& section:archive.sections) {
        require(section.required && (section.id==profile_id || section.id==meta_id || section.id==world_id ||
            section.id==relation_id || section.id==ownership_id),"unsupported or optional neutral property section");parts.emplace(section.id,&section.data);
    }
    require(parts.size()==5,"missing neutral property section");NeutralPropertyState s;
    s.resources=decode_session_resource_identity(*parts.at(meta_id));
    require(encode_session_resource_identity(s.resources)==encode_session_resource_identity(resources),"neutral property resources changed");
    Reader h(*parts.at(profile_id));require(h.number(2)==1 && h.number(2)==1,"unsupported neutral property format or structural profile");
    s.pins.architecture=static_cast<NeutralArchitectureProfile>(h.number(1));s.house_number=static_cast<std::uint16_t>(h.number(2));
    const auto budget=h.number(4);require(budget<=INT32_MAX,"invalid neutral source budget");s.source_budget=static_cast<std::int32_t>(budget);
    s.source_lease.neighborhood_identity=h.hash();s.source_lease.family_identity=h.hash();s.source_lease.occupancy_epoch=h.number(8);s.source_lease.family_chunk_id=static_cast<std::uint16_t>(h.number(2));
    s.checkpoint_sha256=h.hash();s.installed_house_sha256=h.hash();s.expected_overlay_sha256=h.hash();
    require(s.installed_house_sha256==house && nonzero(house),"neutral property installed house changed");
    s.pins.expected_property_revision=h.number(8);if(h.flag())s.pins.expected_property_head=h.hash();h.end();
    Reader o(*parts.at(ownership_id));require(o.number(2)==1,"unsupported neutral ownership version");
    const auto reservations=o.count();for(std::size_t n=0;n<reservations;++n)s.reserved_ids.push_back(o.id());
    const auto groups=o.count();std::size_t members=0;
    for(std::size_t n=0;n<groups;++n) {
        SavedMultitileGroup group;group.leader=o.id();if(o.flag())group.master_guid=static_cast<std::uint32_t>(o.number(4));
        const auto count=o.count();require(count<=4096-members,"neutral group members exceed limit");members+=count;
        for(std::size_t m=0;m<count;++m)group.members.push_back(o.id());
        s.groups.push_back(std::move(group));
    }
    s.created_objects=read_origins(o);s.retired_created_objects=read_origins(o);
    const auto removed=o.count();for(std::size_t n=0;n<removed;++n){const auto v=read_identity(o);s.removed_original_objects.push_back({v.id,v.generation,v.guid});}
    const auto retired=o.count();for(std::size_t n=0;n<retired;++n)s.retired_entities.push_back(read_identity(o));o.end();
    auto world_limits=limits.world;world_limits.world.reserved_entity_ids=s.reserved_ids;
    s.world=decode_world_snapshot(*parts.at(world_id),world_limits);s.relationships=decode_relationship_snapshot(*parts.at(relation_id),limits.relationships);
    validate(s,limits);return s;
}
} // namespace odh
