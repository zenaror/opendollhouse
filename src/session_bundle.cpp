#include "odh/session_bundle.hpp"
#include "odh/seated_session_continuation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <set>
#include <map>

namespace odh {
namespace {
constexpr std::array<char,4> meta_id{{'M','E','T','A'}};
constexpr std::array<char,4> host_id{{'H','O','S','T'}};
constexpr std::array<char,4> household_id{{'H','H','R','D'}};
constexpr std::array<char,4> world_id{{'W','R','L','D'}};
constexpr std::array<char,4> relation_id{{'R','E','L','A'}};
constexpr std::array<char,4> vm_id{{'V','M',' ',' '}};
constexpr std::array<char,4> scheduler_id{{'S','C','H','D'}};
constexpr std::array<char,4> controller_id{{'C','T','R','L'}};
constexpr std::array<char,4> playback_id{{'P','L','A','Y'}};
constexpr std::array<char,4> simulation_id{{'S','I','M','S'}};
constexpr std::array<char,4> seated_id{{'S','E','A','T'}};
constexpr std::array<char,4> route_id{{'R','O','U','T'}};
constexpr std::array<char,4> object_vm_id{{'O','B','V','M'}};
constexpr std::array<char,4> dynamic_owner_id{{'D','Y','N','O'}};
constexpr std::array<char,4> generic_execution_id{{'G','E','X','P'}};
constexpr std::array<char,4> family_lease_id{{'F','A','M','L'}};
constexpr std::array<std::uint8_t,8> host_magic{{'O','D','H','H','S','T','\r','\n'}};
constexpr std::uint16_t payload_version = 1;
static_assert(sizeof(float)==sizeof(std::uint32_t),"session Playback codec requires 32-bit float");
static_assert(std::numeric_limits<float>::is_iec559,"session Playback codec requires IEEE-754 float");

class Writer {
public:
    explicit Writer(std::size_t limit):limit_(limit) {}
    void u8(std::uint8_t value) { append(&value,1); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8));
    }
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void u32(std::uint32_t value) {
        for(unsigned shift=0;shift<32;shift+=8)u8(static_cast<std::uint8_t>(value >> shift));
    }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void u64(std::uint64_t value) {
        for(unsigned shift=0;shift<64;shift+=8)u8(static_cast<std::uint8_t>(value >> shift));
    }
    void append(const std::uint8_t* data,std::size_t size) {
        if(size>limit_||bytes_.size()>limit_-size)
            throw FormatError("session bundle section exceeds byte limit");
        if(size==0)return;
        bytes_.insert(bytes_.end(),data,data+size);
    }
    void append(const std::vector<std::uint8_t>& data) {append(data.data(),data.size());}
    void string(const std::string& value) {
        if(value.size()>4096)throw FormatError("seated continuation text exceeds limits");
        u16(static_cast<std::uint16_t>(value.size()));
        append(reinterpret_cast<const std::uint8_t*>(value.data()),value.size());
    }
    std::vector<std::uint8_t> take() {return std::move(bytes_);}
private:
    std::size_t limit_;
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes):bytes_(bytes) {}
    std::uint8_t u8() {require(1);return bytes_[offset_++];}
    std::uint16_t u16() {
        const auto low=static_cast<std::uint16_t>(u8());
        return static_cast<std::uint16_t>(low|static_cast<std::uint16_t>(u8())<<8);
    }
    std::int16_t i16() {
        const auto bits=u16();
        return static_cast<std::int16_t>(bits<=INT16_MAX?static_cast<std::int32_t>(bits):
            static_cast<std::int32_t>(bits)-65536);
    }
    std::uint32_t u32() {
        std::uint32_t value=0;
        for(unsigned shift=0;shift<32;shift+=8)value|=static_cast<std::uint32_t>(u8())<<shift;
        return value;
    }
    std::int32_t i32() {
        const auto bits=u32();
        return static_cast<std::int32_t>(bits<=INT32_MAX?static_cast<std::int64_t>(bits):
            static_cast<std::int64_t>(bits)-4294967296LL);
    }
    std::uint64_t u64() {
        std::uint64_t value=0;
        for(unsigned shift=0;shift<64;shift+=8)value|=static_cast<std::uint64_t>(u8())<<shift;
        return value;
    }
    std::vector<std::uint8_t> bytes(std::size_t count) {
        require(count);
        auto begin=bytes_.begin()+static_cast<std::ptrdiff_t>(offset_);
        offset_+=count;
        return {begin,begin+static_cast<std::ptrdiff_t>(count)};
    }
    std::size_t remaining() const {return bytes_.size()-offset_;}
    bool done() const {return offset_==bytes_.size();}
    std::string string() {
        const auto size=u16();if(size>4096)throw FormatError("seated continuation text exceeds limits");
        const auto value=bytes(size);return {value.begin(),value.end()};
    }
private:
    void require(std::size_t count) const {
        if(count>remaining())throw FormatError("session bundle section is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_=0;
};

void encode_key(Writer& w,const PlaybackResourceKey& k) {
    w.u8(static_cast<std::uint8_t>(k.kind));w.u32(k.source);w.u16(k.id);w.i16(k.posture);
    w.i16(k.caller);w.i16(k.callee);w.i16(k.stack_object);w.i16(k.actor_age);w.u32(k.code_owner);
}
PlaybackResourceKey decode_key(Reader& r) {
    PlaybackResourceKey k{};const auto kind=r.u8();
    if(kind>static_cast<std::uint8_t>(PlaybackResourceKey::Kind::posture_reset))
        throw FormatError("invalid playback resource kind");
    k.kind=static_cast<PlaybackResourceKey::Kind>(kind);k.source=r.u32();k.id=r.u16();k.posture=r.i16();
    k.caller=r.i16();k.callee=r.i16();k.stack_object=r.i16();k.actor_age=r.i16();k.code_owner=r.u32();return k;
}
void encode_playback_value(Writer& w,const PlaybackSnapshot& p,const SessionBundleLimits& limits) {
    const auto optional=[&](bool value){w.u8(value?1:0);};
    optional(bool(p.state));if(p.state){const auto& s=*p.state;encode_key(w,s.resource);w.i16(s.actor);
        w.u64(s.actor_generation);w.u64(s.vm_generation);std::uint32_t frame=0,speed=0;
        std::memcpy(&frame,&s.frame,sizeof(frame));std::memcpy(&speed,&s.speed,sizeof(speed));w.u32(frame);w.u32(speed);w.u32(s.frames);
        w.u8(s.backwards);w.u8(s.end);w.u8(s.loop);w.u64(s.low);w.u64(s.high);w.u16(s.events_run);w.u64(s.ticks);
        if(s.queue.size()>limits.max_playback_queue||s.queue.size()>UINT16_MAX)throw FormatError("playback queue exceeds limits");
        w.u16(static_cast<std::uint16_t>(s.queue.size()));for(auto item:s.queue)w.i16(item);
        optional(bool(s.wait));if(s.wait){w.u64(s.wait->ticket);w.u8(static_cast<std::uint8_t>(s.wait->bank));
            w.u64(s.wait->index);w.u8(s.wait->expected);}}
    optional(bool(p.carry));if(p.carry)encode_key(w,*p.carry);
    optional(bool(p.hands));if(p.hands){w.u8(static_cast<std::uint8_t>(p.hands->value.left));
        w.u8(static_cast<std::uint8_t>(p.hands->value.right));w.i16(p.hands->actor);
        w.u64(p.hands->actor_generation);w.u64(p.hands->vm_generation);}
    optional(bool(p.last_tick));if(p.last_tick)w.u64(*p.last_tick);
}
PlaybackSnapshot decode_playback_value(Reader& r,const SessionBundleLimits& limits) {
    const auto flag=[&](){const auto v=r.u8();if(v>1)throw FormatError("invalid playback boolean");return v!=0;};
    PlaybackSnapshot p{};if(flag()){PlaybackClipSnapshot s{};s.resource=decode_key(r);s.actor=r.i16();
        s.actor_generation=r.u64();s.vm_generation=r.u64();const auto f=r.u32(),speed=r.u32();
        std::memcpy(&s.frame,&f,sizeof(f));std::memcpy(&s.speed,&speed,sizeof(speed));s.frames=r.u32();s.backwards=flag();s.end=flag();s.loop=flag();
        const auto low=r.u64(),high=r.u64();if(low>SIZE_MAX||high>SIZE_MAX)throw FormatError("playback event cursor exceeds platform bounds");
        s.low=static_cast<std::size_t>(low);s.high=static_cast<std::size_t>(high);s.events_run=r.u16();s.ticks=r.u64();
        const auto count=r.u16();if(count>limits.max_playback_queue||count>r.remaining()/2)throw FormatError("playback queue exceeds bounds");
        for(std::size_t i=0;i<count;++i)s.queue.push_back(r.i16());
        if(flag()){PlaybackWaitSnapshot wait{};wait.ticket=r.u64();const auto bank=r.u8();
            if(bank>static_cast<std::uint8_t>(VmFrameBank::locals))throw FormatError("invalid playback wait bank");
            wait.bank=static_cast<VmFrameBank>(bank);const auto index=r.u64();if(index>SIZE_MAX)throw FormatError("playback wait index exceeds platform bounds");
            wait.index=static_cast<std::size_t>(index);wait.expected=r.u8();s.wait=wait;}p.state=std::move(s);}
    if(flag())p.carry=decode_key(r);
    if(flag()){PlaybackHandsSnapshot h{};const auto left=r.u8(),right=r.u8();
        if(left>3||right>3)throw FormatError("invalid playback hand gesture");
        h.value={static_cast<HandGesture>(left),static_cast<HandGesture>(right)};h.actor=r.i16();
        h.actor_generation=r.u64();h.vm_generation=r.u64();p.hands=h;}
    if(flag())p.last_tick=r.u64();
    if(!r.done())throw FormatError("playback state has trailing data");
    return p;
}
std::vector<std::uint8_t> encode_playback(const PlaybackSnapshot& p,const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(payload_version);encode_playback_value(w,p,limits);return w.take();
}
PlaybackSnapshot decode_playback(const std::vector<std::uint8_t>& bytes,const SessionBundleLimits& limits) {
    Reader r(bytes);if(r.u16()!=payload_version)throw FormatError("unsupported playback snapshot version");
    return decode_playback_value(r,limits);
}

void validate_host(const SessionHostState& host,const SessionBundleLimits& limits) {
    if(host.neighborhood_mode==0||host.neighborhood_mode==6||host.neighborhood_mode>7||
       host.house_number==0||host.resident_guid==0||
       host.runtime_avatar_id<=0||host.global_values.size()>limits.max_host_globals||
       host.global_values.size()>UINT16_MAX)
        throw FormatError("invalid or oversized session host state");
    std::optional<std::uint16_t> previous;
    for(const auto& entry:host.global_values) {
        if(entry.index>INT16_MAX||(previous&&entry.index<=*previous))
            throw FormatError("session globals are not canonical");
        previous=entry.index;
    }
}

std::vector<std::uint8_t> encode_host(const SessionHostState& host,
                                      const SessionBundleLimits& limits,bool household) {
    validate_host(host,limits);
    Writer writer(limits.archive.max_section_bytes);
    writer.append(host_magic.data(),host_magic.size());
    writer.u16(host.family_budget?(household?4:3):(household?2:1));
    writer.u8(host.neighborhood_mode);
    writer.u16(host.house_number);
    writer.u16(host.family_chunk_id);
    writer.i32(host.family_number);
    writer.u32(host.resident_guid);
    writer.i16(host.resident_neighbor_id);
    writer.i16(host.runtime_avatar_id);
    writer.u64(host.tick);
    writer.u64(host.random_seed);
    writer.u16(static_cast<std::uint16_t>(host.global_values.size()));
    for(const auto& entry:host.global_values){writer.u16(entry.index);writer.i16(entry.value);}
    if(host.family_budget)writer.i32(*host.family_budget);
    return writer.take();
}

SessionHostState decode_host(const std::vector<std::uint8_t>& bytes,
                             const SessionBundleLimits& limits,bool& household) {
    Reader reader(bytes);
    if(reader.bytes(host_magic.size())!=std::vector<std::uint8_t>(host_magic.begin(),host_magic.end()))
        throw FormatError("unsupported session host format");
    const auto version=reader.u16();
    if(version<1||version>4)throw FormatError("unsupported session host version");
    household=version==2||version==4;
    const bool has_family_budget=version==3||version==4;
    SessionHostState host;
    host.neighborhood_mode=reader.u8();
    host.house_number=reader.u16();
    host.family_chunk_id=reader.u16();
    host.family_number=reader.i32();
    host.resident_guid=reader.u32();
    host.resident_neighbor_id=reader.i16();
    host.runtime_avatar_id=reader.i16();
    host.tick=reader.u64();
    host.random_seed=reader.u64();
    const auto count=reader.u16();
    if(count>limits.max_host_globals||count>reader.remaining()/4)
        throw FormatError("session globals exceed bounds");
    host.global_values.reserve(count);
    for(std::size_t i=0;i<count;++i)host.global_values.push_back({reader.u16(),reader.i16()});
    if(has_family_budget)host.family_budget=reader.i32();
    if(!reader.done())throw FormatError("session host has trailing data");
    validate_host(host,limits);
    return host;
}

std::vector<std::uint8_t> encode_household(const std::vector<SessionHouseholdMember>& roster,
    const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(1);w.u16(static_cast<std::uint16_t>(roster.size()));
    for(const auto& member:roster){w.i16(member.id);w.u64(member.generation);w.u32(member.guid);
        w.i16(member.neighbor_id);w.u16(member.object_definition_id);w.string(member.character_asset);
        w.append(member.character_fingerprint.data(),member.character_fingerprint.size());}
    return w.take();
}
std::vector<SessionHouseholdMember> decode_household(const std::vector<std::uint8_t>& bytes,
    const SessionBundleLimits& limits) {
    Reader r(bytes);if(r.u16()!=1)throw FormatError("unsupported household roster version");
    const auto count=r.u16();if(!count||count>8||count>limits.max_runtime_entities||count>r.remaining()/52)
        throw FormatError("household roster count exceeds bounds");
    std::vector<SessionHouseholdMember> result;result.reserve(count);
    for(unsigned n=0;n<count;++n){SessionHouseholdMember member;member.id=r.i16();member.generation=r.u64();
        member.guid=r.u32();member.neighbor_id=r.i16();member.object_definition_id=r.u16();
        member.character_asset=r.string();const auto hash=r.bytes(32);
        std::copy(hash.begin(),hash.end(),member.character_fingerprint.begin());result.push_back(std::move(member));}
    if(!r.done())throw FormatError("household roster has trailing data");
    return result;
}
void validate_household(const SessionBundle& bundle) {
    if(bundle.household_roster.empty())return;
    if(bundle.household_roster.size()>8||bundle.household_roster.size()!=bundle.runtime_entities.size())
        throw FormatError("household roster does not cover every runtime resident");
    std::set<std::uint32_t> guids;std::set<std::int16_t> neighbors;std::set<std::string> paths;
    for(std::size_t n=0;n<bundle.household_roster.size();++n){const auto& m=bundle.household_roster[n];
        if(m.id!=bundle.runtime_entities[n].id||!m.generation||!m.guid||m.neighbor_id<=0||!m.object_definition_id||
           m.character_asset.empty()||m.character_asset.size()>4096||m.character_asset.front()=='/'||
           m.character_asset.find('\\')!=std::string::npos||m.character_asset.find(':')!=std::string::npos||
           m.character_asset.find('\0')!=std::string::npos||
           std::all_of(m.character_fingerprint.begin(),m.character_fingerprint.end(),[](auto b){return b==0;})||
           !guids.insert(m.guid).second||!neighbors.insert(m.neighbor_id).second||!paths.insert(m.character_asset).second)
            throw FormatError("household roster identity is invalid or duplicated");
        std::size_t start=0;
        while(start<m.character_asset.size()) {const auto end=m.character_asset.find('/',start);
            const auto part=m.character_asset.substr(start,end==std::string::npos?end:end-start);
            if(part.empty()||part=="."||part=="..")throw FormatError("household character asset path is not canonical");
            if(end==std::string::npos)break;
            start=end+1;if(start==m.character_asset.size())throw FormatError("household character asset path ends in separator");}
        const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& e){return e.id==m.id;});
        if(actor==bundle.world.entities.end()||!actor->definition||actor->generation!=m.generation||actor->definition->guid!=m.guid)
            throw FormatError("household roster differs from World resident identity");
        if(m.id==bundle.host.runtime_avatar_id&&(m.guid!=bundle.host.resident_guid||m.neighbor_id!=bundle.host.resident_neighbor_id))
            throw FormatError("household primary identity differs from HOST");
    }
}

