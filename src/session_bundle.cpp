#include "odh/session_bundle.hpp"
#include "odh/seated_session_continuation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <set>

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

    const auto valid_ticket=[&](std::uint64_t ticket){return ticket&&ticket<=vm.ticket_sequence;};
    bool action_frame=false;
    for(const auto& frame:vm.stack) {
        if(frame.return_mode!=VmCallReturnMode::interaction_next_tick)continue;
        if(!interactions.active||interactions.actions.empty()||
           frame.invocation_ticket!=interactions.active->ticket)
            throw FormatError("session interaction frame has no active controller action");
        const auto& call=interactions.actions.front().call;
        if(frame.key.scope!=call.key.scope||frame.key.id!=call.key.id||
           frame.context.caller!=call.context.caller||frame.context.callee!=call.context.callee||
           frame.context.code_owner!=call.context.code_owner||
           frame.arguments.size()<call.arguments.size())
            throw FormatError("session active interaction frame does not match queued call");
        action_frame=true;
    }
    if(interactions.active) {
        if(interactions.actions.empty()||
           interactions.active->uid!=interactions.actions.front().uid||
           !valid_ticket(interactions.active->ticket)||interactions.wait||
           interactions.resume_tick||interactions.idle_start)
            throw FormatError("session active interaction is invalid");
        if(vm.interaction_return) {
            if(action_frame||vm.interaction_return->ticket!=interactions.active->ticket||
               vm.interaction_return->code_owner!=
                   interactions.actions.front().call.context.code_owner)
                throw FormatError("session interaction return does not match active action");
        }else if(!action_frame&&vm.status!=VmStatus::fault)
            throw FormatError("session active interaction has no VM frame");
    }
    if(interactions.wait) {
        if(interactions.active||interactions.resume_tick||
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
        if(remaining<0||interactions.wait->deadline!=interactions.idle_start+
           (allow==1?1:static_cast<std::uint64_t>(remaining)+1))
            throw FormatError("session interaction wait deadline does not match VM argument");
    }else if(vm.pending&&vm.pending->instruction.instruction.opcode==17&&
             vm.pending->context.caller==entity.id)
        throw FormatError("session VM interaction wait has no controller wait");
    if(interactions.completion) {
        const auto& completion=*interactions.completion;
        if(!completion.uid||completion.uid>interactions.sequence||
           !valid_ticket(completion.ticket)||completion.tick>interactions.clock||
           (!interactions.actions.empty()&&completion.uid>=interactions.actions.front().uid))
            throw FormatError("session interaction completion is invalid");
    }
    if(interactions.resume_tick) {
        if(!interactions.completion||interactions.active||interactions.wait||
           *interactions.resume_tick>max_clock||
           *interactions.resume_tick<=interactions.clock||
           *interactions.resume_tick!=interactions.completion->tick+1||
           !vm.interaction_return||
           vm.interaction_return->ticket!=interactions.completion->ticket||
           vm.interaction_return->result!=interactions.completion->result)
            throw FormatError("session interaction resume does not match VM return");
    }
    if(vm.interaction_return&&!interactions.active&&!interactions.resume_tick)
        throw FormatError("session VM interaction return has no controller action");
}

bool valid_playback_key(const PlaybackResourceKey& key) {
    return (key.kind==PlaybackResourceKey::Kind::animation&&((key.source||key.id)&&key.posture==0))||
           (key.kind==PlaybackResourceKey::Kind::posture_reset&&!key.source&&!key.id&&
            (key.posture==1||key.posture==2||key.posture==3));
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

void validate_links(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    validate_host(bundle.host,limits);
    validate_household(bundle);
    if(bundle.runtime_entities.empty()||bundle.runtime_entities.size()>limits.max_runtime_entities||
       bundle.runtime_entities.size()>UINT16_MAX||
       bundle.runtime_entities.size()!=bundle.scheduler.entities.size()||
       (bundle.scheduler.last_tick?*bundle.scheduler.last_tick!=bundle.host.tick:bundle.host.tick!=0))
        throw FormatError("session runtime and scheduler do not match");
    std::int16_t previous=0;
    bool found_primary=false;
    for(std::size_t i=0;i<bundle.runtime_entities.size();++i) {
        const auto& entity=bundle.runtime_entities[i];
        const auto& scheduled=bundle.scheduler.entities[i];
        if(entity.id<=previous||scheduled.id!=entity.id||
           scheduled.status!=entity.vm.status||scheduled.steps!=entity.vm.steps||
           scheduled.run_generation!=entity.vm.run_generation||
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
        validate_playback_links(entity,*world_actor,bundle.host,limits);
        found_primary|=entity.id==bundle.host.runtime_avatar_id;
        previous=entity.id;
    }
    if(!found_primary)throw FormatError("session primary resident is absent from runtime roster");
    if(bundle.seated_continuation)validate_seated_session_continuation(bundle);
}

const SessionArchiveSection& required_section(const SessionArchive& archive,
                                               const std::array<char,4>& id) {
    const auto it=std::find_if(archive.sections.begin(),archive.sections.end(),
        [&](const SessionArchiveSection& section){return section.id==id;});
    if(it==archive.sections.end()||!it->required)
        throw FormatError("session bundle is missing a required section");
    return *it;
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
bool recognized(const std::array<char,4>& id) {
    return id==simulation_id||id==meta_id||id==host_id||id==world_id||id==relation_id||
           id==household_id||id==vm_id||id==scheduler_id||id==controller_id||id==playback_id||id==seated_id;
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
    if(bundle.seated_continuation)archive.sections.push_back({seated_id,true,encode_seated(*bundle.seated_continuation,limits)});
    if(bundle.simulation)archive.sections.push_back({simulation_id,true,encode_simulation(*bundle.simulation,limits.archive.max_section_bytes)});
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
    for(const auto& section:archive.sections)if(section.id==seated_id){
        if(!section.required)throw FormatError("seated continuation must be a required section");
        result.seated_continuation=decode_seated(section.data);
    }
    for(const auto& section:archive.sections)if(section.id==simulation_id){
        if(!section.required)throw FormatError("session simulation section must be required");
        result.simulation=decode_simulation(section.data,limits.max_runtime_entities);
    }
    validate_links(result,limits);
    validate_simulation(result);
    return result;
}

} // namespace odh