std::vector<std::uint8_t> encode_seated(const SessionSeatedContinuation& c,const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(2);
    w.i16(c.actor_id);w.u64(c.actor_generation);w.i16(c.target_id);w.u64(c.target_generation);
    w.u32(c.target_guid);w.u32(c.target_slot);w.u16(c.action);
    w.u64(c.completion.uid);w.u64(c.completion.ticket);w.u64(c.completion.tick);w.u8(c.completion.result?1:0);
    w.string(c.source);w.string(c.asset);w.u16(c.definition_chunk_id);w.u16(c.tree_table_id);
    w.u16(c.stand_entry_point);w.u16(c.stand_condition);w.u16(c.stand_action);
    w.u8(c.linked_semiglobal?1:0);
    if(c.linked_semiglobal){w.string(c.linked_semiglobal->name);w.string(c.linked_semiglobal->archive);
        w.string(c.linked_semiglobal->entry);}
    w.append(c.sit_routine_fingerprint.data(),32);w.append(c.stand_routine_fingerprint.data(),32);
    w.u8(static_cast<std::uint8_t>(c.code_owners.size()));
    for(const auto& binding:c.code_owners){w.u32(binding.owner);w.string(binding.source);w.string(binding.asset);
        w.u16(binding.root_routine);w.append(binding.routine_fingerprint.data(),32);}
    return w.take();
}
SessionSeatedContinuation decode_seated(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);const auto version=r.u16();if(version!=1&&version!=2)throw FormatError("unsupported seated continuation version");
    SessionSeatedContinuation c;c.actor_id=r.i16();c.actor_generation=r.u64();c.target_id=r.i16();c.target_generation=r.u64();
    c.target_guid=r.u32();c.target_slot=r.u32();c.action=r.u16();
    c.completion.uid=r.u64();c.completion.ticket=r.u64();c.completion.tick=r.u64();const auto result=r.u8();
    if(result>1)throw FormatError("invalid seated completion boolean");
    c.completion.result=result!=0;
    c.source=r.string();c.asset=r.string();c.definition_chunk_id=r.u16();c.tree_table_id=r.u16();
    c.stand_entry_point=r.u16();c.stand_condition=r.u16();c.stand_action=r.u16();
    if(version>=2){const auto linked=r.u8();if(linked>1)throw FormatError("invalid seated semiglobal presence flag");
        if(linked){SessionLinkedSemiglobal value;value.name=r.string();value.archive=r.string();value.entry=r.string();
            c.linked_semiglobal=std::move(value);}}
    const auto digest=[&](auto& value){const auto data=r.bytes(32);std::copy(data.begin(),data.end(),value.begin());};
    digest(c.sit_routine_fingerprint);digest(c.stand_routine_fingerprint);
    const auto count=r.u8();if(count>1)throw FormatError("seated continuation owner count exceeds profile");
    for(unsigned i=0;i<count;++i){SessionActionCodeOwner binding;binding.owner=r.u32();binding.source=r.string();
        binding.asset=r.string();binding.root_routine=r.u16();digest(binding.routine_fingerprint);c.code_owners.push_back(std::move(binding));}
    if(!r.done())throw FormatError("seated continuation has trailing data");
    return c;
}

std::vector<std::uint8_t> encode_household_seated(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    const auto& entries=bundle.household_seated_continuations;
    if(entries.empty()||entries.size()>limits.max_runtime_entities||entries.size()>UINT16_MAX)
        throw FormatError("household seated roster exceeds limits");
    Writer w(limits.archive.max_section_bytes);w.u16(3);w.u16(static_cast<std::uint16_t>(entries.size()));
    for(const auto& c:entries){const auto bytes=encode_seated(c,limits);
        if(bytes.size()>UINT32_MAX)throw FormatError("household seated record exceeds format");
        w.u32(static_cast<std::uint32_t>(bytes.size()));w.append(bytes);}
    return w.take();
}
void decode_household_seated(const std::vector<std::uint8_t>& bytes,SessionBundle& bundle,const SessionBundleLimits& limits) {
    Reader r(bytes);if(r.u16()!=3)throw FormatError("unsupported household seated version");
    const auto count=r.u16();
    if(!count||count>limits.max_runtime_entities||count>bundle.runtime_entities.size())
        throw FormatError("household seated roster exceeds runtime roster");
    for(unsigned i=0;i<count;++i){const auto length=r.u32();
        if(length>limits.archive.max_section_bytes)throw FormatError("household seated record exceeds limit");
        const auto record=r.bytes(length);
        if(record.size()<2||record[0]!=2||record[1]!=0)
            throw FormatError("household seated record requires version 2 fields");
        bundle.household_seated_continuations.push_back(decode_seated(record));}
    if(!r.done())throw FormatError("household seated roster has trailing data");
}

template<class Encoder>
std::vector<std::uint8_t> encode_roster(const std::vector<SessionRuntimeEntity>& entities,
                                        const SessionBundleLimits& limits,Encoder encode) {
    Writer writer(limits.archive.max_section_bytes);
    writer.u16(payload_version);
    writer.u16(static_cast<std::uint16_t>(entities.size()));
    for(const auto& entity:entities) {
        const auto data=encode(entity);
        if(data.size()>UINT32_MAX)throw FormatError("session runtime state exceeds format");
        writer.i16(entity.id);
        writer.u32(static_cast<std::uint32_t>(data.size()));
        writer.append(data);
    }
    return writer.take();
}

template<class Decoder>
void decode_roster(const std::vector<std::uint8_t>& bytes,std::vector<SessionRuntimeEntity>& entities,
                   const SessionBundleLimits& limits,Decoder decode) {
    Reader reader(bytes);
    if(reader.u16()!=payload_version)throw FormatError("unsupported session runtime roster version");
    const auto count=reader.u16();
    if(count==0||count>limits.max_runtime_entities||count>reader.remaining()/6)
        throw FormatError("session runtime roster count exceeds bounds");
    if(entities.empty())entities.resize(count);
    else if(entities.size()!=count)throw FormatError("session runtime roster count mismatch");
    std::int16_t previous=0;
    for(std::size_t i=0;i<count;++i) {
        const auto id=reader.i16();
        const auto size=reader.u32();
        if(id<=previous||size>reader.remaining()||
           (entities[i].id&&entities[i].id!=id))
            throw FormatError("session runtime roster is invalid");
        entities[i].id=id;
        decode(entities[i],reader.bytes(size));
        previous=id;
    }
    if(!reader.done())throw FormatError("session runtime roster has trailing data");
}

// Mirror the value-only preconditions checked by Sleep::valid_snapshot and
// InteractionQueue::valid_snapshot. Their World/VM bindings are rebuilt later.
void validate_controller_vm_links(const SessionRuntimeEntity& entity) {
    const auto& vm=entity.vm;
    const auto& sleep=entity.controllers.sleep;
    const auto& interactions=entity.controllers.interactions;
    constexpr auto max_clock=std::numeric_limits<std::uint32_t>::max();
    if(sleep.job) {
        const auto& job=*sleep.job;
        if(!sleep.actor||sleep.notify_resume||!job.ticket||job.ticket>vm.ticket_sequence||
           job.idle_start>sleep.clock||job.deadline<=sleep.clock||job.deadline>max_clock||
           vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=job.ticket||
           vm.pending->instruction.instruction.opcode!=0||
           vm.pending->context.caller!=sleep.actor->id||vm.stack.empty()||
           job.argument>=vm.stack.back().arguments.size()||job.argument>32767)
            throw FormatError("session Sleep job does not match VM wait");
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto remaining=vm.stack.back().arguments[job.argument];
        if(index!=job.argument||remaining<0||
           job.deadline!=job.idle_start+static_cast<std::uint64_t>(remaining)+1)
            throw FormatError("session Sleep deadline does not match VM argument");
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==0)
        throw FormatError("session VM Sleep wait has no controller job");
    if(sleep.notify_resume&&(!sleep.actor||vm.stack.empty()||
       vm.stack.back().context.caller!=sleep.actor->id||
       (vm.status!=VmStatus::running&&vm.status!=VmStatus::budget_exhausted)))
        throw FormatError("session Sleep notify does not match VM state");

    for(const auto& action:interactions.actions)if(action.check&&
       action.check->scope!=routine_key(action.check->id).scope)
        throw FormatError("session queued interaction check key does not match its routine ID");
    if(vm.interaction_return&&vm.status!=VmStatus::yielded)
        throw FormatError("session interaction return does not match yielded VM status");
    const auto valid_ticket=[&](std::uint64_t ticket){return ticket&&ticket<=vm.ticket_sequence;};
    const auto parent_count=interactions.suspended_active.size();
    if(parent_count>interactions.actions.size()||
       (!interactions.suspended_active.empty()&&!interactions.active)||
       (interactions.profile==InteractionQueueProfile::legacy_fifo&&!interactions.suspended_active.empty()))
        throw FormatError("session nested interaction prefix is invalid");
    std::uint64_t previous_parent_ticket=0;
    for(std::size_t i=0;i<parent_count;++i){
        const auto& parent=interactions.suspended_active[i];
        if(parent.uid!=interactions.actions[i].uid||!valid_ticket(parent.ticket)||
           parent.ticket<=previous_parent_ticket)
            throw FormatError("session suspended interaction parent does not match queue order");
        previous_parent_ticket=parent.ticket;
    }
    if(interactions.active&&(interactions.actions.size()<=parent_count||
       interactions.active->uid!=interactions.actions[parent_count].uid||
       !valid_ticket(interactions.active->ticket)||interactions.active->ticket<=previous_parent_ticket))
        throw FormatError("session active interaction does not match nested queue prefix");
    std::vector<const VmFrame*> interaction_frames;
    for(const auto& frame:vm.stack) {
        if(frame.return_mode!=VmCallReturnMode::interaction_next_tick)continue;
        interaction_frames.push_back(&frame);
    }
    const auto frame_matches=[&](const VmFrame& frame,const InteractionActionSnapshot& action,
                                  std::uint64_t ticket){
        const auto& call=action.call;
        return frame.invocation_ticket==ticket&&
           frame.key.scope==call.key.scope&&frame.key.id==call.key.id&&
           frame.context.caller==call.context.caller&&frame.context.callee==call.context.callee&&
           frame.context.code_owner==call.context.code_owner&&
           frame.arguments.size()>=call.arguments.size();
    };
    bool action_frame=false;
    if(interactions.active){
        const bool returned_unobserved=vm.interaction_return&&!interactions.resume_tick&&
            vm.interaction_return->ticket==interactions.active->ticket;
        const auto expected_frames=parent_count+(returned_unobserved?0:1);
        if(interaction_frames.size()!=expected_frames)
            throw FormatError("session interaction VM frame roster differs from active prefix");
        for(std::size_t i=0;i<interaction_frames.size();++i){
            const auto& saved=i<parent_count?interactions.suspended_active[i]:*interactions.active;
            const auto& action=interactions.actions[i];
            if(!frame_matches(*interaction_frames[i],action,saved.ticket))
                throw FormatError("session interaction VM frame does not match queue key, context, or ticket");
        }
        action_frame=interaction_frames.size()>parent_count;
    }else if(!interaction_frames.empty()){
        throw FormatError("session interaction frame has no active controller prefix");
    }
    if(interactions.active) {
        const auto& active_call=interactions.actions[parent_count].call;
        if(vm.interaction_return&&!interactions.resume_tick&&
           (action_frame||vm.interaction_return->ticket!=interactions.active->ticket||
            vm.interaction_return->code_owner!=active_call.context.code_owner||
            vm.interaction_return->run_generation!=vm.run_generation))
            throw FormatError("session nested interaction return does not match deepest action");
        if(interactions.resume_tick){
            if(!interactions.completion||!vm.interaction_return||
               vm.interaction_return->ticket!=interactions.completion->ticket||
               vm.interaction_return->result!=interactions.completion->result||
               vm.interaction_return->run_generation!=vm.run_generation||
               interactions.completion->ticket<=interactions.active->ticket)
                throw FormatError("session nested parent resume does not match completed child");
        }
    }
    /* active parent and completed child coexist until the scheduled resume tick */
    if(interactions.active) {
        // Value-only session data cannot inspect the BHAV at the saved PC; require
        // the complete active prefix and a runnable VM with no suspended service.
        const bool idle_retry_cut=!interactions.wait&&!interactions.resume_tick&&interactions.idle_start&&
            action_frame&&!vm.pending&&!vm.interaction_return&&!vm.external_frame_return&&
            vm.suspended_pending.empty()&&
            (vm.status==VmStatus::running||vm.status==VmStatus::budget_exhausted);
        if(!valid_ticket(interactions.active->ticket)||
           (!interactions.wait&&!interactions.resume_tick&&interactions.idle_start&&!idle_retry_cut))
            throw FormatError("session active interaction boundary is invalid");
        if(!vm.interaction_return&&!action_frame&&vm.status!=VmStatus::fault) {
            throw FormatError("session active interaction has no VM frame");
        }
    }
    if(interactions.wait) {
        if(interactions.resume_tick||(interactions.active&&!action_frame)||
           !valid_ticket(interactions.wait->ticket)||
           interactions.wait->deadline<=interactions.clock||
           interactions.wait->deadline>max_clock||vm.status!=VmStatus::waiting||
           !vm.pending||vm.pending->ticket!=interactions.wait->ticket||
           vm.pending->context.caller!=entity.id||
           vm.pending->instruction.instruction.opcode!=17||vm.stack.empty())
            throw FormatError("session interaction wait does not match VM pending call");
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto allow=static_cast<unsigned>(operands[2]|(static_cast<unsigned>(operands[3])<<8));
        if(index>=vm.stack.back().arguments.size()||index>32767||allow>1)
            throw FormatError("session interaction wait arguments are invalid");
        const auto remaining=vm.stack.back().arguments[index];
        if(remaining<0)throw FormatError("session interaction wait argument is negative");
        const auto legacy_delay=static_cast<std::uint64_t>(remaining)+1;
        const auto current_delay=allow==1?std::uint64_t{1}:legacy_delay;
        const bool current_deadline=interactions.wait->deadline==interactions.idle_start+current_delay;
        const bool legacy_main_deadline=allow==1&&!interactions.active&&
            interactions.wait->deadline==interactions.idle_start+legacy_delay;
        if(!current_deadline&&!legacy_main_deadline)
            throw FormatError("session interaction wait deadline does not match VM argument");
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==17&&
             vm.pending->context.caller==entity.id)
        throw FormatError("session VM interaction wait has no controller wait");
    if(interactions.completion) {
        const auto& completion=*interactions.completion;
        if(!completion.uid||completion.uid>interactions.sequence||
           !valid_ticket(completion.ticket)||completion.tick>interactions.clock||
           std::any_of(interactions.actions.begin(),interactions.actions.end(),
               [&](const auto& action){return action.uid==completion.uid;}))
            throw FormatError("session interaction completion is invalid");
    }
    if(interactions.resume_tick) {
        if(!interactions.completion||interactions.wait||
           *interactions.resume_tick>max_clock||
           *interactions.resume_tick<=interactions.clock||
           *interactions.resume_tick!=interactions.completion->tick+1||
           !vm.interaction_return||
           vm.interaction_return->ticket!=interactions.completion->ticket||
           vm.interaction_return->result!=interactions.completion->result||
           vm.interaction_return->run_generation!=vm.run_generation||
           (!interactions.active&&!interactions.suspended_active.empty()))
            throw FormatError("session interaction resume does not match VM return");
    }
    if(vm.interaction_return&&!interactions.active&&!interactions.resume_tick)
        throw FormatError("session VM interaction return has no controller action");
}

void write_linked_semiglobal(Writer& w,const std::optional<SessionLinkedSemiglobal>& link) {
    w.u8(link?1:0);
    if(link){w.string(link->name);w.string(link->archive);w.string(link->entry);}
}
std::optional<SessionLinkedSemiglobal> read_linked_semiglobal(Reader& r) {
    const auto present=r.u8();if(present>1)throw FormatError("invalid dynamic owner semiglobal marker");
    if(!present)return {};
    SessionLinkedSemiglobal link;link.name=r.string();link.archive=r.string();link.entry=r.string();
    return link;
}
using ResourceOwnerRoster = std::vector<std::pair<std::int16_t,const std::vector<SessionResourceCodeOwner>*>>;
ResourceOwnerRoster resource_owner_roster(const SessionBundle& bundle) {
    ResourceOwnerRoster result;
    for(const auto& state:bundle.runtime_entities)result.emplace_back(state.id,&state.resource_owners);
    for(const auto& state:bundle.object_threads)result.emplace_back(state.id,&state.resource_owners);
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    return result;
}
std::vector<std::uint8_t> encode_generic_execution(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(1);
    w.u32(bundle.next_code_owner);w.u8(bundle.code_owner_ids_exhausted?1:0);
    const auto roster=resource_owner_roster(bundle);w.u16(static_cast<std::uint16_t>(roster.size()));
    for(const auto& thread:roster){
        w.i16(thread.first);
        CodeOwner primary=0;
        for(const auto& object:bundle.object_threads)if(object.id==thread.first)primary=object.primary_owner;
        w.u32(primary);w.u16(static_cast<std::uint16_t>(thread.second->size()));
        for(const auto& owner:*thread.second){
            w.u32(owner.owner);w.u32(owner.resource_guid);w.string(owner.source);w.string(owner.asset);
            w.u16(owner.definition_chunk_id);w.u16(owner.tree_table_id);w.u16(owner.root_routine);
            w.u8(static_cast<std::uint8_t>(owner.bank_kind));w.u8(static_cast<std::uint8_t>(owner.identity));write_linked_semiglobal(w,owner.linked_semiglobal);
            w.append(owner.routine_fingerprint.data(),owner.routine_fingerprint.size());
        }
    }
    w.u32(static_cast<std::uint32_t>(bundle.created_objects.size()));
    for(const auto& object:bundle.created_objects){
        w.i16(object.id);w.u64(object.generation);w.u32(object.guid);w.string(object.source);w.string(object.asset);
        w.u16(object.definition_chunk_id);
    }
    w.u32(static_cast<std::uint32_t>(bundle.removed_original_objects.size()));
    for(const auto& object:bundle.removed_original_objects){w.i16(object.id);w.u64(object.generation);w.u32(object.guid);}
    w.u32(static_cast<std::uint32_t>(bundle.deferred_removals.size()));
    for(const auto& object:bundle.deferred_removals){w.i16(object.id);w.u64(object.generation);}
    return w.take();
}
void decode_generic_execution(const std::vector<std::uint8_t>& bytes,SessionBundle& bundle,
                              const SessionBundleLimits& limits) {
    Reader r(bytes);if(r.u16()!=1)throw FormatError("unsupported generic execution profile version");
    bundle.next_code_owner=r.u32();const auto exhausted=r.u8();
    if(exhausted>1)throw FormatError("invalid generic code owner exhaustion marker");
    bundle.code_owner_ids_exhausted=exhausted!=0;
    const auto roster=resource_owner_roster(bundle);
    if(r.u16()!=roster.size())throw FormatError("generic execution manifest does not cover every thread");
    for(const auto& thread:roster){
        if(r.i16()!=thread.first)throw FormatError("generic execution thread manifest is reordered or unrelated");
        const auto primary=r.u32();
        const auto count=r.u16();
        if(count>limits.max_resource_owners_per_thread)throw FormatError("generic owner count exceeds limit");
        std::vector<SessionResourceCodeOwner>* owners=nullptr;
        for(auto& state:bundle.runtime_entities)if(state.id==thread.first){
            if(primary)throw FormatError("resident manifest cannot declare an object primary owner");
            owners=&state.resource_owners;
        }
        for(auto& state:bundle.object_threads)if(state.id==thread.first){
            owners=&state.resource_owners;state.primary_owner=primary;
        }
        if(!owners)throw FormatError("generic execution owner manifest has no runtime thread");
        for(unsigned n=0;n<count;++n){
            SessionResourceCodeOwner owner;owner.owner=r.u32();owner.resource_guid=r.u32();
            owner.source=r.string();owner.asset=r.string();owner.definition_chunk_id=r.u16();
            owner.tree_table_id=r.u16();owner.root_routine=r.u16();owner.bank_kind=static_cast<SessionCodeBankKind>(r.u8());
            owner.identity=static_cast<SessionCodeOwnerIdentity>(r.u8());
            owner.linked_semiglobal=read_linked_semiglobal(r);
            const auto hash=r.bytes(owner.routine_fingerprint.size());
            std::copy(hash.begin(),hash.end(),owner.routine_fingerprint.begin());owners->push_back(std::move(owner));
        }
    }
    const auto created=r.u32();
    if(created>limits.world.world.entities)throw FormatError("created object manifest exceeds limit");
    for(std::uint32_t n=0;n<created;++n){
        SessionCreatedObject object;object.id=r.i16();object.generation=r.u64();object.guid=r.u32();
        object.source=r.string();object.asset=r.string();object.definition_chunk_id=r.u16();
        bundle.created_objects.push_back(std::move(object));
    }
    const auto removed=r.u32();
    if(removed>limits.world.world.entities)throw FormatError("removed original manifest exceeds limit");
    for(std::uint32_t n=0;n<removed;++n){
        SessionRemovedOriginalObject object;object.id=r.i16();object.generation=r.u64();object.guid=r.u32();
        bundle.removed_original_objects.push_back(object);
    }
    const auto removals=r.u32();
    if(removals>limits.world.world.entities)throw FormatError("deferred removal manifest exceeds limit");
    for(std::uint32_t n=0;n<removals;++n){
        SessionDeferredRemoval object;object.id=r.i16();object.generation=r.u64();bundle.deferred_removals.push_back(object);
    }
    if(!r.done())throw FormatError("generic execution profile has trailing data");
    bundle.generic_execution=true;
}

std::vector<std::uint8_t> encode_dynamic_owners(const std::vector<SessionRuntimeEntity>& entities,
                                                const SessionBundleLimits& limits) {
    return encode_roster(entities,limits,[&](const auto& entity){
        Writer w(limits.archive.max_section_bytes);
        if(entity.dynamic_action_owners.size()>16)throw FormatError("dynamic action owner roster exceeds profile");
        w.u8(static_cast<std::uint8_t>(entity.dynamic_action_owners.size()));
        for(const auto& owner:entity.dynamic_action_owners){
            w.u32(owner.owner);w.i16(owner.target_id);w.u64(owner.target_generation);w.u32(owner.target_guid);
            w.string(owner.source);w.string(owner.asset);w.u16(owner.definition_chunk_id);
            w.u16(owner.tree_table_id);w.u16(owner.root_routine);write_linked_semiglobal(w,owner.linked_semiglobal);
            w.append(owner.routine_fingerprint.data(),owner.routine_fingerprint.size());
        }
        return w.take();
    });
}
void decode_dynamic_owners(const std::vector<std::uint8_t>& bytes,std::vector<SessionRuntimeEntity>& entities,
                           const SessionBundleLimits& limits) {
    decode_roster(bytes,entities,limits,[&](auto& entity,const auto& data){
        Reader r(data);const auto count=r.u8();if(count>16)throw FormatError("dynamic action owner roster exceeds profile");
        for(unsigned n=0;n<count;++n){SessionDynamicActionOwner owner;owner.owner=r.u32();owner.target_id=r.i16();
            owner.target_generation=r.u64();owner.target_guid=r.u32();owner.source=r.string();owner.asset=r.string();
            owner.definition_chunk_id=r.u16();owner.tree_table_id=r.u16();owner.root_routine=r.u16();
            owner.linked_semiglobal=read_linked_semiglobal(r);const auto digest=r.bytes(owner.routine_fingerprint.size());
            std::copy(digest.begin(),digest.end(),owner.routine_fingerprint.begin());
            entity.dynamic_action_owners.push_back(std::move(owner));}
        if(!r.done())throw FormatError("dynamic action owner entry has trailing bytes");
    });
}

bool supported_route45_instruction(const Instruction& instruction) {
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(
        static_cast<std::uint16_t>(instruction.operands[3])<<8));
    return instruction.opcode==45&&scope<=2&&(instruction.operands[4]&0xFEU)==0&&
        instruction.operands[5]==0&&instruction.operands[6]==0&&instruction.operands[7]==0;
}
void validate_local_route_links(const SessionRuntimeEntity& entity,const Entity& actor,
                                const std::vector<Entity>& world,const SessionBundleLimits& limits) {
    const auto& vm=entity.vm;
    const auto* pending_instruction=vm.pending?&vm.pending->instruction.instruction:nullptr;
    const bool saved_go_here_route=entity.route&&entity.route->call_failure_trees&&pending_instruction&&
        pending_instruction->opcode==27&&
        pending_instruction->operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0};
    const bool route45=pending_instruction&&supported_route45_instruction(*pending_instruction);
    const bool pending_route=route45||saved_go_here_route;
    if(pending_route!=entity.route.has_value())
        throw FormatError("session route wait and local route continuation do not match");
    if(!entity.route)return;
    const auto& route=*entity.route;
    if((route.call_failure_trees&&!saved_go_here_route)||(!route.call_failure_trees&&!route45))
        throw FormatError("session local route does not match its Route45 or Go Here instruction");
    if(vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=route.ticket||
       !route.ticket||route.ticket>vm.ticket_sequence||route.actor!=entity.id||
       route.target==entity.id||route.actor_generation!=actor.generation||
       route.run_generation!=vm.run_generation||!route.same_room_verified||
       route.expected.level!=route.target_position.level)
        throw FormatError("session local Route45 ticket or identity is inconsistent");
    const bool verified_destination_owner=std::any_of(entity.dynamic_action_owners.begin(),
        entity.dynamic_action_owners.end(),[&](const auto& owner){
            return owner.owner==vm.pending->context.code_owner&&owner.target_id==route.target&&
                owner.target_guid==0x7c4&&owner.root_routine==4096&&!owner.linked_semiglobal;
        });
    const bool verified_resource_owner=std::any_of(entity.resource_owners.begin(),entity.resource_owners.end(),
        [&](const auto& owner){return owner.owner==vm.pending->context.code_owner;});
    if(vm.pending->context.caller!=entity.id||vm.pending->context.stack_object!=route.target||
       (vm.pending->context.code_owner!=static_cast<CodeOwner>(entity.id)&&!verified_destination_owner&&!verified_resource_owner)||
       vm.stack.empty()||
       vm.pending->instruction.depth!=vm.stack.size()||vm.pending->context.caller!=vm.stack.back().context.caller||
       vm.pending->context.stack_object!=vm.stack.back().context.stack_object||
       vm.pending->context.code_owner!=vm.stack.back().context.code_owner)
        throw FormatError("session local Route45 context differs from its VM frame (pending caller="+
            std::to_string(vm.pending->context.caller)+", stack="+std::to_string(vm.pending->context.stack_object)+
            ", owner="+std::to_string(vm.pending->context.code_owner)+", depth="+
            std::to_string(vm.pending->instruction.depth)+", frames="+std::to_string(vm.stack.size())+
            ", top caller="+(vm.stack.empty()?std::string("none"):std::to_string(vm.stack.back().context.caller))+
            ", top stack="+(vm.stack.empty()?std::string("none"):std::to_string(vm.stack.back().context.stack_object))+
            ", top owner="+(vm.stack.empty()?std::string("none"):std::to_string(vm.stack.back().context.code_owner))+
            ", actor="+std::to_string(entity.id)+", target="+std::to_string(route.target)+")");
    const auto& interactions=entity.controllers.interactions;
    if(!vm.suspended_pending.empty()||vm.external_frame_return||entity.controllers.sleep.job||
       interactions.wait||(interactions.active.has_value()!=!interactions.actions.empty()))
        throw FormatError("session local Route45 overlaps another pending service");
    if(actor.attachment)throw FormatError("session local Route45 actor is attached");
    if(actor.person_data.empty()||actor.person_data[0]!=0)
        throw FormatError("session local Route45 actor is not standing");
    if(!actor.position||!(*actor.position==route.expected))
        throw FormatError("session local Route45 actor position differs from its progress (actor="+
            (actor.position?std::to_string(actor.position->x)+","+std::to_string(actor.position->y)+","+
                std::to_string(actor.position->level)+","+std::to_string(actor.position->direction):"missing")+
            ", route="+std::to_string(route.expected.x)+","+std::to_string(route.expected.y)+","+
            std::to_string(route.expected.level)+","+std::to_string(route.expected.direction)+")");
    if(actor.static_footprint==route.walking_started)
        throw FormatError("session local Route45 footprint differs from its progress");
    if(!std::isfinite(route.expected_radian_direction)||actor.radian_direction!=route.expected_radian_direction)
        throw FormatError("session local Route45 actor direction differs from its progress");
    if(route.frame.portal_turns||route.frame.wait_time||route.frame.retries_remaining||
       (route.frame.state!=RouteFrameState::initial&&route.frame.state!=RouteFrameState::walking&&
        route.frame.state!=RouteFrameState::turn_only)||route.path.empty()||
       route.path.size()>limits.max_route_points||route.next==0||route.next>route.path.size()||
       route.facing>7||(route.entry_flags<=0&&!(route.call_failure_trees&&route.entry_flags==0))||
       !(route.path[route.next-1]==RoutePoint{route.expected.x,route.expected.y}))
        throw FormatError("session local Route45 path or arrival state is invalid");
    const auto target=std::find_if(world.begin(),world.end(),[&](const auto& value){
        return value.id==route.target&&value.generation==route.target_generation;
    });
    if(target==world.end()||!target->position||!(*target->position==route.target_position))
        throw FormatError("session local Route45 target identity or position is stale");
}

bool valid_playback_key(const PlaybackResourceKey& key) {
    return (key.kind==PlaybackResourceKey::Kind::animation&&((key.source||key.id)&&key.posture==0))||
           (key.kind==PlaybackResourceKey::Kind::posture_reset&&!key.source&&!key.id&&
            (key.posture==1||key.posture==2||key.posture==3));
}

bool nonzero_digest(const std::array<std::uint8_t,32>& digest) {
    return std::any_of(digest.begin(),digest.end(),[](auto value){return value!=0;});
}
bool valid_carry_key(const PlaybackResourceKey& key) {
    return (key.kind==PlaybackResourceKey::Kind::animation&&((key.source||key.id)&&key.posture==0))||
           (key.kind==PlaybackResourceKey::Kind::posture_reset&&!key.source&&!key.id&&key.posture==-1);
}
void validate_playback_links(const SessionRuntimeEntity& entity,const Entity& actor,
                             const SessionHostState& host,const SessionBundleLimits& limits) {
    const auto& p=entity.playback;const auto& vm=entity.vm;
    if(p.last_tick&&*p.last_tick>host.tick)throw FormatError("playback tick is ahead of session tick");
    const auto key_actor_matches=[&](const PlaybackResourceKey& key){
        return key.caller==entity.id&&
               key.actor_age==(actor.person_data.size()>58?actor.person_data[58]:0);
    };
    if(p.state){const auto& s=*p.state;
        const auto terminal_frame=std::trunc(static_cast<double>(s.frame));
        const bool invalid_frame_state=(s.end&&s.loop)||
            (!s.loop&&(s.end?(s.backwards?terminal_frame>=0:terminal_frame<s.frames):
                              (s.backwards?(terminal_frame<0||s.frame>static_cast<float>(s.frames)):
                                            (s.frame<0||terminal_frame>=s.frames))));
        if(!valid_playback_key(s.resource)||!key_actor_matches(s.resource)||
           s.actor!=entity.id||s.actor_generation!=actor.generation||
           s.vm_generation!=vm.run_generation||!std::isfinite(s.frame)||!std::isfinite(s.speed)||
           s.speed<=0||!s.frames||s.frames>limits.max_playback_frames||s.ticks>limits.max_playback_ticks||
           s.events_run>255||s.low>s.high||s.high>limits.max_playback_events||
           s.low>limits.max_playback_events||s.queue.size()>limits.max_playback_queue||
           s.frame<static_cast<float>(std::numeric_limits<std::int32_t>::min())||
           s.frame>static_cast<float>(std::numeric_limits<std::int32_t>::max())||
           invalid_frame_state||
           (s.backwards?s.low!=0:s.high>limits.max_playback_events)||!p.hands||
           (s.wait&&(!s.wait->ticket||s.wait->ticket>vm.ticket_sequence)))
            throw FormatError("session playback state identity or limits are invalid");
        if(s.wait){if(vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=s.wait->ticket||
            vm.pending->context.caller!=entity.id||vm.pending->instruction.instruction.opcode!=44||
            !vm.pending->instruction.depth||vm.pending->instruction.depth>vm.stack.size())
                throw FormatError("session playback wait does not match VM pending opcode 44");
            const auto& ins=vm.pending->instruction.instruction;const auto flags=ins.operands[5];
            const auto mode=static_cast<unsigned>((flags&1U)|((flags>>3)&2U));
            if(mode!=0&&mode!=3)
                throw FormatError("session playback wait uses a nonblocking opcode 44 mode");
            if((flags&0x80U)||s.resource.kind!=PlaybackResourceKey::Kind::animation||
               s.resource.caller!=vm.pending->context.caller||s.resource.callee!=vm.pending->context.callee||
               s.resource.stack_object!=vm.pending->context.stack_object||
               s.resource.code_owner!=vm.pending->context.code_owner)
                throw FormatError("session playback resource context does not match VM pending call");
            const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
            const auto index=static_cast<std::size_t>((flags&32U)?ins.operands[2]:0);
            const auto& frame=vm.stack[vm.pending->instruction.depth-1];
            if(s.wait->bank!=bank||s.wait->index!=index||s.wait->expected!=ins.operands[6]||
               (bank==VmFrameBank::arguments?index>=frame.arguments.size():
                index>=frame.locals.size()&&index>=256))
                throw FormatError("session playback wait operands do not match VM");
            auto resource_id=static_cast<std::uint16_t>(ins.operands[0]|(ins.operands[1]<<8));
            auto resource_source=static_cast<std::uint32_t>(ins.operands[4]);
            if(flags&4U){if(resource_id>=frame.arguments.size())
                    throw FormatError("session playback animation parameter index is invalid");
                resource_id=static_cast<std::uint16_t>(frame.arguments[resource_id]);
                if(!resource_source)resource_source=65536;}
            if(!resource_id||s.resource.id!=resource_id||s.resource.source!=resource_source||s.resource.posture)
                throw FormatError("session playback resource key does not match VM opcode 44");
        }else if(vm.pending&&vm.pending->context.caller==entity.id&&
                 vm.pending->instruction.instruction.opcode==44)
            throw FormatError("session pending animation VM call has no playback wait");
    }else if(vm.pending&&vm.pending->context.caller==entity.id&&
             vm.pending->instruction.instruction.opcode==44)
        throw FormatError("session pending animation VM call has no playback state");
    if(p.carry&&(!valid_carry_key(*p.carry)||!key_actor_matches(*p.carry)))
        throw FormatError("session playback carry identity is invalid");
    if(p.hands){const auto& h=*p.hands;
        if(h.actor!=entity.id||h.actor_generation!=actor.generation||h.vm_generation!=vm.run_generation||
           !valid_hand_gesture(h.value.left)||!valid_hand_gesture(h.value.right))
            throw FormatError("session playback hands identity is invalid");
        if(p.state&&(h.actor!=p.state->actor||h.actor_generation!=p.state->actor_generation||
           h.vm_generation!=p.state->vm_generation))throw FormatError("session playback state and hands disagree");
    }
}

void validate_object_thread_links(const SessionObjectThread& thread,const Entity& object,
                                  const EntityVmSchedulerSnapshot::Entity& scheduled,
                                  const SessionHostState& host) {
    const auto& vm=thread.vm;const auto& sleep=thread.sleep;
    if(object.kind!=EntityKind::object||!object.definition||object.definition->guid!=thread.guid||
       object.generation!=thread.generation||thread.guid==0||thread.id<=0||
       scheduled.id!=thread.id||scheduled.status!=vm.status||scheduled.steps!=vm.steps||
       scheduled.run_generation!=vm.run_generation||sleep.clock!=host.tick||
       !sleep.last_tick||*sleep.last_tick!=host.tick||!sleep.actor||
       sleep.actor->id!=thread.id||sleep.actor->generation!=thread.generation||
       sleep.actor->run_generation!=vm.run_generation)
        throw FormatError("session object thread identity or scheduler state is inconsistent");
    if((thread.code_owners.empty()&&thread.resource_owners.empty())||thread.code_owners.size()>16)
        throw FormatError("session object thread has no bounded code provenance");
    std::uint32_t previous_owner=0;bool found_private_owner=false;
    for(const auto& owner:thread.code_owners){
        if(!owner.owner||owner.owner<=previous_owner||owner.source.empty()||owner.asset.empty()||
           !owner.root_routine||!nonzero_digest(owner.routine_fingerprint))
            throw FormatError("session object code owner provenance is incomplete or unordered");
        found_private_owner|=owner.owner==thread.guid;previous_owner=owner.owner;
    }
    found_private_owner|=std::any_of(thread.resource_owners.begin(),thread.resource_owners.end(),
        [&](const auto& owner){return owner.owner==thread.primary_owner&&owner.resource_guid==thread.guid&&
            owner.bank_kind==SessionCodeBankKind::complete_private;});
    if(!found_private_owner)throw FormatError("session object private code owner is absent");
    const auto owner_is_pinned=[&](CodeOwner owner){return !owner||std::any_of(
        thread.code_owners.begin(),thread.code_owners.end(),[&](const auto& value){return value.owner==owner;})||
        std::any_of(thread.resource_owners.begin(),thread.resource_owners.end(),
            [&](const auto& value){return value.owner==owner;});};
    if(std::any_of(vm.stack.begin(),vm.stack.end(),[&](const auto& frame){
           return !owner_is_pinned(frame.context.code_owner);})||
       (vm.pending&&!owner_is_pinned(vm.pending->context.code_owner))||
       std::any_of(vm.suspended_pending.begin(),vm.suspended_pending.end(),[&](const auto& pending){
           return !owner_is_pinned(pending.context.code_owner);})||
       (vm.interaction_return&&!owner_is_pinned(vm.interaction_return->code_owner))||
       (vm.external_frame_return&&!owner_is_pinned(vm.external_frame_return->code_owner)))
        throw FormatError("session object VM state references an unpinned code owner");
    constexpr auto max_clock=std::numeric_limits<std::uint32_t>::max();
    if(sleep.clock>max_clock||sleep.diagnostic.size()>4096||
       (sleep.job&&(!sleep.job->ticket||sleep.job->ticket>vm.ticket_sequence||sleep.notify_resume||
        sleep.job->idle_start>sleep.clock||sleep.job->deadline<=sleep.clock||
        sleep.job->deadline>max_clock||vm.status!=VmStatus::waiting||!vm.pending||
        vm.pending->ticket!=sleep.job->ticket||vm.pending->instruction.instruction.opcode!=0||
        vm.pending->context.caller!=thread.id||vm.stack.empty()||
        sleep.job->argument>=vm.stack.back().arguments.size()||sleep.job->argument>32767)))
        throw FormatError("session object Sleep state does not match its VM");
    if(sleep.job){
        const auto& operands=vm.pending->instruction.instruction.operands;
        const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
        const auto remaining=vm.stack.back().arguments[sleep.job->argument];
        if(index!=sleep.job->argument||remaining<0||sleep.job->deadline!=sleep.job->idle_start+
           static_cast<std::uint64_t>(remaining)+1)
            throw FormatError("session object Sleep deadline does not match VM argument");
    }
    if(!sleep.job&&vm.pending&&vm.pending->instruction.instruction.opcode==0)
        throw FormatError("session object VM Sleep wait has no controller job");
    if(sleep.notify_resume&&(vm.stack.empty()||vm.stack.back().context.caller!=thread.id||
       (vm.status!=VmStatus::running&&vm.status!=VmStatus::budget_exhausted)))
        throw FormatError("session object Sleep notify does not match VM state");
}

void validate_dynamic_action_owners(const SessionRuntimeEntity& entity,
    const std::vector<Entity>& world,const SessionSeatedContinuation* verified_seated) {
    std::set<CodeOwner> referenced;
    const auto note=[&](CodeOwner owner){if(owner>=0x80000000U)referenced.insert(owner);};
    for(const auto& frame:entity.vm.stack)note(frame.context.code_owner);
    if(entity.vm.pending)note(entity.vm.pending->context.code_owner);
    for(const auto& pending:entity.vm.suspended_pending)note(pending.context.code_owner);
    if(entity.vm.interaction_return)note(entity.vm.interaction_return->code_owner);
    if(entity.vm.external_frame_return)note(entity.vm.external_frame_return->code_owner);
    for(const auto& action:entity.controllers.interactions.actions)note(action.call.context.code_owner);
    if(entity.playback.state)note(entity.playback.state->resource.code_owner);
    if(entity.playback.carry)note(entity.playback.carry->code_owner);
    for(const auto& owner:entity.resource_owners)referenced.erase(owner.owner);
    // SEAT has a separate strict owner roster for the completed Sit carry.
    // Only bindings already verified by validate_seated_session_continuation
    // are handled there; do not misclassify them as Go Here DYNO owners.
    if(verified_seated&&entity.id==verified_seated->actor_id)
        for(const auto& owner:verified_seated->code_owners)referenced.erase(owner.owner);
    if(!referenced.empty()&&!entity.route)
        throw FormatError("dynamic action owners are supported only for a saved local route");
    std::set<CodeOwner> declared;CodeOwner previous=0;
    for(const auto& owner:entity.dynamic_action_owners){
        const auto target=std::find_if(world.begin(),world.end(),[&](const auto& value){return value.id==owner.target_id;});
        const auto scope=routine_key(owner.root_routine).scope;
        const bool link_bad=owner.linked_semiglobal&&
            (owner.linked_semiglobal->name.empty()||owner.linked_semiglobal->archive.empty()||
             owner.linked_semiglobal->entry.empty()||owner.linked_semiglobal->name.find_first_of("/\\.:")!=std::string::npos||
             owner.linked_semiglobal->archive.front()=='/'||owner.linked_semiglobal->archive.find('\\')!=std::string::npos||
             owner.linked_semiglobal->archive.find(':')!=std::string::npos||
             owner.linked_semiglobal->archive.find("..")!=std::string::npos||
             owner.linked_semiglobal->entry!=owner.linked_semiglobal->name+".iff");
        // A private root may need its exact GLOB bank for a TTAB TestFunction
        // or a private routine it calls. Semiglobal roots always require the
        // link; private roots may carry it when the installed owner did.
        const bool root_has_link=scope!=RoutineScope::semiglobal||owner.linked_semiglobal.has_value();
        const bool zero_digest=std::all_of(owner.routine_fingerprint.begin(),owner.routine_fingerprint.end(),
            [](std::uint8_t value){return value==0;});
        if(owner.owner<0x80000000U||owner.owner<=previous||owner.target_id<=0||
           owner.target_id==entity.id||!owner.target_generation||!owner.target_guid||owner.source.empty()||
           owner.asset.empty()||!owner.definition_chunk_id||
           (scope!=RoutineScope::private_object&&scope!=RoutineScope::semiglobal)||!root_has_link||link_bad||
           zero_digest||target==world.end()||target->kind!=EntityKind::object||!target->definition||
           target->generation!=owner.target_generation||target->definition->guid!=owner.target_guid||
           !declared.insert(owner.owner).second)
            throw FormatError("dynamic action owner identity or resource provenance is invalid");
        previous=owner.owner;
    }
    if(referenced!=declared)
        throw FormatError("dynamic action owner roster is missing, excessive or unrelated to the VM");
}

bool valid_resource_text(const std::string& value,bool allow_empty=false) {
    return (allow_empty||!value.empty())&&value.size()<=4096&&value.find('\0')==std::string::npos;
}
bool valid_resource_link(const std::optional<SessionLinkedSemiglobal>& link) {
    return !link||(valid_resource_text(link->name)&&valid_resource_text(link->archive)&&
        valid_resource_text(link->entry)&&link->name.find_first_of("/\\.:")==std::string::npos&&
        link->archive.front()!='/'&&link->archive.find('\\')==std::string::npos&&
        link->archive.find(':')==std::string::npos&&link->archive.find("..")==std::string::npos&&
        link->entry==link->name+".iff");
}
std::set<CodeOwner> referenced_owners(const VmSnapshot& vm) {
    std::set<CodeOwner> result;
    const auto note=[&](CodeOwner owner){if(owner)result.insert(owner);};
    for(const auto& frame:vm.stack)note(frame.context.code_owner);
    if(vm.pending)note(vm.pending->context.code_owner);
    for(const auto& wait:vm.suspended_pending)note(wait.context.code_owner);
    if(vm.interaction_return)note(vm.interaction_return->code_owner);
    if(vm.external_frame_return)note(vm.external_frame_return->code_owner);
    return result;
}
void validate_resource_owners(const std::vector<SessionResourceCodeOwner>& owners,
    const std::set<CodeOwner>& referenced,const std::set<CodeOwner>& implicit,
    const SessionBundleLimits& limits) {
    if(owners.size()>limits.max_resource_owners_per_thread||owners.size()>UINT16_MAX)
        throw FormatError("session resource owner count exceeds bounds");
    std::set<CodeOwner> declared;CodeOwner previous=0;
    for(const auto& owner:owners){
        const auto scope=routine_key(owner.root_routine).scope;
        const bool private_root=scope==RoutineScope::private_object;
        const bool semiglobal_root=scope==RoutineScope::semiglobal;
        const bool kind_valid=owner.bank_kind==SessionCodeBankKind::private_closure||
            owner.bank_kind==SessionCodeBankKind::complete_private||
            owner.bank_kind==SessionCodeBankKind::private_and_semiglobal;
        const bool identity_valid=owner.identity==SessionCodeOwnerIdentity::process?
            owner.owner>=0x80000000U:(owner.identity==SessionCodeOwnerIdentity::resource_guid&&owner.owner==owner.resource_guid);
        if(!identity_valid||!owner.owner||owner.owner<=previous||implicit.count(owner.owner)||!owner.resource_guid||
           !owner.definition_chunk_id||!valid_resource_text(owner.source)||
           !valid_resource_text(owner.asset,true)||!valid_resource_link(owner.linked_semiglobal)||
           !nonzero_digest(owner.routine_fingerprint)||!kind_valid||(!private_root&&!semiglobal_root)||
           (owner.bank_kind==SessionCodeBankKind::private_and_semiglobal?!owner.linked_semiglobal:
                (owner.linked_semiglobal.has_value()||!private_root)))
            throw FormatError("session resource owner provenance is invalid or unordered");
        // The owner manifest records code banks installed in this VM, including
        // banks retained for interactions that will be queued later. A bank
        // need not be named by the VM at this exact tick.
        declared.insert(owner.owner);previous=owner.owner;
    }
    for(const auto owner:referenced)
        if(!implicit.count(owner)&&!declared.count(owner))
            throw FormatError("session execution references an undeclared resource owner");
}
bool same_resource_owner(const SessionResourceCodeOwner& a,const SessionResourceCodeOwner& b) {
    return a.resource_guid==b.resource_guid&&a.source==b.source&&a.asset==b.asset&&
        a.definition_chunk_id==b.definition_chunk_id&&a.tree_table_id==b.tree_table_id&&
        a.root_routine==b.root_routine&&a.bank_kind==b.bank_kind&&a.identity==b.identity&&a.linked_semiglobal==b.linked_semiglobal&&
        a.routine_fingerprint==b.routine_fingerprint;
}
void validate_generic_execution(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    const bool has_resource_owners=std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [](const auto& state){return !state.resource_owners.empty();})||
        std::any_of(bundle.object_threads.begin(),bundle.object_threads.end(),
            [](const auto& state){return !state.resource_owners.empty()||state.primary_owner!=0;});
    if(!bundle.generic_execution){
        if(has_resource_owners||!bundle.created_objects.empty()||!bundle.removed_original_objects.empty()||
           !bundle.deferred_removals.empty()||bundle.next_code_owner!=0x80000000U||bundle.code_owner_ids_exhausted)
            throw FormatError("generic execution values require GEXP profile");
        return;
    }
    if(bundle.runtime_entities.size()+bundle.object_threads.size()>UINT16_MAX)
        throw FormatError("generic execution thread count exceeds format");
    if(bundle.next_code_owner<0x80000000U||
       (bundle.code_owner_ids_exhausted&&bundle.next_code_owner!=UINT32_MAX))
        throw FormatError("generic code owner allocator state is invalid");
    const auto allocated=[&](CodeOwner owner){
        if(owner>=0x80000000U&&!bundle.code_owner_ids_exhausted&&owner>=bundle.next_code_owner)
            throw FormatError("persisted process owner is ahead of allocator cursor");
    };
    for(const auto& state:bundle.runtime_entities){
        for(const auto& owner:state.resource_owners)if(owner.identity==SessionCodeOwnerIdentity::process)allocated(owner.owner);
        for(const auto& owner:state.dynamic_action_owners)allocated(owner.owner);
    }
    for(const auto& state:bundle.object_threads){
        for(const auto& owner:state.resource_owners)if(owner.identity==SessionCodeOwnerIdentity::process)allocated(owner.owner);
        // Legacy OBVM identifies its primary private bank by GUID, not a
        // process allocation. Secondary process owners still consume the cursor.
        for(const auto& owner:state.code_owners)if(owner.owner!=state.guid)allocated(owner.owner);
    }
    std::set<CodeOwner> resident_owners;
    for(const auto& state:bundle.runtime_entities)resident_owners.insert(static_cast<CodeOwner>(state.id));
    std::map<CodeOwner,const SessionDynamicActionOwner*> legacy_actions;
    std::map<CodeOwner,const SessionActionCodeOwner*> legacy_objects;
    for(const auto& state:bundle.runtime_entities)for(const auto& owner:state.dynamic_action_owners){
        if(resident_owners.count(owner.owner))throw FormatError("dynamic owner collides with a resident owner");
        const auto inserted=legacy_actions.emplace(owner.owner,&owner);
        if(!inserted.second){const auto& old=*inserted.first->second;
            if(old.target_guid!=owner.target_guid||old.source!=owner.source||old.asset!=owner.asset||
               old.definition_chunk_id!=owner.definition_chunk_id||old.tree_table_id!=owner.tree_table_id||
               old.root_routine!=owner.root_routine||old.linked_semiglobal!=owner.linked_semiglobal||
               old.routine_fingerprint!=owner.routine_fingerprint)
                throw FormatError("legacy dynamic owner ID has conflicting programs across threads");
        }
    }
    for(const auto& state:bundle.object_threads)for(const auto& owner:state.code_owners){
        if(resident_owners.count(owner.owner)||legacy_actions.count(owner.owner))
            throw FormatError("legacy object owner ID collides with another owner profile");
        const auto inserted=legacy_objects.emplace(owner.owner,&owner);
        if(!inserted.second){const auto& old=*inserted.first->second;
            if(old.source!=owner.source||old.asset!=owner.asset||old.root_routine!=owner.root_routine||
               old.routine_fingerprint!=owner.routine_fingerprint)
                throw FormatError("legacy object owner ID has conflicting programs across threads");
        }
    }
    std::map<CodeOwner,const SessionResourceCodeOwner*> global_owners;
    const auto register_owners=[&](const auto& owners){
        for(const auto& owner:owners){
            if(resident_owners.count(owner.owner)||legacy_actions.count(owner.owner)||legacy_objects.count(owner.owner))
                throw FormatError("resource owner ID collides with an implicit or legacy owner profile");
            const auto inserted=global_owners.emplace(owner.owner,&owner);
            if(!inserted.second&&!same_resource_owner(owner,*inserted.first->second))
                throw FormatError("shared resource owner ID has conflicting programs across threads");
        }
    };
    for(const auto& state:bundle.runtime_entities)register_owners(state.resource_owners);
    for(const auto& state:bundle.object_threads)register_owners(state.resource_owners);
    for(const auto& state:bundle.runtime_entities){
        auto referenced=referenced_owners(state.vm);std::set<CodeOwner> implicit{static_cast<CodeOwner>(state.id)};
        for(const auto& action:state.controllers.interactions.actions)
            if(action.call.context.code_owner)referenced.insert(action.call.context.code_owner);
        if(state.playback.state&&state.playback.state->resource.code_owner)
            referenced.insert(state.playback.state->resource.code_owner);
        if(state.playback.carry&&state.playback.carry->code_owner)referenced.insert(state.playback.carry->code_owner);
        for(const auto& owner:state.dynamic_action_owners)implicit.insert(owner.owner);
        validate_resource_owners(state.resource_owners,referenced,implicit,limits);
    }
    for(const auto& thread:bundle.object_threads){
        std::set<CodeOwner> implicit;
        for(const auto& owner:thread.code_owners)implicit.insert(owner.owner);
        auto referenced=referenced_owners(thread.vm);
        if(thread.primary_owner){
            const auto primary=std::find_if(thread.resource_owners.begin(),thread.resource_owners.end(),
                [&](const auto& owner){return owner.owner==thread.primary_owner;});
            if(primary==thread.resource_owners.end()||primary->resource_guid!=thread.guid||
               primary->bank_kind!=SessionCodeBankKind::complete_private)
                throw FormatError("object primary owner is not its exact complete private resource bank");
            referenced.insert(thread.primary_owner);
        }else if(thread.code_owners.empty())throw FormatError("generic object thread has no explicit primary owner");
        validate_resource_owners(thread.resource_owners,referenced,implicit,limits);
    }
    if(bundle.created_objects.size()>limits.world.world.entities||
       bundle.removed_original_objects.size()>limits.world.world.entities||
       bundle.deferred_removals.size()>limits.world.world.entities)
        throw FormatError("generic execution object provenance exceeds World limits");
    std::int16_t previous=0;
    for(const auto& origin:bundle.created_objects){
        const auto entity=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& value){return value.id==origin.id;});
        if(origin.id<=previous||!origin.generation||origin.generation>bundle.world.generation_sequence||
           !origin.guid||!origin.definition_chunk_id||!valid_resource_text(origin.source)||
           !valid_resource_text(origin.asset,true)||entity==bundle.world.entities.end()||
           entity->kind!=EntityKind::object||!entity->definition||entity->definition->guid!=origin.guid||
           entity->generation!=origin.generation)
            throw FormatError("created object provenance differs from World identity");
        previous=origin.id;
        const auto thread=std::find_if(bundle.object_threads.begin(),bundle.object_threads.end(),
            [&](const auto& value){return value.id==origin.id;});
        if(thread!=bundle.object_threads.end()&&!std::any_of(thread->resource_owners.begin(),thread->resource_owners.end(),
            [&](const auto& owner){return owner.owner==thread->primary_owner&&owner.resource_guid==origin.guid&&owner.source==origin.source&&
                owner.asset==origin.asset&&owner.definition_chunk_id==origin.definition_chunk_id;}))
            throw FormatError("created object Main bank differs from creation resource");
    }
    previous=0;
    for(const auto& removed:bundle.removed_original_objects){
        if(removed.id<=previous||!removed.generation||removed.generation>bundle.world.generation_sequence||!removed.guid||
           std::any_of(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& entity){
               return entity.id==removed.id&&entity.generation==removed.generation;}))
            throw FormatError("removed original object identity is invalid or still live");
        previous=removed.id;
    }
    std::set<std::pair<std::int16_t,std::uint64_t>> removals;
    for(const auto& removal:bundle.deferred_removals){
        const auto entity=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& value){return value.id==removal.id&&value.generation==removal.generation;});
        if(!removals.emplace(removal.id,removal.generation).second||entity==bundle.world.entities.end()||
           entity->kind!=EntityKind::object)
            throw FormatError("deferred object removal target is duplicated, absent or stale");
    }
}

void validate_links(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    validate_host(bundle.host,limits);
    validate_household(bundle);
    validate_generic_execution(bundle,limits);
    validate_seated_session_continuations(bundle);
    if(bundle.runtime_entities.empty()||bundle.runtime_entities.size()>limits.max_runtime_entities||
       bundle.object_threads.size()>limits.max_object_threads||
       bundle.runtime_entities.size()>UINT16_MAX||
       bundle.runtime_entities.size()+bundle.object_threads.size()!=bundle.scheduler.entities.size()||
       (bundle.scheduler.last_tick?*bundle.scheduler.last_tick!=bundle.host.tick:bundle.host.tick!=0)) {
        throw FormatError("session runtime and scheduler do not match (runtime="+
            std::to_string(bundle.runtime_entities.size())+", scheduler="+
            std::to_string(bundle.scheduler.entities.size())+", host_tick="+
            std::to_string(bundle.host.tick)+", scheduler_tick="+
            (bundle.scheduler.last_tick?std::to_string(*bundle.scheduler.last_tick):"unset")+")");
    }
    std::int16_t previous=0;
    bool found_primary=false;
    for(std::size_t i=0;i<bundle.runtime_entities.size();++i) {
        const auto& entity=bundle.runtime_entities[i];
        const auto scheduled=std::lower_bound(bundle.scheduler.entities.begin(),bundle.scheduler.entities.end(),
            entity.id,[](const auto& entry,std::int16_t id){return entry.id<id;});
        if(entity.id<=previous||scheduled==bundle.scheduler.entities.end()||scheduled->id!=entity.id||
           scheduled->status!=entity.vm.status||scheduled->steps!=entity.vm.steps||
           scheduled->run_generation!=entity.vm.run_generation||
           entity.controllers.interactions.actor_id!=entity.id||
           (entity.controllers.sleep.actor&&entity.controllers.sleep.actor->id!=entity.id))
            throw FormatError("session runtime entity state does not match scheduler");
        const auto world_actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const Entity& value){return value.id==entity.id&&value.kind==EntityKind::avatar;});
        if(world_actor==bundle.world.entities.end())
            throw FormatError("session runtime actor is absent from world");
        const auto& interactions=entity.controllers.interactions;
        if(interactions.actor_generation!=world_actor->generation||
           interactions.run_generation!=entity.vm.run_generation)
            throw FormatError("session interaction actor generation does not match world and VM");
        if(const auto& sleeping=entity.controllers.sleep.actor; sleeping&&
           (sleeping->id!=entity.id||sleeping->generation!=world_actor->generation||
            sleeping->run_generation!=entity.vm.run_generation))
            throw FormatError("session sleeping actor does not match world and VM");
        for(const auto& action:interactions.actions) {
            const auto target=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
                [&](const Entity& value){return value.id==action.call.context.callee;});
            if(target==bundle.world.entities.end()||target->generation!=action.target_generation)
                throw FormatError("session queued interaction target is stale");
        }
        validate_controller_vm_links(entity);
        validate_dynamic_action_owners(entity,bundle.world.entities,
            find_seated_session_continuation(bundle,entity.id));
        validate_local_route_links(entity,*world_actor,bundle.world.entities,limits);
        validate_playback_links(entity,*world_actor,bundle.host,limits);
        found_primary|=entity.id==bundle.host.runtime_avatar_id;
        previous=entity.id;
    }
    if(!found_primary)throw FormatError("session primary resident is absent from runtime roster");
    std::int16_t previous_object=0;
    for(const auto& thread:bundle.object_threads){
        const auto resident=std::lower_bound(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
            thread.id,[](const auto& value,std::int16_t id){return value.id<id;});
        if(thread.id<=previous_object||(resident!=bundle.runtime_entities.end()&&resident->id==thread.id))
            throw FormatError("session object thread IDs must be ordered and disjoint from avatars");
        const auto world_object=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& value){return value.id==thread.id;});
        const auto scheduled=std::lower_bound(bundle.scheduler.entities.begin(),bundle.scheduler.entities.end(),
            thread.id,[](const auto& value,std::int16_t id){return value.id<id;});
        if(world_object==bundle.world.entities.end()||scheduled==bundle.scheduler.entities.end()||
           scheduled->id!=thread.id)
            throw FormatError("session object thread is absent from World or Scheduler");
        validate_object_thread_links(thread,*world_object,*scheduled,bundle.host);
        previous_object=thread.id;
    }
    std::vector<std::int16_t> expected_ids;expected_ids.reserve(bundle.runtime_entities.size()+bundle.object_threads.size());
    for(const auto& entity:bundle.runtime_entities)expected_ids.push_back(entity.id);
    for(const auto& thread:bundle.object_threads)expected_ids.push_back(thread.id);
    std::sort(expected_ids.begin(),expected_ids.end());
    for(std::size_t i=0;i<expected_ids.size();++i)
        if(bundle.scheduler.entities[i].id!=expected_ids[i])
            throw FormatError("session scheduler contains an unrepresented VM thread");
    for(const auto& entity:bundle.world.entities)if(!entity.static_footprint){
        if(bundle.generic_execution&&std::any_of(bundle.created_objects.begin(),bundle.created_objects.end(),
            [&](const auto& origin){return origin.id==entity.id&&origin.generation==entity.generation;}))continue;
        const auto route=std::find_if(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
            [&](const auto& runtime){return runtime.id==entity.id&&runtime.route.has_value();});
        const bool go_here_marker=entity.kind==EntityKind::object&&entity.ui_ghost&&entity.position&&entity.definition&&
            entity.definition->guid==0x7c4&&std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
                [&](const auto& runtime){return runtime.route&&runtime.route->target==entity.id&&
                    runtime.route->target_generation==entity.generation&&runtime.route->target_position==*entity.position&&
                    std::any_of(runtime.dynamic_action_owners.begin(),runtime.dynamic_action_owners.end(),
                        [&](const auto& owner){return owner.target_id==entity.id&&owner.target_generation==entity.generation&&
                            owner.target_guid==0x7c4&&owner.root_routine==4096;});});
        if((entity.kind!=EntityKind::avatar||route==bundle.runtime_entities.end())&&!go_here_marker)
            throw FormatError("session has a dynamic footprint without a saved local route");
    }
}

const SessionArchiveSection& required_section(const SessionArchive& archive,
                                               const std::array<char,4>& id) {
    const auto it=std::find_if(archive.sections.begin(),archive.sections.end(),
        [&](const SessionArchiveSection& section){return section.id==id;});
    if(it==archive.sections.end()||!it->required)
        throw FormatError("session bundle is missing a required section");
    return *it;
}
void validate_family_lease(const SessionBundle& bundle) {
    if(!bundle.family_lease)return;
    const auto& lease=*bundle.family_lease;
    const auto zero=[](const auto& hash){return std::all_of(hash.begin(),hash.end(),[](auto b){return b==0;});};
    if(zero(lease.neighborhood_identity)||zero(lease.family_identity)||!lease.occupancy_epoch||
       lease.family_chunk_id!=bundle.host.family_chunk_id||bundle.host.house_number<1||
       bundle.host.house_number>98||bundle.household_roster.empty()||!bundle.generic_execution||
       bundle.seated_continuation)
        throw FormatError("invalid managed family session lease or household profile");
}
std::vector<std::uint8_t> encode_family_lease(const SessionFamilyLease& lease,std::size_t limit) {
    Writer w(limit);w.u16(1);w.u16(lease.family_chunk_id);w.u64(lease.occupancy_epoch);
    w.append(lease.neighborhood_identity.data(),lease.neighborhood_identity.size());
    w.append(lease.family_identity.data(),lease.family_identity.size());return w.take();
}
SessionFamilyLease decode_family_lease(const std::vector<std::uint8_t>& data) {
    if(data.size()!=76)throw FormatError("invalid family lease extent");
    Reader r(data);if(r.u16()!=1)throw FormatError("unsupported family lease version");
    SessionFamilyLease lease;lease.family_chunk_id=r.u16();lease.occupancy_epoch=r.u64();
    for(auto& b:lease.neighborhood_identity)b=r.u8();
    for(auto& b:lease.family_identity)b=r.u8();
    return lease;
}
void validate_simulation(const SessionBundle& bundle) {
    if(!bundle.simulation)return;
    const auto& sim=*bundle.simulation;Ts1Clock clock;
    if(!clock.restore(sim.clock)||sim.clock.ticks!=bundle.host.tick || sim.avatars.size()!=bundle.runtime_entities.size())
        throw FormatError("invalid session simulation clock or roster");
    for(const auto& value:bundle.host.global_values){
        const auto current=clock.global(static_cast<std::int16_t>(value.index));
        if(current && *current!=value.value)throw FormatError("session globals differ from simulation clock");
    }
    for(std::size_t i=0;i<sim.avatars.size();++i) {
        Ts1MotiveDecay decay;
        if(sim.avatars[i].id!=bundle.runtime_entities[i].id || !decay.restore(sim.avatars[i].decay))
            throw FormatError("invalid session motive state");
    }
}
std::vector<std::uint8_t> encode_simulation(const SessionSimulationState& sim,std::size_t limit) {
    Writer w(limit);w.u16(1);const auto& c=sim.clock;
    w.u64(c.ticks);w.u16(c.minute_fraction);w.u8(c.minute);w.u8(c.hour);w.u8(c.day);w.u8(c.month);w.i32(c.year);
    w.u16(static_cast<std::uint16_t>(sim.avatars.size()));
    for(const auto& a:sim.avatars){w.i16(a.id);w.u8(a.decay.last_two_minute_bucket);for(auto f:a.decay.fractions)w.i16(f);}
    return w.take();
}
SessionSimulationState decode_simulation(const std::vector<std::uint8_t>& bytes,std::size_t max_avatars) {
    Reader r(bytes);if(r.u16()!=1)throw FormatError("unsupported session simulation version");
    SessionSimulationState sim;auto& c=sim.clock;c.ticks=r.u64();c.minute_fraction=r.u16();c.minute=r.u8();c.hour=r.u8();c.day=r.u8();c.month=r.u8();c.year=r.i32();
    const auto count=r.u16();if(count>max_avatars)throw FormatError("session simulation roster exceeds bounds");
    for(unsigned i=0;i<count;++i){SessionAvatarMotiveState a;a.id=r.i16();a.decay.last_two_minute_bucket=r.u8();for(auto& f:a.decay.fractions)f=r.i16();sim.avatars.push_back(a);}
    if(!r.done())throw FormatError("session simulation has unclaimed bytes");
    return sim;
}
std::vector<std::uint8_t> encode_local_route(const LocalRouteSnapshot& route,
                                             const SessionBundleLimits& limits) {
    if(route.path.empty()||route.path.size()>limits.max_route_points||
       route.path.size()>UINT32_MAX||route.next>UINT32_MAX)
        throw FormatError("local route snapshot exceeds path limits");
    Writer w(limits.archive.max_section_bytes);
    w.u16(2);w.u64(route.ticket);w.u64(route.actor_generation);w.u64(route.target_generation);
    w.u64(route.run_generation);w.i16(route.actor);w.i16(route.target);
    const auto position=[&](const EntityPosition& value){
        w.i32(value.x);w.i32(value.y);w.i16(value.level);w.u8(value.direction);
    };
    position(route.expected);position(route.target_position);
    std::uint32_t radians=0;std::memcpy(&radians,&route.expected_radian_direction,sizeof(radians));w.u32(radians);
    w.u8(static_cast<std::uint8_t>(route.frame.state));w.u32(route.frame.portal_turns);
    w.u8(route.frame.wait_time?1:0);if(route.frame.wait_time)w.u32(*route.frame.wait_time);
    w.u8(route.frame.retries_remaining?1:0);if(route.frame.retries_remaining)w.u8(*route.frame.retries_remaining);
    w.u32(static_cast<std::uint32_t>(route.path.size()));
    for(const auto& point:route.path){w.i32(point.x);w.i32(point.y);}
    w.u32(static_cast<std::uint32_t>(route.next));w.i16(route.entry_flags);w.u8(route.facing);
    w.u8(route.face_anywhere?1:0);w.u8(route.walking_started?1:0);w.u8(route.same_room_verified?1:0);
    w.u8(route.call_failure_trees?1:0);
    return w.take();
}
LocalRouteSnapshot decode_local_route(const std::vector<std::uint8_t>& bytes,
                                       const SessionBundleLimits& limits) {
    Reader r(bytes);const auto version=r.u16();
    if(version!=1&&version!=2)throw FormatError("unsupported local route snapshot version");
    LocalRouteSnapshot route;route.ticket=r.u64();route.actor_generation=r.u64();
    route.target_generation=r.u64();route.run_generation=r.u64();route.actor=r.i16();route.target=r.i16();
    const auto position=[&](){return EntityPosition{r.i32(),r.i32(),r.i16(),r.u8()};};
    route.expected=position();route.target_position=position();
    const auto radians=r.u32();std::memcpy(&route.expected_radian_direction,&radians,sizeof(radians));
    const auto frame_state=r.u8();
    if(frame_state>static_cast<std::uint8_t>(RouteFrameState::failed))
        throw FormatError("invalid saved route frame state");
    route.frame.state=static_cast<RouteFrameState>(frame_state);route.frame.portal_turns=r.u32();
    const auto has_wait=r.u8();if(has_wait>1)throw FormatError("invalid saved route wait marker");
    if(has_wait)route.frame.wait_time=r.u32();
    const auto has_retries=r.u8();if(has_retries>1)throw FormatError("invalid saved route retry marker");
    if(has_retries)route.frame.retries_remaining=r.u8();
    const auto count=r.u32();
    if(count==0||count>limits.max_route_points||count>r.remaining()/8)
        throw FormatError("saved route path exceeds bounds");
    route.path.reserve(count);for(std::uint32_t n=0;n<count;++n)route.path.push_back({r.i32(),r.i32()});
    route.next=r.u32();route.entry_flags=r.i16();route.facing=r.u8();
    const auto face_anywhere=r.u8(),walking_started=r.u8(),same_room=r.u8();
    const auto call_failure_trees=version>=2?r.u8():0;
    if(face_anywhere>1||walking_started>1||same_room>1||call_failure_trees>1||!r.done())
        throw FormatError("invalid saved route flags or trailing bytes");
    route.face_anywhere=face_anywhere!=0;route.walking_started=walking_started!=0;
    route.same_room_verified=same_room!=0;route.call_failure_trees=call_failure_trees!=0;
    return route;
}
std::vector<std::uint8_t> encode_routes(const std::vector<SessionRuntimeEntity>& entities,
                                        const SessionBundleLimits& limits) {
    return encode_roster(entities,limits,[&](const auto& entity){
        Writer w(limits.archive.max_section_bytes);w.u8(entity.route?1:0);
        if(entity.route)w.append(encode_local_route(*entity.route,limits));
        return w.take();
    });
}
void decode_routes(const std::vector<std::uint8_t>& bytes,std::vector<SessionRuntimeEntity>& entities,
                   const SessionBundleLimits& limits) {
    decode_roster(bytes,entities,limits,[&](auto& entity,const auto& data){
        Reader r(data);const auto has_route=r.u8();
        if(has_route>1)throw FormatError("invalid route roster marker");
        if(has_route){const auto payload=r.bytes(r.remaining());entity.route=decode_local_route(payload,limits);}
        else if(!r.done())throw FormatError("empty route roster entry has trailing bytes");
    });
}
bool recognized(const std::array<char,4>& id) {
    return id==simulation_id||id==meta_id||id==host_id||id==world_id||id==relation_id||
           id==household_id||id==vm_id||id==scheduler_id||id==controller_id||id==playback_id||
           id==seated_id||id==route_id||id==object_vm_id||id==dynamic_owner_id||id==generic_execution_id||id==family_lease_id;
}

void write_sleep_snapshot(Writer& w,const SleepSnapshot& state) {
    w.u8(state.actor?1:0);
    if(state.actor){w.i16(state.actor->id);w.u64(state.actor->generation);w.u64(state.actor->run_generation);}
    w.u8(state.job?1:0);
    if(state.job){w.u64(state.job->ticket);w.u64(state.job->idle_start);w.u64(state.job->deadline);
        w.u64(static_cast<std::uint64_t>(state.job->argument));}
    w.u8(state.last_tick?1:0);if(state.last_tick)w.u64(*state.last_tick);
    w.u64(state.clock);w.u8(state.interrupted?1:0);w.u8(state.notify_resume?1:0);w.string(state.diagnostic);
}
SleepSnapshot read_sleep_snapshot(Reader& r) {
    const auto flag=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid object Sleep flag");return value!=0;};
    SleepSnapshot state;
    if(flag())state.actor=SleepActorSnapshot{r.i16(),r.u64(),r.u64()};
    if(flag()){
        const auto ticket=r.u64(),idle=r.u64(),deadline=r.u64(),argument=r.u64();
        if(argument>SIZE_MAX)throw FormatError("object Sleep argument exceeds platform bounds");
        state.job=SleepJobSnapshot{ticket,idle,deadline,static_cast<std::size_t>(argument)};
    }
    if(flag())state.last_tick=r.u64();
    state.clock=r.u64();state.interrupted=flag();
    state.notify_resume=flag();state.diagnostic=r.string();return state;
}

std::vector<std::uint8_t> encode_object_threads(const std::vector<SessionObjectThread>& threads,
                                                const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(payload_version);
    w.u16(static_cast<std::uint16_t>(threads.size()));
    for(const auto& thread:threads){
        Writer entry(limits.archive.max_section_bytes);entry.u64(thread.generation);entry.u32(thread.guid);
        entry.u8(static_cast<std::uint8_t>(thread.code_owners.size()));
        for(const auto& owner:thread.code_owners){entry.u32(owner.owner);entry.string(owner.source);
            entry.string(owner.asset);entry.u16(owner.root_routine);
            entry.append(owner.routine_fingerprint.data(),owner.routine_fingerprint.size());}
        const auto vm=encode_vm_snapshot(thread.vm,limits.vm);
        if(vm.size()>UINT32_MAX)throw FormatError("object VM snapshot exceeds format");
        entry.u32(static_cast<std::uint32_t>(vm.size()));entry.append(vm);
        write_sleep_snapshot(entry,thread.sleep);
        const auto data=entry.take();if(data.size()>UINT32_MAX)throw FormatError("object thread exceeds format");
        w.i16(thread.id);w.u32(static_cast<std::uint32_t>(data.size()));w.append(data);
    }
    return w.take();
}

std::vector<SessionObjectThread> decode_object_threads(const std::vector<std::uint8_t>& bytes,
                                                        const SessionBundleLimits& limits) {
    Reader r(bytes);if(r.u16()!=payload_version)throw FormatError("unsupported object thread roster version");
    const auto count=r.u16();
    if(!count||count>limits.max_object_threads||count>r.remaining()/6)
        throw FormatError("object thread count exceeds bounds");
    std::vector<SessionObjectThread> result;result.reserve(count);std::int16_t previous=0;
    for(std::uint16_t i=0;i<count;++i){
        SessionObjectThread thread;thread.id=r.i16();const auto size=r.u32();
        if(thread.id<=previous||size>r.remaining())throw FormatError("object thread roster is invalid");
        const auto payload=r.bytes(size);Reader entry(payload);
        thread.generation=entry.u64();thread.guid=entry.u32();
        const auto owners=entry.u8();if(owners>16)throw FormatError("object code owner count exceeds bounds");
        for(std::uint8_t n=0;n<owners;++n){SessionActionCodeOwner owner;owner.owner=entry.u32();
            owner.source=entry.string();owner.asset=entry.string();owner.root_routine=entry.u16();
            const auto digest=entry.bytes(owner.routine_fingerprint.size());
            std::copy(digest.begin(),digest.end(),owner.routine_fingerprint.begin());thread.code_owners.push_back(std::move(owner));}
        const auto vm_size=entry.u32();if(vm_size>entry.remaining())throw FormatError("object VM snapshot exceeds entry");
        thread.vm=decode_vm_snapshot(entry.bytes(vm_size),limits.vm);thread.sleep=read_sleep_snapshot(entry);
        if(!entry.done())throw FormatError("object thread entry has trailing data");
        result.push_back(std::move(thread));previous=result.back().id;
    }
    if(!r.done())throw FormatError("object thread roster has trailing data");
    return result;
}
}

bool same_session_household_roster(const std::vector<SessionHouseholdMember>& a,
    const std::vector<SessionHouseholdMember>& b) {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){
        return x.id==y.id&&x.generation==y.generation&&x.guid==y.guid&&x.neighbor_id==y.neighbor_id&&
            x.object_definition_id==y.object_definition_id&&x.character_asset==y.character_asset&&
            x.character_fingerprint==y.character_fingerprint;});
}

void validate_session_playback_links(const SessionRuntimeEntity& state,const Entity& actor,
    const SessionHostState& host,const SessionBundleLimits& limits) {
    validate_playback_links(state,actor,host,limits);
}

std::vector<std::uint8_t> encode_session_bundle(const SessionBundle& bundle,
                                                 SessionBundleLimits limits) {
    validate_links(bundle,limits);
    validate_simulation(bundle);
    validate_family_lease(bundle);
    SessionArchive archive;
    archive.sections={
        {meta_id,true,encode_session_resource_identity(bundle.resources)},
        {host_id,true,encode_host(bundle.host,limits,!bundle.household_roster.empty())},
        {world_id,true,encode_world_snapshot(bundle.world,limits.world)},
        {relation_id,true,encode_relationship_snapshot(bundle.relationships,limits.relationships)},
        {vm_id,true,encode_roster(bundle.runtime_entities,limits,[&](const auto& entity){
            return encode_vm_snapshot(entity.vm,limits.vm);})},
        {scheduler_id,true,encode_entity_vm_scheduler_snapshot(bundle.scheduler,limits.scheduler)},
        {controller_id,true,encode_roster(bundle.runtime_entities,limits,[&](const auto& entity){
            return encode_entity_thread_controllers_snapshot(entity.controllers,limits.controllers);})},
        {playback_id,true,encode_roster(bundle.runtime_entities,limits,[&](const auto& entity){
            return encode_playback(entity.playback,limits);})}
    };
    if(!bundle.household_roster.empty())archive.sections.push_back({household_id,true,encode_household(bundle.household_roster,limits)});
    if(std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [](const auto& entity){return entity.route.has_value();}))
        archive.sections.push_back({route_id,false,encode_routes(bundle.runtime_entities,limits)});
    if(!bundle.object_threads.empty())
        archive.sections.push_back({object_vm_id,false,encode_object_threads(bundle.object_threads,limits)});
    if(std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [](const auto& entity){return !entity.dynamic_action_owners.empty();}))
        archive.sections.push_back({dynamic_owner_id,false,encode_dynamic_owners(bundle.runtime_entities,limits)});
    if(bundle.seated_continuation)archive.sections.push_back({seated_id,true,encode_seated(*bundle.seated_continuation,limits)});
    if(!bundle.household_seated_continuations.empty())archive.sections.push_back({seated_id,true,encode_household_seated(bundle,limits)});
    if(bundle.simulation)archive.sections.push_back({simulation_id,true,encode_simulation(*bundle.simulation,limits.archive.max_section_bytes)});
    if(bundle.generic_execution)archive.sections.push_back({generic_execution_id,true,encode_generic_execution(bundle,limits)});
    if(bundle.family_lease)archive.sections.push_back({family_lease_id,true,encode_family_lease(*bundle.family_lease,limits.archive.max_section_bytes)});
    return encode_session_archive(archive,limits.archive);
}

SessionBundle decode_session_bundle(const std::vector<std::uint8_t>& bytes,
                                     std::optional<SessionResourceIdentity> expected_resources,
                                     SessionBundleLimits limits) {
    const auto archive=decode_session_archive(bytes,limits.archive);
    for(const auto& section:archive.sections)
        if(section.required&&!recognized(section.id))
            throw FormatError("session bundle has an unknown required section");
    SessionBundle result;
    result.resources=decode_session_resource_identity(required_section(archive,meta_id).data);
    if(expected_resources&&encode_session_resource_identity(*expected_resources)!=
                           encode_session_resource_identity(result.resources))
        throw FormatError("session game resources do not match");
    bool household=false;
    result.host=decode_host(required_section(archive,host_id).data,limits,household);
    const auto roster_section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==household_id;});
    if(household)result.household_roster=decode_household(required_section(archive,household_id).data,limits);
    else if(roster_section!=archive.sections.end())throw FormatError("legacy session cannot contain household roster");
    result.world=decode_world_snapshot(required_section(archive,world_id).data,limits.world);
    result.relationships=decode_relationship_snapshot(required_section(archive,relation_id).data,
                                                       limits.relationships);
    decode_roster(required_section(archive,vm_id).data,result.runtime_entities,limits,
        [&](auto& entity,const auto& data){entity.vm=decode_vm_snapshot(data,limits.vm);});
    result.scheduler=decode_entity_vm_scheduler_snapshot(required_section(archive,scheduler_id).data,
                                                         limits.scheduler);
    decode_roster(required_section(archive,controller_id).data,result.runtime_entities,limits,
        [&](auto& entity,const auto& data){
            entity.controllers=decode_entity_thread_controllers_snapshot(data,limits.controllers);});
    decode_roster(required_section(archive,playback_id).data,result.runtime_entities,limits,
        [&](auto& entity,const auto& data){entity.playback=decode_playback(data,limits);});
    for(const auto& section:archive.sections)if(section.id==route_id){
        if(section.required)throw FormatError("local route continuation section must remain optional");
        decode_routes(section.data,result.runtime_entities,limits);
    }
    for(const auto& section:archive.sections)if(section.id==object_vm_id){
        if(section.required)throw FormatError("object VM continuation section must remain optional");
        result.object_threads=decode_object_threads(section.data,limits);
    }
    for(const auto& section:archive.sections)if(section.id==dynamic_owner_id){
        if(section.required)throw FormatError("dynamic action owner section must remain optional");
        decode_dynamic_owners(section.data,result.runtime_entities,limits);
    }
    for(const auto& section:archive.sections)if(section.id==seated_id){
        if(!section.required)throw FormatError("seated continuation must be a required section");
        Reader seated_version(section.data);
        if(seated_version.u16()==3)decode_household_seated(section.data,result,limits);
        else result.seated_continuation=decode_seated(section.data);
    }
    for(const auto& section:archive.sections)if(section.id==simulation_id){
        if(!section.required)throw FormatError("session simulation section must be required");
        result.simulation=decode_simulation(section.data,limits.max_runtime_entities);
    }
    for(const auto& section:archive.sections)if(section.id==generic_execution_id){
        if(!section.required)throw FormatError("generic execution profile must be required");
        decode_generic_execution(section.data,result,limits);
    }
    for(const auto& section:archive.sections)if(section.id==family_lease_id){
        if(!section.required)throw FormatError("family lease section must be required");
        result.family_lease=decode_family_lease(section.data);
    }
    validate_links(result,limits);
    validate_simulation(result);
    validate_family_lease(result);
    return result;
}

} // namespace odh
