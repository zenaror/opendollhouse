#include "odh/sha256.hpp"
#include "odh/session_bundle.hpp"
#include "odh/actorless_object_trade.hpp"
#include "odh/object_value_registry.hpp"
#include "odh/seated_session_continuation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <set>
#include <map>
#include <tuple>

namespace odh {
namespace {
constexpr std::array<char,4> npc_primary_id{{'N','P','R','M'}};
constexpr std::array<char,4> travel_id{{'T','R','I','P'}};
constexpr std::array<char,4> inventory_id{{'N','I','N','V'}};
constexpr std::array<char,4> build_buy_id{{'B','B','U','Y'}};
constexpr std::array<char,4> house_layout_id{{'H','L','S','T'}};
constexpr std::array<char,4> family_spells_id{{'F','S','P','L'}};
constexpr std::array<char,4> family_vacation_id{{'F','V','A','C'}};
constexpr std::array<char,4> character_family_id{{'C','F','A','M'}};
constexpr std::array<char,4> personal_state_id{{'N','P','E','R'}};
constexpr std::array<char,4> type_attributes_id{{'T','A','T','T'}};
constexpr std::array<char,4> object_groups_id{{'O','G','R','P'}};
constexpr std::array<char,4> visited_id{{'T','V','I','S'}};
constexpr std::array<char,4> home_id{{'T','H','O','M'}};
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
constexpr std::array<char,4> route_stack_id{{'R','S','T','K'}};
constexpr std::array<char,4> object_vm_id{{'O','B','V','M'}};
constexpr std::array<char,4> dynamic_owner_id{{'D','Y','N','O'}};
constexpr std::array<char,4> generic_execution_id{{'G','E','X','P'}};
constexpr std::array<char,4> object_trade_id{{'O','T','R','D'}};
constexpr std::array<char,4> runtime_globals_id{{'R','G','L','B'}};
constexpr std::array<char,4> object_history_id{{'O','H','S','T'}};
constexpr std::array<char,4> family_lease_id{{'F','A','M','L'}};
constexpr std::array<char,4> ui_state_id{{'U','I','S','T'}};
constexpr std::array<char,4> dialog_id{{'D','L','O','G'}};
constexpr std::array<char,4> headline_id{{'H','L','D','N'}};
constexpr std::array<char,4> npc_avatar_id{{'N','P','C','A'}};
constexpr std::array<char,4> architecture_id{{'A','R','C','T'}};
constexpr std::array<char,4> architecture_support_id{{'O','S','U','P'}};
constexpr std::array<char,4> architecture_objects_id{{'A','O','B','J'}};
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
    if(kind>static_cast<std::uint8_t>(PlaybackResourceKey::Kind::reach))
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
    if(p.source_walk){
        for(const auto& secondary:p.source_walk->secondary){
            PlaybackSnapshot lane;lane.state=secondary;Writer payload(limits.archive.max_section_bytes);
            encode_playback_value(payload,lane,limits);const auto bytes=payload.take();
            if(bytes.size()>UINT32_MAX)throw FormatError("locomotion lane exceeds section bounds");
            w.u32(static_cast<std::uint32_t>(bytes.size()));w.append(bytes);
        }
        for(const auto weight:p.source_walk->weights){std::uint32_t bits=0;
            std::memcpy(&bits,&weight,sizeof(bits));w.u32(bits);}
    }

}
PlaybackSnapshot decode_playback_value(Reader& r,const SessionBundleLimits& limits,bool source_walk=false) {
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
    if(source_walk){
        PlaybackSourceWalkSnapshot walking;
        for(auto& secondary:walking.secondary){
            const auto size=r.u32();if(size>limits.archive.max_section_bytes||size>r.remaining())
                throw FormatError("locomotion lane exceeds section bounds");
            const auto bytes=r.bytes(size);Reader lane_reader(bytes);
            auto lane=decode_playback_value(lane_reader,limits,false);
            if(!lane.state||lane.carry||lane.hands||lane.last_tick||lane.source_walk)
                throw FormatError("locomotion lane has non-clip payload");
            secondary=std::move(*lane.state);
        }
        for(auto& weight:walking.weights){const auto bits=r.u32();std::memcpy(&weight,&bits,sizeof(weight));}
        p.source_walk=std::move(walking);
    }
    if(!r.done())throw FormatError("playback state has trailing data");
    return p;
}
std::vector<std::uint8_t> encode_playback(const PlaybackSnapshot& p,const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(p.source_walk?std::uint16_t{2}:payload_version);encode_playback_value(w,p,limits);return w.take();
}
PlaybackSnapshot decode_playback(const std::vector<std::uint8_t>& bytes,const SessionBundleLimits& limits) {
    Reader r(bytes);const auto version=r.u16();if(version!=1&&version!=2)throw FormatError("unsupported playback snapshot version");
    return decode_playback_value(r,limits,version==2);
}

void validate_runtime_globals(const SessionBundle& bundle) {
    if(!bundle.runtime_globals)return;
    auto validator=RuntimeGlobals::from_legacy({});
    if(!validator.restore(*bundle.runtime_globals)||!bundle.host.global_values.empty())
        throw FormatError("runtime globals snapshot has invalid shape or duplicate HOST authority");
}
std::vector<std::uint8_t> encode_runtime_globals(const RuntimeGlobalsSnapshot& state,
        const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(1);
    w.u8(state.selected_sim_is_runtime_id?1:0);
    std::uint64_t known=0;
    for(std::size_t i=0;i<state.words.size();++i)if(state.words[i])known|=std::uint64_t{1}<<i;
    w.u64(known);
    for(const auto& value:state.words)if(value)w.i16(*value);
    return w.take();
}
RuntimeGlobalsSnapshot decode_runtime_globals(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);if(r.u16()!=1)throw FormatError("unsupported runtime globals version");
    const auto resolved=r.u8();if(resolved>1)throw FormatError("invalid runtime globals selection marker");
    const auto known=r.u64();if(known>>38)throw FormatError("runtime globals bitmap exceeds source extent38");
    RuntimeGlobalsSnapshot state;state.words.resize(38);state.selected_sim_is_runtime_id=resolved!=0;
    for(std::size_t i=0;i<38;++i)if(known&(std::uint64_t{1}<<i))state.words[i]=r.i16();
    if(!r.done())throw FormatError("runtime globals have trailing bytes");
    return state;
}

void validate_house_layout_links(const SessionBundle& bundle) {
    if(!bundle.house_layout_history)return;
    const auto& history=*bundle.house_layout_history;
    if(!bundle.host.house_number||!house_layout_history_valid(history)||
       (history.last_sample_tick&&(*history.last_sample_tick>bundle.host.tick||*history.last_sample_tick%10)))
        throw FormatError("House layout history differs from authenticated house/tick");
}
std::vector<std::uint8_t> encode_house_layout_section(const SessionBundle& bundle,std::size_t limit) {
    validate_house_layout_links(bundle);const auto& history=*bundle.house_layout_history;
    Writer w(limit);w.u16(1);w.u16(bundle.host.house_number);
    w.append(bundle.resources.resource_fingerprint.data(),bundle.resources.resource_fingerprint.size());
    for(const auto* pair:{&history.past[0],&history.past[1],&history.past[2],&history.past[3],&history.past[4],&history.current}){
        w.i32(pair->samples);w.i32(pair->routing);
    }
    w.u8(history.last_sample_tick?1:0);if(history.last_sample_tick)w.u64(*history.last_sample_tick);
    return w.take();
}
HouseLayoutHistory decode_house_layout_section(const std::vector<std::uint8_t>& bytes,const SessionBundle& bundle) {
    Reader r(bytes);if(r.u16()!=1)throw FormatError("unsupported House layout history version");
    if(r.u16()!=bundle.host.house_number||r.bytes(32)!=std::vector<std::uint8_t>(
            bundle.resources.resource_fingerprint.begin(),bundle.resources.resource_fingerprint.end()))
        throw FormatError("House layout section belongs to a different house/resource identity");
    HouseLayoutHistory history;
    for(auto* pair:{&history.past[0],&history.past[1],&history.past[2],&history.past[3],&history.past[4],&history.current}){
        pair->samples=r.i32();pair->routing=r.i32();
    }
    const auto stamp=r.u8();if(stamp>1)throw FormatError("invalid House layout sample marker");
    if(stamp)history.last_sample_tick=r.u64();
    if(!r.done()||!house_layout_history_valid(history)||
       (history.last_sample_tick&&(*history.last_sample_tick>bundle.host.tick||*history.last_sample_tick%10)))
        throw FormatError("invalid House layout history or clock binding");
    return history;
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
                                      const SessionBundleLimits& limits,bool household,bool trade,bool globals) {
    validate_host(host,limits);
    Writer writer(limits.archive.max_section_bytes);
    writer.append(host_magic.data(),host_magic.size());
    writer.u16(globals?6:(trade?5:(host.family_budget?(household?4:3):(household?2:1))));
    if(globals||trade)writer.u8(static_cast<std::uint8_t>((household?1:0)|(host.family_budget?2:0)|(globals&&trade?4:0)));
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
                             const SessionBundleLimits& limits,bool& household,bool& trade,bool& globals) {
    Reader reader(bytes);
    if(reader.bytes(host_magic.size())!=std::vector<std::uint8_t>(host_magic.begin(),host_magic.end()))
        throw FormatError("unsupported session host format");
    const auto version=reader.u16();
    if(version<1||version>6)throw FormatError("unsupported session host version");
    globals=version==6;
    const auto flags=version>=5?reader.u8():0;
    if(flags>(globals?7:3))throw FormatError("invalid session host feature flags");
    trade=version==5||(globals&&(flags&4)!=0);
    household=version>=5?(flags&1)!=0:(version==2||version==4);
    const bool has_family_budget=version>=5?(flags&2)!=0:(version==3||version==4);
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
                   const SessionBundleLimits& limits,Decoder decode,bool allow_empty=false) {
    Reader reader(bytes);
    if(reader.u16()!=payload_version)throw FormatError("unsupported session runtime roster version");
    const auto count=reader.u16();
    if((count==0&&!allow_empty)||count>limits.max_runtime_entities||count>reader.remaining()/6)
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

bool context_role_matches(const VmContext& context,VmEntityRole role,std::int16_t id,std::uint64_t generation){
    const auto ref=vm_context_reference(context,role);
    return ref.id==id&&(!context.entity_references_bound||ref.generation==generation);
}

// Mirror the value-only preconditions checked by Sleep::valid_snapshot and
// InteractionQueue::valid_snapshot. Their World/VM bindings are rebuilt later.
void validate_controller_vm_links(const SessionRuntimeEntity& entity,std::uint64_t host_tick) {
    const auto& vm=entity.vm;
    const auto& sleep=entity.controllers.sleep;
    const auto& interactions=entity.controllers.interactions;
    constexpr auto max_clock=std::numeric_limits<std::uint32_t>::max();
    if(entity.controllers.schedule_idle_start&&
       (*entity.controllers.schedule_idle_start>host_tick||
        *entity.controllers.schedule_idle_start>sleep.clock||sleep.clock!=interactions.clock))
        throw FormatError("session shared schedule clock is outside the host/controller clocks");
    if(!vm.immediate_continuations.empty()&&!entity.controllers.schedule_idle_start)
        throw FormatError("session immediate continuation has no shared schedule clock");
    if(sleep.job) {
        const auto& job=*sleep.job;
        if(!sleep.actor||sleep.notify_resume||!job.ticket||job.ticket>vm.ticket_sequence||
           job.idle_start>sleep.clock||job.deadline<=sleep.clock||job.deadline>max_clock||
           vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=job.ticket||
           vm.pending->instruction.instruction.opcode!=0||
           !context_role_matches(vm.pending->context,VmEntityRole::caller,sleep.actor->id,sleep.actor->generation)||vm.stack.empty()||
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
       !context_role_matches(vm.stack.back().context,VmEntityRole::caller,sleep.actor->id,sleep.actor->generation)||
       (vm.status!=VmStatus::running&&vm.status!=VmStatus::budget_exhausted)))
        throw FormatError("session Sleep notify does not match VM state");

    for(const auto& action:interactions.actions)if(action.call.action_tree_override&&!*action.call.action_tree_override)
        throw FormatError("accepted queued action explicitly disables ActionTree");
    for(const auto& action:interactions.actions)if(action.check&&
       !routine_key_valid(*action.check))
        throw FormatError("session queued interaction check key is invalid");
    if(vm.interaction_return&&vm.status!=VmStatus::yielded)
        throw FormatError("session interaction return does not match yielded VM status");
    if(vm.immediate_return&&vm.status!=VmStatus::yielded)
        throw FormatError("session immediate return does not match yielded VM status");
    const auto valid_ticket=[&](std::uint64_t ticket){return ticket&&ticket<=vm.ticket_sequence;};
    const auto parent_count=interactions.suspended_active.size();
    if(parent_count>interactions.actions.size()||
       (!interactions.suspended_active.empty()&&!interactions.active)||
       (interactions.profile==InteractionQueueProfile::legacy_fifo&&!interactions.suspended_active.empty()))
        throw FormatError("session nested interaction prefix is invalid");
    std::uint64_t previous_parent_ticket=0;
    for(std::size_t i=0;i<parent_count;++i){
        const auto& parent=interactions.suspended_active[i];
        if(!interaction_header_binding_valid(interactions,i,parent)||!valid_ticket(parent.ticket)||
           parent.ticket<=previous_parent_ticket)
            throw FormatError("session suspended interaction parent does not match queue order");
        previous_parent_ticket=parent.ticket;
    }
    if(interactions.active&&(!interaction_header_binding_valid(interactions,parent_count,*interactions.active)||
       !valid_ticket(interactions.active->ticket)||interactions.active->ticket<=previous_parent_ticket))
        throw FormatError("session active interaction does not match nested queue prefix");
    std::vector<const VmFrame*> interaction_frames;
    for(const auto& frame:vm.stack) {
        if(frame.return_mode!=VmCallReturnMode::interaction_next_tick&&
           frame.return_mode!=VmCallReturnMode::immediate)continue;
        interaction_frames.push_back(&frame);
    }
    const auto frame_matches=[&](const VmFrame& frame,const VmCall& call,
                                  std::uint64_t ticket){
        return frame.invocation_ticket==ticket&&
           frame.key.scope==call.key.scope&&frame.key.id==call.key.id&&
           frame.context.caller==call.context.caller&&frame.context.callee==call.context.callee&&
           frame.context.code_owner==call.context.code_owner&&
           frame.context.entity_references_bound==call.context.entity_references_bound&&
           frame.context.caller_generation==call.context.caller_generation&&frame.context.callee_generation==call.context.callee_generation&&
           (!vm.action_tree_known||frame.action_tree)&&
           frame.exact_arguments==call.exact_arguments&&
           (call.exact_arguments?frame.arguments.size()==call.arguments.size():frame.arguments.size()>=call.arguments.size())&&
           frame.return_mode==call.return_mode;
    };
    for(const auto& action:interactions.actions) {
        const auto expected=(action.flags&4U)?VmCallReturnMode::immediate:
                                              VmCallReturnMode::interaction_next_tick;
        if(action.call.return_mode!=expected||
           ((action.flags&4U)&&action.mode!=InteractionQueueMode::idle))
            throw FormatError("session queued interaction flags do not match call return mode");
    }
    for(std::size_t i=0;i<vm.immediate_continuations.size();++i){
        const auto& continuation=vm.immediate_continuations[i];
        if(!valid_ticket(continuation.ticket)||continuation.parent_depth==0||
           continuation.parent_depth>vm.stack.size()||
           (continuation.parent_status!=VmStatus::running&&
            continuation.parent_status!=VmStatus::budget_exhausted&&
            continuation.parent_status!=VmStatus::waiting)||
           (i&&continuation.parent_depth<=vm.immediate_continuations[i-1].parent_depth))
            throw FormatError("session immediate continuation identity or depth is invalid");
        const auto boundary=continuation.parent_depth;
        const bool returned_latest=i+1==vm.immediate_continuations.size()&&vm.immediate_return&&
            vm.immediate_return->ticket==continuation.ticket;
        if(returned_latest){
            if(vm.stack.size()!=continuation.parent_depth||vm.status!=VmStatus::yielded||
               vm.immediate_return->run_generation!=vm.run_generation)
                throw FormatError("session popped immediate return does not match its continuation");
        }else if(boundary>=vm.stack.size()||
                 vm.stack[boundary].return_mode!=VmCallReturnMode::immediate||
                 vm.stack[boundary].invocation_ticket!=continuation.ticket){
            throw FormatError("session immediate continuation has no protected frame");
        }
        if(continuation.parent_pending){
            const auto& pending=*continuation.parent_pending;
            const auto& parent=vm.stack[boundary-1];
            if(pending.instruction.instruction.opcode==0&&(!sleep.actor||
               sleep.actor->id!=entity.id||sleep.actor->generation!=interactions.actor_generation||
               sleep.actor->run_generation!=vm.run_generation))
                throw FormatError("session parked Sleep parent has no matching controller actor");
            const auto opcode=pending.instruction.instruction.opcode;
            const auto& operands=pending.instruction.instruction.operands;
            if((continuation.parent_status!=VmStatus::waiting)||!valid_ticket(pending.ticket)||
               pending.ticket==continuation.ticket||pending.context.caller!=entity.id||
               !vm_context_equal(parent.context,pending.context)||
               pending.instruction.depth!=continuation.parent_depth||pending.instruction.key.scope!=parent.key.scope||
               pending.instruction.key.id!=parent.key.id||pending.instruction.code_owner!=parent.context.code_owner||
               pending.instruction.depth==0||pending.instruction.pc!=parent.pc||
               (opcode!=0&&opcode!=17&&opcode!=44&&opcode!=22&&opcode!=27&&opcode!=45))
                throw FormatError("session immediate parent pending call is unsupported or malformed");
            if(opcode==0||opcode==17){
                const auto index=static_cast<std::uint16_t>(operands[0]|(static_cast<unsigned>(operands[1])<<8));
                if(index>32767||index>=parent.arguments.size()||parent.arguments[index]<0||
                   (opcode==17&&(operands[2]|(static_cast<unsigned>(operands[3])<<8))>1))
                    throw FormatError("session immediate parent wait arguments are invalid");
            }else if(opcode==22||opcode==27||opcode==45){
                if(!entity.route_stack||std::count_if(entity.route_stack->jobs.begin(),entity.route_stack->jobs.end(),
                    [&](const auto& job){return job.ticket==pending.ticket&&job.actor==entity.id&&
                        job.run_generation==vm.run_generation&&job.actor_generation==interactions.actor_generation;})!=1)
                    throw FormatError("session parked routing parent lacks its unique route manifest");
            }else{
                const auto flags=operands[5];
                const auto mode=(flags&1U)|((flags>>3)&2U);
                if((flags&0x80U)||(mode!=0&&mode!=3))
                    throw FormatError("session parked parent animation mode is unsupported");
                auto resource_id=static_cast<std::uint16_t>(operands[0]|(operands[1]<<8));
                if(flags&4U){
                    if(resource_id>=parent.arguments.size())
                        throw FormatError("session parked parent animation parameter index is invalid");
                    resource_id=static_cast<std::uint16_t>(parent.arguments[resource_id]);
                }
                if(!resource_id)
                    throw FormatError("session parked parent animation has no waitable resource");
                const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
                const auto index=static_cast<std::size_t>((flags&32U)?operands[2]:0);
                if((bank==VmFrameBank::arguments&&index>=parent.arguments.size())||
                   (bank==VmFrameBank::locals&&index>=parent.locals.size()&&index>=256))
                    throw FormatError("session parked parent animation output index is invalid");
            }
        }else if(continuation.parent_status==VmStatus::waiting){
            throw FormatError("session waiting immediate parent has no pending call");
        }
    }
    if(vm.immediate_return){
        if(vm.immediate_continuations.empty()||
           vm.immediate_return->ticket!=vm.immediate_continuations.back().ticket||
           vm.immediate_return->run_generation!=vm.run_generation)
            throw FormatError("session immediate return does not match latest continuation");
    }
    bool action_frame=false;
    if(interactions.active){
        const auto* executing=interaction_execution_call(interactions,parent_count);
        if(!executing)throw FormatError("session active execution descriptor is absent");
        const auto& active_call=*executing;
        const bool returned_unobserved=(active_call.return_mode==VmCallReturnMode::interaction_next_tick&&
            vm.interaction_return&&!interactions.resume_tick&&
            vm.interaction_return->ticket==interactions.active->ticket)||
            (active_call.return_mode==VmCallReturnMode::immediate&&vm.immediate_return&&
            !interactions.resume_tick&&vm.immediate_return->ticket==interactions.active->ticket);
        const auto expected_frames=parent_count+(returned_unobserved?0:1);
        if(interaction_frames.size()!=expected_frames)
            throw FormatError("session interaction VM frame roster differs from active prefix");
        for(std::size_t i=0;i<interaction_frames.size();++i){
            const auto& saved=i<parent_count?interactions.suspended_active[i]:*interactions.active;
            const auto* executing=interaction_execution_call(interactions,i);
            if(!executing||!frame_matches(*interaction_frames[i],*executing,saved.ticket))
                throw FormatError("session interaction VM frame does not match queue key, context, or ticket");
        }
        action_frame=interaction_frames.size()>parent_count;
    }else if(!interaction_frames.empty()){
        throw FormatError("session interaction frame has no active controller prefix");
    }
    if(interactions.active) {
        const auto* executing=interaction_execution_call(interactions,parent_count);
        if(!executing)throw FormatError("session active execution descriptor is absent");
        const auto& active_call=*executing;
        if(vm.interaction_return&&!interactions.resume_tick&&
           (active_call.return_mode!=VmCallReturnMode::interaction_next_tick||action_frame||
            vm.interaction_return->ticket!=interactions.active->ticket||
            vm.interaction_return->code_owner!=active_call.context.code_owner||
            vm.interaction_return->run_generation!=vm.run_generation))
            throw FormatError("session nested interaction return does not match deepest action");
        if(vm.immediate_return&&!interactions.resume_tick&&
           (active_call.return_mode!=VmCallReturnMode::immediate||action_frame||
            vm.immediate_return->ticket!=interactions.active->ticket||
            vm.immediate_return->code_owner!=active_call.context.code_owner||
            vm.immediate_return->run_generation!=vm.run_generation))
            throw FormatError("session immediate return does not match deepest action");
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
        if(!vm.interaction_return&&!vm.immediate_return&&!action_frame&&vm.status!=VmStatus::fault) {
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
    if(vm.immediate_return&&!interactions.active&&!interactions.resume_tick)
        throw FormatError("session VM immediate return has no controller action");
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
void validate_object_trade(const SessionBundle& bundle) {
    if(!bundle.object_trade)return;
    if(!bundle.generic_execution||!bundle.object_history)
        throw FormatError("trade ledger requires generic resource and object history provenance");
    if(bundle.object_trade->objects.size()>4096)throw FormatError("trade ledger exceeds bounds");
    validate_object_value_groups(*bundle.object_trade,bundle.world);
    if(bundle.object_trade->version==2){
        for(const auto& group:bundle.object_trade->groups)for(const auto& member:group.members){
            const auto& object=member.object;
            const auto created=std::find_if(bundle.created_objects.begin(),bundle.created_objects.end(),[&](const auto& value){return value.id==object.id;});
            if(created!=bundle.created_objects.end()&&(created->generation!=object.generation||created->guid!=object.guid||
                created->source!=object.source||created->asset!=object.asset||created->definition_chunk_id!=object.definition_chunk_id||created->master_guid!=object.master_guid))
                throw FormatError("object value group differs from creation provenance");
        }
        return;
    }
    std::int16_t previous=0;
    for(const auto& state:bundle.object_trade->objects) {
        const auto& o=state.object;
        if(o.master_guid)throw FormatError("trade ledger v1 cannot preserve multipart construction provenance");
        if(o.id<=previous||!o.generation||!o.guid||!o.definition_chunk_id||o.source.empty()||o.source.size()>4096||o.asset.size()>4096||
           o.source.find('\0')!=std::string::npos||o.asset.find('\0')!=std::string::npos)
            throw FormatError("trade ledger identity/provenance is invalid");
        previous=o.id;
        const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& e){return e.id==o.id;});
        if(found==bundle.world.entities.end()||found->generation!=o.generation||found->kind!=EntityKind::object||
           !found->definition||found->definition->guid!=o.guid)
            throw FormatError("trade ledger references missing or reused object");
        const auto origin=std::find_if(bundle.created_objects.begin(),bundle.created_objects.end(),
            [&](const auto& e){return e.id==o.id;});
        if(origin!=bundle.created_objects.end()&&(origin->generation!=o.generation||origin->guid!=o.guid||
           origin->source!=o.source||origin->asset!=o.asset||origin->definition_chunk_id!=o.definition_chunk_id))
            throw FormatError("trade ledger differs from creation resource provenance");
    }
}
std::vector<std::uint8_t> encode_object_trade(const SessionObjectTradeLedger& ledger,const SessionBundleLimits& limits) {
    Writer w(limits.archive.max_section_bytes);w.u16(ledger.version);
    if(ledger.version==1){
        w.u16(static_cast<std::uint16_t>(ledger.objects.size()));
        for(const auto& state:ledger.objects){const auto& o=state.object;
            w.i16(o.id);w.u64(o.generation);w.u32(o.guid);w.string(o.source);w.string(o.asset);w.u16(o.definition_chunk_id);
            w.i32(state.initial_price);w.u8(state.wear?1:0);if(state.wear)w.u16(*state.wear);
        }
    }else{
        w.u64(ledger.revision);w.u16(static_cast<std::uint16_t>(ledger.groups.size()));
        for(const auto& group:ledger.groups){
            w.u64(group.group_id);w.u64(group.revision);w.u8(group.multitile?1:0);w.u16(static_cast<std::uint16_t>(group.members.size()));
            for(const auto& member:group.members){const auto& o=member.object;
                w.i16(o.id);w.u64(o.generation);w.u32(o.guid);w.string(o.source);w.string(o.asset);w.u16(o.definition_chunk_id);
                w.u8(o.master_guid?1:0);if(o.master_guid)w.u32(*o.master_guid);
                w.i16(member.offset_x);w.i16(member.offset_y);w.i16(member.offset_level);
                w.u8(member.lead_object?1:0);if(member.lead_object)w.u16(*member.lead_object);
                w.u8(member.wear_known?1:0);w.u8(member.wear?1:0);if(member.wear)w.u16(*member.wear);
                w.u8(member.disabled?1:0);if(member.disabled)w.u8(*member.disabled);
            }
            w.u8(group.base?1:0);if(group.base){w.i16(group.base->id);w.u64(group.base->generation);}
            w.u8(group.initial_price?1:0);if(group.initial_price)w.i32(*group.initial_price);
            const auto& origin=group.price_origin;w.u8(static_cast<std::uint8_t>(origin.kind));
            w.u32(origin.guid);w.string(origin.source);w.string(origin.asset);w.u16(origin.definition_chunk_id);
            w.u8(origin.objd_sha256?1:0);if(origin.objd_sha256)w.append(origin.objd_sha256->data(),origin.objd_sha256->size());
            w.u8(origin.writer?1:0);if(origin.writer){w.i16(origin.writer->id);w.u64(origin.writer->generation);}
        }
    }
    return w.take();
}
SessionObjectTradeLedger decode_object_trade(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);const auto version=r.u16();if(version!=1&&version!=2)throw FormatError("unsupported trade ledger version");
    SessionObjectTradeLedger result;result.version=version;
    const auto flag=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid object value boolean");return value!=0;};
    if(version==2)result.revision=r.u64();
    const auto count=r.u16();if(count>4096)throw FormatError("trade ledger exceeds bounds");
    std::size_t members=0,bytes_used=0;
    const auto read_text=[&](){auto value=r.string();if(version==2){if(value.size()>1048576-bytes_used)throw FormatError("object value provenance budget exceeded");bytes_used+=value.size();}return value;};
    for(std::size_t n=0;n<count;++n){
        if(version==1){SessionObjectTradeState state;auto& o=state.object;
            o.id=r.i16();o.generation=r.u64();o.guid=r.u32();o.source=read_text();o.asset=read_text();o.definition_chunk_id=r.u16();
            state.initial_price=r.i32();if(flag())state.wear=r.u16();result.objects.push_back(std::move(state));continue;
        }
        SessionObjectValueGroup group;group.group_id=r.u64();group.revision=r.u64();group.multitile=flag();
        const auto member_count=r.u16();if(!member_count||member_count>4096-members)throw FormatError("object value member budget exceeded");members+=member_count;
        for(unsigned m=0;m<member_count;++m){SessionObjectValueMember member;auto& o=member.object;
            o.id=r.i16();o.generation=r.u64();o.guid=r.u32();o.source=read_text();o.asset=read_text();o.definition_chunk_id=r.u16();
            if(flag())o.master_guid=r.u32();member.offset_x=r.i16();member.offset_y=r.i16();member.offset_level=r.i16();
            if(flag())member.lead_object=r.u16();member.wear_known=flag();if(flag())member.wear=r.u16();
            if(flag())member.disabled=r.u8();group.members.push_back(std::move(member));
        }
        if(flag())group.base=ObjectTradeIdentity{r.i16(),r.u64()};if(flag())group.initial_price=r.i32();
        auto& origin=group.price_origin;origin.kind=static_cast<SessionObjectPriceSource>(r.u8());origin.guid=r.u32();
        origin.source=read_text();origin.asset=read_text();origin.definition_chunk_id=r.u16();
        if(flag()){std::array<std::uint8_t,32> digest{};for(auto& byte:digest)byte=r.u8();origin.objd_sha256=digest;}
        if(flag())origin.writer=ObjectTradeIdentity{r.i16(),r.u64()};result.groups.push_back(std::move(group));
    }
    if(!r.done())throw FormatError("trade ledger has trailing bytes");
    return result;
}
std::vector<std::uint8_t> encode_generic_execution(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    const bool masters=std::any_of(bundle.created_objects.begin(),bundle.created_objects.end(),[](const auto& o){return o.master_guid.has_value();});
    Writer w(limits.archive.max_section_bytes);w.u16(masters?2:1);
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
        w.u16(object.definition_chunk_id);if(masters)w.u32(object.master_guid.value_or(0));
    }
    w.u32(static_cast<std::uint32_t>(bundle.removed_original_objects.size()));
    for(const auto& object:bundle.removed_original_objects){w.i16(object.id);w.u64(object.generation);w.u32(object.guid);}
    w.u32(static_cast<std::uint32_t>(bundle.deferred_removals.size()));
    for(const auto& object:bundle.deferred_removals){w.i16(object.id);w.u64(object.generation);}
    return w.take();
}
void decode_generic_execution(const std::vector<std::uint8_t>& bytes,SessionBundle& bundle,
                              const SessionBundleLimits& limits) {
    Reader r(bytes);const auto version=r.u16();if(version!=1&&version!=2)throw FormatError("unsupported generic execution profile version");
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
        if(version>=2){const auto master=r.u32();if(master)object.master_guid=master;}
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

std::vector<std::uint8_t> encode_object_history(const SessionObjectHistory& history,const SessionBundleLimits& limits) {
    const bool resource_only=std::any_of(history.retired_npc_avatars.begin(),history.retired_npc_avatars.end(),[](const auto& o){return o.neighbor_id==0;});
    const bool masters=std::any_of(history.retired_created_objects.begin(),history.retired_created_objects.end(),[](const auto& o){return o.master_guid.has_value();});
    const bool household=!history.retired_household_avatars.empty();
    Writer w(limits.archive.max_section_bytes);w.u16(household?5:masters?4:resource_only?3:history.reusable_entity_ids?2:1);w.u8(history.complete?1:0);
    if(masters||household)w.u8(history.reusable_entity_ids?1:0);
    w.u32(static_cast<std::uint32_t>(history.retired_created_objects.size()));
    for(const auto& object:history.retired_created_objects) {
        w.i16(object.id);w.u64(object.generation);w.u32(object.guid);
        w.string(object.source);w.string(object.asset);w.u16(object.definition_chunk_id);
        if(masters||household)w.u32(object.master_guid.value_or(0));
    }
    w.u32(static_cast<std::uint32_t>(history.reserved_entity_ids.size()));
    for(const auto id:history.reserved_entity_ids)w.i16(id);
    if(history.reusable_entity_ids){
        w.u32(static_cast<std::uint32_t>(history.retired_npc_avatars.size()));
        for(const auto& o:history.retired_npc_avatars){
            w.i16(o.id);w.u64(o.generation);w.u32(o.guid);w.i16(o.neighbor_id);
            w.string(o.source);w.string(o.asset);w.u16(o.object_definition_id);
            w.append(o.character_fingerprint.data(),o.character_fingerprint.size());
        }
    }
    if(household){
        w.u32(static_cast<std::uint32_t>(history.retired_household_avatars.size()));
        for(const auto& o:history.retired_household_avatars){const auto& m=o.member;const auto& lease=o.source_lease;
            w.i16(m.id);w.u64(m.generation);w.u32(m.guid);w.i16(m.neighbor_id);w.u16(m.object_definition_id);
            w.string(m.character_asset);w.append(m.character_fingerprint.data(),32);
            w.append(lease.neighborhood_identity.data(),32);w.append(lease.family_identity.data(),32);
            w.u64(lease.occupancy_epoch);w.u16(lease.family_chunk_id);w.append(o.source_checkpoint_sha256.data(),32);
        }
    }
    return w.take();
}
SessionObjectHistory decode_object_history(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);const auto version=r.u16();if(version!=1&&version!=2&&version!=3&&version!=4&&version!=5)throw FormatError("unsupported object history version");
    const auto complete=r.u8();if(complete>1)throw FormatError("invalid object history completeness marker");
    SessionObjectHistory result;result.complete=complete!=0;result.reusable_entity_ids=version>=2;
    if(version>=4){const auto reusable=r.u8();if(reusable>1)throw FormatError("invalid object history reuse marker");result.reusable_entity_ids=reusable!=0;}
    const auto count=r.u32();if(count>4096)throw FormatError("retired creation history exceeds limit");
    for(std::uint32_t n=0;n<count;++n) {
        SessionCreatedObject object;object.id=r.i16();object.generation=r.u64();object.guid=r.u32();
        object.source=r.string();object.asset=r.string();object.definition_chunk_id=r.u16();
        if(version>=4){const auto master=r.u32();if(master)object.master_guid=master;}
        result.retired_created_objects.push_back(std::move(object));
    }
    const auto reservations=r.u32();if(reservations>4096)throw FormatError("object history reservations exceed limit");
    for(std::uint32_t n=0;n<reservations;++n)result.reserved_entity_ids.push_back(r.i16());
    if(result.reusable_entity_ids){
        const auto npcs=r.u32();if(npcs>4096)throw FormatError("retired NPC history exceeds limit");
        for(std::uint32_t n=0;n<npcs;++n){
            SessionNpcAvatarOrigin o;o.id=r.i16();o.generation=r.u64();o.guid=r.u32();o.neighbor_id=r.i16();
            if(o.neighbor_id==0&&version<3)throw FormatError("resource-only retired NPC requires history version3");
            o.source=r.string();o.asset=r.string();o.object_definition_id=r.u16();
            for(auto& byte:o.character_fingerprint)byte=r.u8();
            result.retired_npc_avatars.push_back(std::move(o));
        }
    }
    if(version>=5){
        const auto count=r.u32();if(count>4096)throw FormatError("retired household history exceeds limit");
        for(std::uint32_t n=0;n<count;++n){SessionRetiredHouseholdAvatar o;auto& m=o.member;auto& lease=o.source_lease;
            m.id=r.i16();m.generation=r.u64();m.guid=r.u32();m.neighbor_id=r.i16();m.object_definition_id=r.u16();
            m.character_asset=r.string();for(auto& byte:m.character_fingerprint)byte=r.u8();
            for(auto& byte:lease.neighborhood_identity)byte=r.u8();for(auto& byte:lease.family_identity)byte=r.u8();
            lease.occupancy_epoch=r.u64();lease.family_chunk_id=r.u16();for(auto& byte:o.source_checkpoint_sha256)byte=r.u8();
            result.retired_household_avatars.push_back(std::move(o));
        }
        if(result.retired_household_avatars.empty())throw FormatError("OHST5 requires explicit retired household provenance");
    }
    if(!r.done())throw FormatError("object history has trailing data");
    return result;
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
bool source_circle_call_resolves_index(const VmSnapshot& vm,const LocalRouteSnapshot& route,
                                       const Instruction& instruction) {
    if(!route.source_circle||!supported_route45_instruction(instruction)||vm.stack.empty())return false;
    const auto data=static_cast<std::uint16_t>(instruction.operands[0]|(
        static_cast<std::uint16_t>(instruction.operands[1])<<8));
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(
        static_cast<std::uint16_t>(instruction.operands[3])<<8));
    std::size_t index=data;
    if(scope==0){
        const auto& arguments=vm.stack.back().arguments;
        if(data>=arguments.size()||arguments[data]<0)return false;
        index=static_cast<std::size_t>(arguments[data]);
    }
    return index==route.source_circle->slot_index;
}
bool source_range_call_resolves_index(const VmSnapshot& vm,const LocalRouteSnapshot& route,
                                       const Instruction& instruction) {
    if(!route.source_range||vm.stack.empty())return false;
    if(is_source_goto_relative_slot_instruction(instruction)){
        const auto generated=make_source_goto_relative_slot(instruction);
        if(!generated||generated->records.size()!=1)return false;
        const auto& a=route.source_range->slot;const auto& b=generated->records.front();
        // A transient request has no resource SLOT index: authenticate every
        // generated field against its raw opcode-27 operands, not OldTrapCount.
        return route.source_range->slot_index==0&&route.source_range->slot_version==generated->version&&
            a.type==b.type&&a.offset_bits==b.offset_bits&&a.standing==b.standing&&a.sitting==b.sitting&&
            a.ground==b.ground&&a.flags==b.flags&&a.snap_target_slot==b.snap_target_slot&&
            a.proximity==b.proximity&&a.max_size==b.max_size&&a.unknown_i10==b.unknown_i10&&
            a.gradient_bits==b.gradient_bits&&a.height==b.height&&a.facing==b.facing&&a.resolution==b.resolution;
    }
    if(!supported_route45_instruction(instruction))return false;
    const auto data=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(instruction.operands[2]|(instruction.operands[3]<<8));
    std::size_t index=data;
    if(scope==0){
        const auto& args=vm.stack.back().arguments;
        if(data>=args.size()||args[data]<0)return false;
        index=static_cast<std::size_t>(args[data]);
    }
    return index==route.source_range->slot_index;
}
bool same_float_bits(float a,float b) {
    std::uint32_t lhs=0,rhs=0;
    std::memcpy(&lhs,&a,sizeof(lhs));std::memcpy(&rhs,&b,sizeof(rhs));
    return lhs==rhs;
}
bool source_circle_goal_matches(const SourceSlotCircleGoal& a,const SourceSlotCircleGoal& b) {
    return a.position==b.position&&a.entry_flags==b.entry_flags&&a.facing==b.facing&&
        same_float_bits(a.radian_direction,b.radian_direction);
}
void validate_source_circle_value(const LocalRouteSnapshot& route) {
    if(!route.source_circle)return;
    const auto& circle=*route.source_circle;
    if(circle.slot_version!=10||circle.slot_index>UINT16_MAX||!is_source_slot_circle(circle.slot)||
       !route.arrival_radian_direction||
       ((route.target==route.actor)!=route.target_anchor_radian_direction.has_value())||
       (route.target_anchor_radian_direction&&
        !std::isfinite(*route.target_anchor_radian_direction))||
       !std::isfinite(circle.target_radian_direction)||
       !std::isfinite(*route.arrival_radian_direction)||circle.fallback_goals.size()>7||
       route.path.empty()||route.target_position.level<1||
       route.target_position.direction>7||route.target_position.x<INT16_MIN||
       route.target_position.x>INT16_MAX||route.target_position.y<INT16_MIN||
       route.target_position.y>INT16_MAX)
        throw FormatError("invalid source social-circle SLOT route identity or transform");
    if(!(circle.selected.position==route.path.back())||
       circle.selected.entry_flags!=static_cast<std::uint8_t>(route.entry_flags)||
       circle.selected.facing!=route.facing||route.face_anywhere!=(circle.slot.facing==-3)||
       !same_float_bits(circle.selected.radian_direction,*route.arrival_radian_direction))
        throw FormatError("source circle selected goal differs from its path or arrival");
    const auto validate_goal=[](const SourceSlotCircleGoal& goal) {
        return goal.position.x>=INT16_MIN&&goal.position.x<=INT16_MAX&&
            goal.position.y>=INT16_MIN&&goal.position.y<=INT16_MAX&&goal.entry_flags!=0&&
            goal.facing<=7&&std::isfinite(goal.radian_direction);
    };
    if(!validate_goal(circle.selected))
        throw FormatError("source circle selected goal is outside the LotTilePos profile");
    std::vector<SourceSlotCircleGoal> saved_goals;
    saved_goals.reserve(circle.fallback_goals.size()+1);
    saved_goals.push_back(circle.selected);
    for(const auto& goal:circle.fallback_goals) {
        if(!validate_goal(goal)||std::any_of(saved_goals.begin(),saved_goals.end(),[&](const auto& old){
                return old.position==goal.position;}))
            throw FormatError("source circle fallback goal is invalid or duplicated");
        saved_goals.push_back(goal);
    }
    SlotTable table{};table.version=circle.slot_version;
    // The saved row is enough for geometry. Its actual resource index is
    // validated against Route45 separately; never allocate up to that index.
    table.records.push_back(circle.slot);
    std::vector<SourceSlotCircleGoal> source_goals;
    try {
        source_goals=enumerate_source_slot_circle_candidates(table,0,
            route.target_position,circle.target_radian_direction);
    } catch(...) {throw FormatError("source circle SLOT candidates cannot be re-derived");}
    for(const auto& goal:saved_goals)
        if(std::none_of(source_goals.begin(),source_goals.end(),[&](const auto& source){
                return source_circle_goal_matches(goal,source);}))
            throw FormatError("saved source circle goal differs from its frozen SLOT target");
}
double source_round_even(double value) {
    const auto lower=std::floor(value),fraction=value-lower;
    return fraction<0.5?lower:fraction>0.5?lower+1:
        std::fmod(std::abs(lower),2.0)==0?lower:lower+1;
}
std::uint8_t source_direction_notch(float radians) {
    constexpr double pi=3.141592653589793238462643383279502884;
    auto direction=std::fmod(source_round_even(static_cast<double>(radians)/(pi/4)),8.0);
    if(direction<0)direction+=8;
    return static_cast<std::uint8_t>(direction);
}
void validate_source_range_value(const LocalRouteSnapshot& route) {
    if(!route.source_range)return;
    const auto& range=*route.source_range;
    if(route.source_circle||route.look_towards||range.slot_version!=10||range.slot_index>UINT16_MAX||
       !is_source_slot_range(range.slot)||!route.arrival_radian_direction||
       !std::isfinite(*route.arrival_radian_direction)||!std::isfinite(range.target_radian_direction)||
       range.fallback_goals.size()>=4096||route.path.empty()||route.face_anywhere!=(range.slot.facing==-3)||
       route.entry_flags<=0||route.entry_flags>255||route.target_position.level<1||route.target_position.level>127||
       route.target_position.x<INT16_MIN||route.target_position.x>INT16_MAX||
       route.target_position.y<INT16_MIN||route.target_position.y>INT16_MAX||
       route.target_position.direction!=source_direction_notch(range.target_radian_direction)||
       ((route.target==route.actor)!=route.target_anchor_radian_direction.has_value())||
       (route.target_anchor_radian_direction&&(!std::isfinite(*route.target_anchor_radian_direction)||
          !same_float_bits(*route.target_anchor_radian_direction,range.target_radian_direction))))
        throw FormatError("invalid source ranged SLOT route identity, profile or frozen transform");
    if(!(range.selected.position==route.path.back())||range.selected.entry_flags!=route.entry_flags||
       range.selected.facing!=route.facing||!same_float_bits(range.selected.radian_direction,*route.arrival_radian_direction))
        throw FormatError("source range selected goal differs from its path or continuous arrival");
    SlotTable table{};table.version=range.slot_version;table.records.push_back(range.slot);
    std::vector<SourceSlotCircleGoal> geometry;
    try{geometry=enumerate_source_slot_range_candidates(table,0,route.target_position,range.target_radian_direction);}
    catch(...){throw FormatError("source range SLOT geometry cannot be re-derived within its bounded profile");}
    if(geometry.size()>4096)throw FormatError("source range geometry exceeds candidate bounds");
    std::map<std::pair<std::int32_t,std::int32_t>,SourceSlotCircleGoal> source;
    for(const auto& goal:geometry)source.emplace(std::make_pair(goal.position.x,goal.position.y),goal);
    std::set<std::pair<std::int32_t,std::int32_t>> seen;
    const auto validate=[&](const SourceSlotCircleGoal& goal){
        const auto key=std::make_pair(goal.position.x,goal.position.y);
        if(goal.position.x<INT16_MIN||goal.position.x>INT16_MAX||goal.position.y<INT16_MIN||goal.position.y>INT16_MAX||
           !goal.entry_flags||goal.facing>7||!std::isfinite(goal.radian_direction)||!seen.insert(key).second)
            throw FormatError("source range choice is invalid or duplicated");
        const auto found=source.find(key);
        if(found==source.end()||!source_circle_goal_matches(goal,found->second))
            throw FormatError("source range choice differs from its exact frozen SLOT geometry");
    };
    validate(range.selected);for(const auto& goal:range.fallback_goals)validate(goal);
}
struct SourceTurnSelection {std::uint8_t index;float residual;};
SourceTurnSelection source_turn_selection(float from,float to) {
    constexpr double pi=3.141592653589793238462643383279502884;
    auto difference=std::fmod(std::fmod(static_cast<double>(to)-from,pi*2)+pi*2,pi*2);
    if(difference>pi)difference-=pi*2;
    const auto magnitude=std::abs(difference);
    const auto offset=static_cast<std::uint8_t>(difference>0?0:1);
    if(magnitude>=pi-0.01)return {static_cast<std::uint8_t>(4+offset),0};
    if(magnitude>=pi/2-0.01)return {static_cast<std::uint8_t>(6+offset),static_cast<float>(magnitude-pi/2)};
    if(magnitude>=pi/4-0.01)return {static_cast<std::uint8_t>(8+offset),static_cast<float>(magnitude-pi/4)};
    return {0,0};
}
void validate_source_look_route_value(const LocalRouteSnapshot& route,const SessionBundleLimits& limits) {
    if(!route.look_towards)return;
    const auto& look=*route.look_towards;
    if(look.mode<2||look.mode>5||!std::isfinite(look.start_radian_direction)||
       !std::isfinite(look.target_radian_direction)||!std::isfinite(look.animation_speed)||
       !std::isfinite(look.turn_tweak)||look.animation_speed<=0||
       route.source_circle||route.source_range||route.arrival_radian_direction||route.target_anchor_radian_direction||
       route.target!=route.actor||route.target_generation!=route.actor_generation||
       route.path.size()!=1||route.next!=1||
       !(route.path.front()==RoutePoint{route.expected.x,route.expected.y})||
       route.expected.x!=route.target_position.x||route.expected.y!=route.target_position.y||
       route.expected.level!=route.target_position.level||
       route.target_position.direction!=source_direction_notch(look.start_radian_direction)||
       route.facing!=source_direction_notch(look.target_radian_direction)||route.entry_flags!=1||
       route.face_anywhere||route.walking_started||route.call_failure_trees||!route.same_room_verified||
       route.frame.portal_turns||route.frame.wait_time||route.frame.retries_remaining)
        throw FormatError("invalid frozen LookTowards route geometry or source fields");
    const auto selection=source_turn_selection(look.start_radian_direction,look.target_radian_direction);
    if(look.animation_index==0){
        if(route.frame.state!=RouteFrameState::turn_only||look.animation_num_frames||
           look.turn_tweak!=0||look.animation_speed!=1||
           !same_float_bits(route.expected_radian_direction,look.start_radian_direction))
            throw FormatError("unstarted LookTowards route contains turn animation progress");
    }else if(look.animation_index>=4&&look.animation_index<=9){
        const auto speed=look.walk_style==1&&!look.in_pool?2.0f:1.0f;
        if(route.frame.state!=RouteFrameState::end_turn||!selection.index||
           selection.index!=look.animation_index||!look.animation_num_frames||
           look.animation_num_frames>limits.max_playback_frames||
           !same_float_bits(look.animation_speed,speed))
            throw FormatError("LookTowards END_TURN animation differs from source selection");
        auto tweak=selection.residual/(static_cast<float>(look.animation_num_frames)/speed);
        if((selection.index&1U)!=0)tweak=-tweak;
        if(!same_float_bits(tweak,look.turn_tweak))
            throw FormatError("LookTowards turn tweak differs from selected clip duration");
    }else throw FormatError("LookTowards animation index is outside the supported source profile");
}
void write_slot_record(Writer& w,const SlotRecord& slot) {
    w.u16(slot.type);for(const auto value:slot.offset_bits)w.u32(value);
    w.i32(slot.standing);w.i32(slot.sitting);w.i32(slot.ground);w.u32(slot.flags);
    w.i32(slot.snap_target_slot);
    w.u8(slot.proximity?1:0);if(slot.proximity)for(const auto value:*slot.proximity)w.i32(value);
    w.u8(slot.max_size?1:0);if(slot.max_size)w.i32(*slot.max_size);
    w.u8(slot.unknown_i10?1:0);if(slot.unknown_i10)w.i32(*slot.unknown_i10);
    w.u8(slot.gradient_bits?1:0);if(slot.gradient_bits)w.u32(*slot.gradient_bits);
    w.u8(slot.height?1:0);if(slot.height)w.i32(*slot.height);
    w.u8(slot.facing?1:0);if(slot.facing)w.i32(*slot.facing);
    w.u8(slot.resolution?1:0);if(slot.resolution)w.i32(*slot.resolution);
}
SlotRecord read_slot_record(Reader& r) {
    SlotRecord slot{};slot.type=r.u16();for(auto& value:slot.offset_bits)value=r.u32();
    slot.standing=r.i32();slot.sitting=r.i32();slot.ground=r.i32();slot.flags=r.u32();
    slot.snap_target_slot=r.i32();
    const auto marker=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid source SLOT optional marker");return value!=0;};
    if(marker()){std::array<std::int32_t,3> proximity{};for(auto& value:proximity)value=r.i32();slot.proximity=proximity;}
    if(marker())slot.max_size=r.i32();if(marker())slot.unknown_i10=r.i32();
    if(marker())slot.gradient_bits=r.u32();if(marker())slot.height=r.i32();
    if(marker())slot.facing=r.i32();if(marker())slot.resolution=r.i32();
    return slot;
}
void write_source_circle_goal(Writer& w,const SourceSlotCircleGoal& goal) {
    w.i32(goal.position.x);w.i32(goal.position.y);w.u8(goal.entry_flags);w.u8(goal.facing);
    std::uint32_t radians=0;std::memcpy(&radians,&goal.radian_direction,sizeof(radians));w.u32(radians);
}
SourceSlotCircleGoal read_source_circle_goal(Reader& r) {
    SourceSlotCircleGoal goal;goal.position={r.i32(),r.i32()};goal.entry_flags=r.u8();goal.facing=r.u8();
    const auto radians=r.u32();std::memcpy(&goal.radian_direction,&radians,sizeof(radians));return goal;
}
void write_source_circle(Writer& w,const SourceSlotCircleRouteSnapshot& circle) {
    w.u32(circle.slot_version);
    if(circle.slot_index>UINT32_MAX)throw FormatError("source circle SLOT index exceeds wire bound");
    w.u32(static_cast<std::uint32_t>(circle.slot_index));write_slot_record(w,circle.slot);
    std::uint32_t target=0;std::memcpy(&target,&circle.target_radian_direction,sizeof(target));w.u32(target);
    write_source_circle_goal(w,circle.selected);
    if(circle.fallback_goals.size()>7)throw FormatError("source circle fallback list exceeds eight-goal profile");
    w.u8(static_cast<std::uint8_t>(circle.fallback_goals.size()));
    for(const auto& goal:circle.fallback_goals)write_source_circle_goal(w,goal);
}
SourceSlotCircleRouteSnapshot read_source_circle(Reader& r) {
    SourceSlotCircleRouteSnapshot circle;circle.slot_version=r.u32();circle.slot_index=r.u32();
    circle.slot=read_slot_record(r);const auto target=r.u32();
    std::memcpy(&circle.target_radian_direction,&target,sizeof(target));
    circle.selected=read_source_circle_goal(r);const auto count=r.u8();
    if(count>7||count>r.remaining()/14)throw FormatError("source circle fallback count exceeds bounds");
    circle.fallback_goals.reserve(count);
    for(unsigned n=0;n<count;++n)circle.fallback_goals.push_back(read_source_circle_goal(r));
    return circle;
}
void write_source_range(Writer& w,const SourceSlotRangeRouteSnapshot& range) {
    if(range.slot_index>UINT16_MAX||range.fallback_goals.size()>=4096)
        throw FormatError("source range index or choice count exceeds wire bounds");
    w.u32(range.slot_version);w.u32(static_cast<std::uint32_t>(range.slot_index));write_slot_record(w,range.slot);
    std::uint32_t angle=0;std::memcpy(&angle,&range.target_radian_direction,sizeof(angle));w.u32(angle);
    write_source_circle_goal(w,range.selected);w.u32(static_cast<std::uint32_t>(range.fallback_goals.size()));
    for(const auto& goal:range.fallback_goals)write_source_circle_goal(w,goal);
}
SourceSlotRangeRouteSnapshot read_source_range(Reader& r) {
    SourceSlotRangeRouteSnapshot range;range.slot_version=r.u32();range.slot_index=r.u32();range.slot=read_slot_record(r);
    const auto angle=r.u32();std::memcpy(&range.target_radian_direction,&angle,sizeof(angle));
    range.selected=read_source_circle_goal(r);const auto count=r.u32();
    if(count>=4096||count>r.remaining()/14)throw FormatError("source range fallback count exceeds bounded payload");
    range.fallback_goals.reserve(count);
    for(std::uint32_t n=0;n<count;++n)range.fallback_goals.push_back(read_source_circle_goal(r));
    return range;
}
bool source_choices_unstarted_failure(const LocalRouteSnapshot& route) {
    return route.source_choices&&!route.source_choices->chair_attempted&&!route.source_sparse&&!route.walking_started&&
        route.frame.state==RouteFrameState::room_portal&&route.next==1&&route.path.size()==1&&
        route.path.front()==RoutePoint{route.expected.x,route.expected.y};
}
void validate_source_choices_value(const LocalRouteSnapshot& route) {
    if(!route.source_choices)return;
    const auto& choices=*route.source_choices;
    if(route.source_circle||route.source_range||route.look_towards||
       (route.source_sparse&&route.source_sparse->choices)||(choices.slot_version<4||choices.slot_version>10)||choices.slot_index>65535||
       choices.slot.type!=3||choices.remaining.size()>=4096||!std::isfinite(choices.target_radian_direction)||
       route.path.empty()||route.target_position.direction>7||route.target_position.level<1||
       route.entry_flags!=choices.selected.entry_flags||route.facing!=choices.selected.facing||
       route.face_anywhere!=choices.selected.face_anywhere||
       (!route.arrival_radian_direction||!same_float_bits(*route.arrival_radian_direction,choices.selected.radian_direction)))
        throw FormatError("source SLOT choices conflict with route identity or selected arrival");
    const auto valid=[&](const SourceSlotChoiceGoal& goal){
        if(goal.position.x<INT16_MIN||goal.position.x>INT16_MAX||goal.position.y<INT16_MIN||goal.position.y>INT16_MAX||
           goal.level<1||goal.level>127||goal.level!=route.expected.level||goal.facing>7||!std::isfinite(goal.radian_direction)||
           (goal.chair&&(goal.chair->id<=0||!goal.chair->generation||choices.slot.sitting<=0)))return false;
        return true;
    };
    if(!valid(choices.selected))throw FormatError("source SLOT selected choice is invalid");
    for(const auto& goal:choices.remaining)if(!valid(goal))throw FormatError("source SLOT remaining choice is invalid or duplicated");
    const bool failure=source_choices_unstarted_failure(route);
    if(route.room_route_replan_active){
        if(route.frame.state!=RouteFrameState::initial&&route.frame.state!=RouteFrameState::room_portal)
            throw FormatError("architecture selected-goal room replan has an invalid frame state");
        return; // Full route-stack validation authenticates the actual portal child.
    }
    if(choices.selected.chair&&!choices.chair_attempted){
        if(!failure&&(route.source_sparse||route.walking_started||route.frame.state!=(choices.chair_attempted?RouteFrameState::room_portal:RouteFrameState::initial)||route.next!=1||route.path.size()!=1||
           !(route.path.front()==RoutePoint{route.expected.x,route.expected.y})))
            throw FormatError("source chair must run EP26 before any movement path");
    }else if(!failure&&!is_source_standing_before_walk(route)&&!choices.selected.chair&&
        (choices.chair_attempted||!route.source_sparse||!(route.path.back()==choices.selected.position)))
        throw FormatError("source standing choice has no exact motion goal or retains chair attempt");
}
void write_source_choices(Writer& w,const SourceSlotChoicesSnapshot& choices) {
    if(choices.slot_index>65535||choices.remaining.size()>=4096)throw FormatError("source SLOT choices exceed bounds");
    w.u32(choices.slot_version);w.u32(static_cast<std::uint32_t>(choices.slot_index));write_slot_record(w,choices.slot);
    const auto real=[&](float value){std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));w.u32(bits);};
    real(choices.target_radian_direction);
    const auto goal=[&](const SourceSlotChoiceGoal& g){w.i32(g.position.x);w.i32(g.position.y);w.u8(g.entry_flags);w.u8(g.facing);real(g.radian_direction);
        w.u8(g.face_anywhere);w.u8(bool(g.chair));if(g.chair){w.i16(g.chair->id);w.u64(g.chair->generation);}w.i16(g.level);};
    goal(choices.selected);w.u32(static_cast<std::uint32_t>(choices.remaining.size()));for(const auto& g:choices.remaining)goal(g);
    w.u8(choices.chair_attempted);
}
SourceSlotChoicesSnapshot read_source_choices(Reader& r) {
    SourceSlotChoicesSnapshot choices;choices.slot_version=r.u32();choices.slot_index=r.u32();choices.slot=read_slot_record(r);
    const auto real=[&](){float value=0;const auto bits=r.u32();std::memcpy(&value,&bits,sizeof(value));return value;};
    const auto flag=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid source SLOT choice marker");return value!=0;};
    choices.target_radian_direction=real();
    const auto goal=[&](){SourceSlotChoiceGoal value;value.position={r.i32(),r.i32()};value.entry_flags=r.u8();value.facing=r.u8();value.radian_direction=real();
        value.face_anywhere=flag();if(flag())value.chair=LiveAttemptWalkIdentity{r.i16(),r.u64()};value.level=r.i16();return value;};
    choices.selected=goal();const auto count=r.u32();if(count>=4096||count>r.remaining()/18)throw FormatError("source SLOT remaining choices exceed bounds");
    choices.remaining.reserve(count);for(std::uint32_t n=0;n<count;++n)choices.remaining.push_back(goal());
    choices.chair_attempted=flag();return choices;
}
void write_source_look_route(Writer& w,const SourceLookTowardsRouteSnapshot& look) {
    w.u8(look.mode);
    const auto real=[&](float value){std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));w.u32(bits);};
    real(look.start_radian_direction);real(look.target_radian_direction);w.u8(look.in_pool?1:0);
    w.i16(look.walk_style);w.u8(look.animation_index);real(look.animation_speed);
    w.u32(look.animation_num_frames);real(look.turn_tweak);
}
SourceLookTowardsRouteSnapshot read_source_look_route(Reader& r) {
    SourceLookTowardsRouteSnapshot look;look.mode=r.u8();
    const auto real=[&](){const auto bits=r.u32();float value=0;std::memcpy(&value,&bits,sizeof(value));return value;};
    look.start_radian_direction=real();look.target_radian_direction=real();
    const auto in_pool=r.u8();if(in_pool>1)throw FormatError("invalid LookTowards pool marker");
    look.in_pool=in_pool!=0;look.walk_style=r.i16();look.animation_index=r.u8();
    look.animation_speed=real();look.animation_num_frames=r.u32();look.turn_tweak=real();return look;
}
// Reuse the bounded VM codec for frozen frames, pending traces and calls. These
// value envelopes never execute or recursively contain session state.
VmFrame route_call_frame(const VmCall& call) {
    VmFrame frame;frame.key=call.key;frame.context=call.context;frame.arguments=call.arguments;
    frame.return_mode=call.return_mode;frame.exact_arguments=call.exact_arguments;frame.action_tree=call.action_tree_override.value_or(false);return frame;
}
VmCall route_frame_call(const VmFrame& frame) {
    return {frame.key,frame.context,frame.arguments,frame.return_mode,frame.exact_arguments};
}
std::vector<std::uint8_t> route_vm_values(const VmFrame& frame,const std::optional<VmPending>& pending,
                                       const SessionBundleLimits& limits) {
    VmSnapshot values;values.stack.push_back(frame);values.pending=pending;
    values.action_tree_known=frame.action_tree||frame.context.entity_references_bound||(pending&&pending->context.entity_references_bound);
    return encode_vm_snapshot(values,limits.vm);
}
bool route_frame_equal(const VmFrame& a,const VmFrame& b,const SessionBundleLimits& limits) {
    return route_vm_values(a,std::nullopt,limits)==route_vm_values(b,std::nullopt,limits);
}
bool route_pending_equal(const VmPending& a,const VmPending& b,const SessionBundleLimits& limits) {
    VmFrame frame{};frame.key=routine_key(0);
    return route_vm_values(frame,a,limits)==route_vm_values(frame,b,limits);
}
void validate_carried_route_value(const LocalRouteSnapshot& route,const SessionBundleLimits& limits) {
    const auto& chain=route.carried_target;
    if(chain.empty())return;
    if(chain.size()>4096||route.actor==route.target||
       chain.front().child.id!=route.target||chain.front().child.generation!=route.target_generation||
       !(chain.front().original==route.target_position)||
       chain.back().parent.id!=route.actor||chain.back().parent.generation!=route.actor_generation)
        throw FormatError("carried route ancestry endpoints or anchor are invalid");
    std::set<std::int16_t> children;
    for(std::size_t n=0;n<chain.size();++n){const auto& link=chain[n];
        if(link.child.id<=0||link.parent.id<=0||!link.child.generation||!link.parent.generation||
           link.child.id==route.actor||!children.insert(link.child.id).second||
           link.slot>=limits.world.world.bank_words||!std::isfinite(link.radian_direction)||
           link.original.direction>7||link.original.level<1||
           link.original.x<0||link.original.x>32767||link.original.y<0||link.original.y>32767||
           (n&&(chain[n-1].parent.id!=link.child.id||chain[n-1].parent.generation!=link.child.generation)))
            throw FormatError("carried route ancestry link is invalid");
    }
}
void validate_local_route_links(const SessionRuntimeEntity& entity,const Entity& actor,
                                const std::vector<Entity>& world,const SessionBundleLimits& limits,
                                bool check_pose=true,bool deferred=false,bool nested_request=false,bool external_frame_active=false,bool parked_failure_tree=false,bool external_standing=false,bool failed_selected_attempt=false) {
    const auto& vm=entity.vm;
    const auto* pending_instruction=vm.pending?&vm.pending->instruction.instruction:nullptr;
    const bool legacy_go_here=pending_instruction&&
        pending_instruction->opcode==27&&
        pending_instruction->operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0};
    const auto ordinary_target=vm.pending?std::find_if(world.begin(),world.end(),[&](const Entity& value){return value.id==vm.pending->context.stack_object&&(!vm.pending->context.entity_references_bound||value.generation==vm.pending->context.stack_object_generation);}):world.end();
    // Historical native GoHere used its own exact Destination marker profile,
    // not the source SLOT/AnyDirection contract. Preserve that old wire state
    // only with the marker's generation and recorded dynamic owner authority.
    const bool legacy_destination=legacy_go_here&&entity.route&&ordinary_target!=world.end()&&
        ordinary_target->ui_ghost&&ordinary_target->definition&&ordinary_target->definition->guid==0x7c4&&
        std::any_of(entity.dynamic_action_owners.begin(),entity.dynamic_action_owners.end(),[&](const auto& owner){
            return owner.owner==vm.pending->context.code_owner&&owner.target_id==ordinary_target->id&&
                owner.target_generation==ordinary_target->generation&&owner.target_guid==0x7c4&&
                owner.root_routine==4096&&!owner.linked_semiglobal;
        });
    const bool ordinary_go_here=pending_instruction&&ordinary_target!=world.end()&&!ordinary_target->ui_ghost&&
        is_source_ordinary_go_here_instruction(*pending_instruction);
    const auto relative_request=pending_instruction?decode_source_goto_relative_position(*pending_instruction):
        std::optional<SourceGotoRelativePositionRequest>{};
    const bool exact_relative=pending_instruction&&
        ((!legacy_destination&&is_source_exact_goto_relative_instruction(*pending_instruction))||
         (ordinary_go_here&&entity.route&&(entity.route->source_sparse||deferred)));
    // GotoRelative creates a transient ranged SLOT before planning a selected
    // goal. Only a deferred room request can retain this operand-only shape;
    // it is not an exact OnTopOf request or an invented selected SLOT snapshot.
    const bool deferred_ranged_relative=deferred&&pending_instruction&&
        is_source_ranged_goto_relative_instruction(*pending_instruction);
    const bool selected_ranged_relative=entity.route&&pending_instruction&&
        ((entity.route->source_range&&is_source_ranged_goto_relative_instruction(*pending_instruction))||
         (entity.route->source_choices&&is_source_goto_relative_slot_instruction(*pending_instruction)));
    const bool route45=pending_instruction&&supported_route45_instruction(*pending_instruction);
    const bool source_circle=entity.route&&entity.route->source_circle.has_value();
    const bool source_range=entity.route&&entity.route->source_range.has_value();
    const bool source_sparse=entity.route&&entity.route->source_sparse.has_value();
    const bool source_choices=entity.route&&entity.route->source_choices.has_value();
    const bool source_generic=source_choices||(source_sparse&&entity.route->source_sparse->choices.has_value());
    const bool look_towards=entity.route&&entity.route->look_towards.has_value();
    const bool look_towards_instruction=pending_instruction&&pending_instruction->opcode==22&&
        pending_instruction->operands[0]>=2&&pending_instruction->operands[0]<=5;
    const bool source_point=entity.route&&entity.route->arrival_radian_direction.has_value()&&!source_circle&&!source_range&&!source_generic;
    const bool source_circle_instruction=source_circle&&pending_instruction&&
        source_circle_call_resolves_index(vm,*entity.route,*pending_instruction);
    const bool source_range_instruction=source_range&&pending_instruction&&
        source_range_call_resolves_index(vm,*entity.route,*pending_instruction);
    const bool failure_trees=!nested_request&&(legacy_go_here||((exact_relative||deferred_ranged_relative||selected_ranged_relative)&&relative_request&&!relative_request->no_failure_trees)||
        ((deferred||source_point||source_circle||source_range||source_generic)&&route45&&(pending_instruction->operands[4]&1U)==0));
    const bool pending_route=route45||legacy_go_here||exact_relative||deferred_ranged_relative||selected_ranged_relative||look_towards_instruction;
    if(pending_route!=entity.route.has_value()){
        std::string operands;
        if(pending_instruction)for(const auto value:pending_instruction->operands)
            operands+=(operands.empty()?"":",")+std::to_string(value);
        throw FormatError("session route wait and local route continuation do not match (actor="+
            std::to_string(entity.id)+", routine="+std::to_string(vm.pending?vm.pending->instruction.key.id:0)+
            ", pc="+std::to_string(vm.pending?vm.pending->instruction.pc:0)+
            ", opcode="+std::to_string(pending_instruction?pending_instruction->opcode:0)+
            ", operands=["+operands+"], pending_route="+std::to_string(pending_route)+
            ", local_route="+std::to_string(entity.route.has_value())+
            ", route45="+std::to_string(route45)+", legacy_relative="+std::to_string(legacy_go_here)+
            ", exact_relative="+std::to_string(exact_relative)+", ordinary_go_here="+std::to_string(ordinary_go_here)+
            ", deferred_ranged_relative="+std::to_string(deferred_ranged_relative)+
            ", look_towards="+std::to_string(look_towards_instruction)+
            ", sparse="+std::to_string(source_sparse)+", circle="+std::to_string(source_circle)+
            ", range="+std::to_string(source_range)+", deferred="+std::to_string(deferred)+
            ", nested="+std::to_string(nested_request)+", external="+std::to_string(external_frame_active)+")");
    }
    if(!entity.route)return;
    const auto& route=*entity.route;
    const bool self_target=route.target==entity.id;
    // A source exact-point request may retain its original target angle even
    // when that distinct target has moved. Old point archives omit this value.
    if((!look_towards&&self_target&&!route.target_anchor_radian_direction)||
       (!self_target&&route.target_anchor_radian_direction&&!source_point)||
       (route.target_anchor_radian_direction&&!std::isfinite(*route.target_anchor_radian_direction))||
       (self_target&&!look_towards&&((!route45&&!selected_ranged_relative)||route.actor_generation!=route.target_generation||
        !std::isfinite(*route.target_anchor_radian_direction)||route.target_position.direction>7||
        route.target_position.level<1||route.target_position.x<0||route.target_position.x>32767||
        route.target_position.y<0||route.target_position.y>32767||route.path.empty()||
        (!source_circle&&!source_range&&!source_choices&&!look_towards&&!source_sparse&&!(route.path.front()==RoutePoint{route.target_position.x,route.target_position.y})))))
        throw FormatError("self-target route has no valid frozen request anchor");
    if(source_circle&&!source_circle_instruction)
        throw FormatError("source circle continuation does not match its source SLOT call");
    if(source_circle)validate_source_circle_value(route);
    if(source_range){
        if(!source_range_instruction)throw FormatError("source range continuation does not match its source SLOT call");
        validate_source_range_value(route);
    }
    if(source_choices){
        validate_source_choices_value(route);
        if(!route.room_route_replan_active&&source_choices_unstarted_failure(route)&&(!parked_failure_tree||!route.call_failure_trees))
            throw FormatError("source SLOT has no motion outside its authenticated failure-tree continuation");
        const auto& choices=*route.source_choices;
        bool matches=false;
        if(pending_instruction&&is_source_goto_relative_slot_instruction(*pending_instruction)){
            auto probe=route;SourceSlotRangeRouteSnapshot range;range.slot_version=choices.slot_version;range.slot_index=choices.slot_index;range.slot=choices.slot;
            probe.source_range=range;matches=source_range_call_resolves_index(vm,probe,*pending_instruction);
        }else if(route45&&!vm.stack.empty()){
            const auto data=static_cast<std::size_t>(pending_instruction->operands[0]|(pending_instruction->operands[1]<<8));
            const auto scope=pending_instruction->operands[2]|(pending_instruction->operands[3]<<8);
            const auto index=scope==0?(data<vm.stack.back().arguments.size()&&vm.stack.back().arguments[data]>=0?
                static_cast<std::size_t>(vm.stack.back().arguments[data]):std::size_t{65536}):data;
            matches=index==choices.slot_index;
        }
        if(!matches)throw FormatError("source SLOT choices do not match their original instruction/index");
        const auto live_chair=[&](const SourceSlotChoiceGoal& goal){if(!goal.chair)return;
            const auto chair=std::find_if(world.begin(),world.end(),[&](const auto& e){return e.id==goal.chair->id&&e.generation==goal.chair->generation;});
            if(chair==world.end()||chair->kind!=EntityKind::object||chair->ui_ghost||!chair->entry_points||
               chair->entry_points->size()<=26||chair->entry_points->at(26).action==0)
                throw FormatError("source SLOT chair generation or EP26 is unavailable");};
        live_chair(choices.selected);for(const auto& goal:choices.remaining)live_chair(goal);
    }
    if(look_towards){
        if(!look_towards_instruction||route.look_towards->mode!=pending_instruction->operands[0])
            throw FormatError("LookTowards continuation does not match pending opcode 22");
        validate_source_look_route_value(route,limits);
    }else if(look_towards_instruction)
        throw FormatError("pending body LookTowards has no local turn route");
    if(source_point&&(!route45||route.entry_flags!=0||!std::isfinite(*route.arrival_radian_direction)))
        throw FormatError("continuous arrival direction requires a source exact-point SLOT route");
    if(deferred_ranged_relative&&(route.entry_flags!=0||route.facing!=0||
       route.face_anywhere!=(pending_instruction->operands[3]==255)||source_circle||source_range||source_sparse||source_choices||
       look_towards||route.arrival_radian_direction))
        throw FormatError("deferred ranged relative request has a selected goal or changed facing policy");
    if(route.call_failure_trees!=failure_trees||(!route45&&!legacy_go_here&&!exact_relative&&!deferred_ranged_relative&&!selected_ranged_relative&&!look_towards)||
       (exact_relative&&(route.entry_flags!=0||route.face_anywhere!=(pending_instruction->operands[3]==255))))
        throw FormatError("session local route does not match its Route45 or exact relative instruction actor="+std::to_string(entity.id)+
            " routine="+std::to_string(vm.pending?vm.pending->instruction.key.id:0)+" pc="+std::to_string(vm.pending?vm.pending->instruction.pc:0)+
            " opcode="+std::to_string(pending_instruction?pending_instruction->opcode:0)+" flags="+std::to_string(pending_instruction?pending_instruction->operands[6]:0)+
            " nested="+std::to_string(nested_request)+" failure_actual="+std::to_string(route.call_failure_trees)+" failure_expected="+std::to_string(failure_trees)+
            " exact_relative="+std::to_string(exact_relative)+" entry="+std::to_string(route.entry_flags)+" face_any="+std::to_string(route.face_anywhere)+
            " direction="+std::to_string(pending_instruction?pending_instruction->operands[3]:0));
    if(vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=route.ticket||
       !route.ticket||route.ticket>vm.ticket_sequence||route.actor!=entity.id||
       route.actor_generation!=actor.generation||
       route.run_generation!=vm.run_generation||(!deferred&&!source_sparse&&!route.same_room_verified&&!failed_selected_attempt)||
       (!deferred&&route.expected.level!=route.target_position.level))
        throw FormatError("session local Route45 ticket or identity is inconsistent");
    const bool verified_destination_owner=std::any_of(entity.dynamic_action_owners.begin(),
        entity.dynamic_action_owners.end(),[&](const auto& owner){
            return owner.owner==vm.pending->context.code_owner&&owner.target_id==route.target&&
                owner.target_guid==0x7c4&&owner.root_routine==4096&&!owner.linked_semiglobal;
        });
    const bool verified_resource_owner=std::any_of(entity.resource_owners.begin(),entity.resource_owners.end(),
        [&](const auto& owner){return owner.owner==vm.pending->context.code_owner;});
    if(vm.pending->context.caller!=entity.id||(!look_towards&&vm.pending->context.stack_object!=route.target)||
       (vm.pending->context.code_owner!=static_cast<CodeOwner>(entity.id)&&!verified_destination_owner&&!verified_resource_owner)||
       vm.stack.empty()||
       vm.pending->instruction.depth!=vm.stack.size()||vm.pending->context.caller!=vm.stack.back().context.caller||
       vm.pending->context.entity_references_bound!=vm.stack.back().context.entity_references_bound||
       vm.pending->context.caller_generation!=vm.stack.back().context.caller_generation||
       (look_towards&&(vm.pending->context.callee!=vm.stack.back().context.callee||vm.pending->context.callee_generation!=vm.stack.back().context.callee_generation))||
       vm.pending->context.stack_object!=vm.stack.back().context.stack_object||
       vm.pending->context.stack_object_generation!=vm.stack.back().context.stack_object_generation||
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
    if(!vm.suspended_pending.empty()||vm.external_frame_return||
       (!external_frame_active&&(entity.controllers.sleep.job||interactions.wait))||
       (interactions.active.has_value()!=!interactions.actions.empty()))
        throw FormatError("session local Route45 overlaps another pending service");
    if(deferred){
        if(!route.path.empty()||route.next!=1||route.same_room_verified||route.frame.state!=RouteFrameState::room_portal)
            throw FormatError("deferred room route has invalid traversal state");
        return;
    }
    const bool standing_before_walk=!failed_selected_attempt&&is_source_standing_before_walk(route);
    if(standing_before_walk&&((route.frame.state==RouteFrameState::stand_func&&!external_standing)||
       (route.frame.state==RouteFrameState::initial&&(actor.person_data.empty()||actor.person_data[0]!=1))))
        throw FormatError("source standing phase has no actual posture or external EP27");
    if(check_pose&&!look_towards&&actor.attachment&&!standing_before_walk)throw FormatError("session local Route45 actor is attached");
    if(check_pose&&!look_towards&&!standing_before_walk&&(actor.person_data.empty()||actor.person_data[0]!=0))
        throw FormatError("session local Route45 actor is not standing");
    if(check_pose&&(!actor.position||!(*actor.position==route.expected)))
        throw FormatError("session local Route45 actor position differs from its progress (actor="+
            (actor.position?std::to_string(actor.position->x)+","+std::to_string(actor.position->y)+","+
                std::to_string(actor.position->level)+","+std::to_string(actor.position->direction):"missing")+
            ", route="+std::to_string(route.expected.x)+","+std::to_string(route.expected.y)+","+
            std::to_string(route.expected.level)+","+std::to_string(route.expected.direction)+")");
    const bool shooed_pose=source_sparse&&route.source_sparse->walk_to_purpose==SourceWalkToPurpose::shoo&&
        route.frame.state==RouteFrameState::shooed;
    if(check_pose&&((!look_towards&&!shooed_pose&&actor.static_footprint==route.walking_started)||
       (look_towards&&route.walking_started)))
        throw FormatError("session local Route45 footprint differs from its progress");
    if(!std::isfinite(route.expected_radian_direction)||(check_pose&&actor.radian_direction!=route.expected_radian_direction))
        throw FormatError("session local Route45 actor direction differs from its progress");
    if(source_sparse){
        if(pending_instruction&&is_source_ordinary_go_here_instruction(*pending_instruction)&&!ordinary_go_here)
            throw FormatError("ordinary GoHere sparse route cannot target a historical UI ghost");
        const auto& sparse=*route.source_sparse;
        const bool failure_rejected_motion=parked_failure_tree&&sparse.collision&&sparse.motion.phase==WalkMotionPhase::collision_required&&
            (sparse.collision->phase==SourceRouteCollisionPhase::ready||sparse.collision->phase==SourceRouteCollisionPhase::soft_failure);
        const bool shoo=sparse.walk_to_purpose==SourceWalkToPurpose::shoo;
        const bool architecture_walk=sparse.walk_to_purpose==SourceWalkToPurpose::architecture_invalidation;
        if(sparse.walk_to_purpose!=SourceWalkToPurpose::ranked_alternative&&!shoo&&!architecture_walk)
            throw FormatError("unknown source WalkTo-null purpose");
        if(architecture_walk&&(!sparse.walk_to_pending||route.path.empty()||!route.next||route.next>route.path.size()))
            throw FormatError("architecture WalkTo-null lacks its historical source path");
        const bool shooed=shoo&&route.frame.state==RouteFrameState::shooed;
        const bool externally_parked=external_frame_active&&(route.frame.state==RouteFrameState::room_portal||shooed);
        if(shoo&&(!sparse.walk_to_pending||sparse.avatars_to_consider.empty()||sparse.lifecycle==1||
           !((sparse.lifecycle==0&&(shooed||route.frame.state==RouteFrameState::walking||
                    route.frame.state==RouteFrameState::turn_only||externally_parked))||
             (sparse.lifecycle==2&&route.frame.state==RouteFrameState::end_turn&&sparse.turn))))
            throw FormatError("source shoo pending walk has an invalid lifecycle");
        if(look_towards||sparse.lifecycle>2||sparse.face_anywhere!=route.face_anywhere||
           !std::isfinite(sparse.target_radian_direction)||
           (sparse.lifecycle==1&&((!externally_parked&&route.frame.state!=RouteFrameState::begin_turn)||!sparse.turn))||
           (sparse.lifecycle==2&&((!externally_parked&&route.frame.state!=RouteFrameState::end_turn)||!sparse.turn))||
           (sparse.lifecycle==0&&((sparse.turn&&!(shoo&&(shooed||externally_parked)))||(!externally_parked&&!shooed&&route.frame.state!=RouteFrameState::walking&&
                                             route.frame.state!=RouteFrameState::turn_only&&!(architecture_walk&&route.frame.state==RouteFrameState::initial)))))
            throw FormatError("source sparse route lifecycle does not match its motion state");
        if(sparse.walk_to_pending&&!shoo&&!architecture_walk&&((!route45&&!selected_ranged_relative)||(!source_circle&&!source_range&&!sparse.choices&&!source_choices)||
           !((sparse.lifecycle==0&&(route.frame.state==RouteFrameState::walking||route.frame.state==RouteFrameState::turn_only||externally_parked))||
             (sparse.lifecycle==2&&route.frame.state==RouteFrameState::end_turn&&sparse.turn))||!sparse.motion.current||
           route.next!=1||route.path.empty()||!(route.path.front()==RoutePoint{route.expected.x,route.expected.y})||
           ((route.frame.state==RouteFrameState::turn_only||sparse.lifecycle==2)&&
             !(route.path.back()==RoutePoint{route.expected.x,route.expected.y}))))
            throw FormatError("pending fallback WalkTo has no exact ranked request or retained current path");
        if(shoo&&((route.frame.state==RouteFrameState::turn_only||sparse.lifecycle==2)&&
           (route.path.empty()||!(route.path.back()==RoutePoint{route.expected.x,route.expected.y}))))
            throw FormatError("source shoo TURN_ONLY goal is not its current position");
        if(!sparse.fallback_room_route.empty()&&!architecture_walk&&((!route45&&!selected_ranged_relative)||(!source_circle&&!source_range&&!sparse.choices&&!source_choices)))
            throw FormatError("fallback room route has no exact ranked SLOT request");
        if(sparse.fallback_room_route.size()>32)throw FormatError("fallback room route exceeds bounds");
        std::set<std::tuple<std::uint16_t,std::uint16_t,std::int16_t>> room_edges;
        for(std::size_t i=0;i<sparse.fallback_room_route.size();++i){const auto& edge=sparse.fallback_room_route[i];
            const auto portal=std::find_if(world.begin(),world.end(),[&](const auto& value){return value.id==edge.portal_id;});
            if(edge.source_room==edge.target_room||edge.portal_id<=0||portal==world.end()||portal->kind!=EntityKind::object||
               !portal->position||!(*portal->position==edge.position)||
               (i&&sparse.fallback_room_route[i-1].target_room!=edge.source_room)||
               !room_edges.insert({edge.source_room,edge.target_room,edge.portal_id}).second)
                throw FormatError("fallback room route has stale portal geometry or invalid ordered edges");
        }
        if(sparse.motion.current){WalkMotion verifier;
            if(!sparse.room||sparse.motion.previous_position.level!=route.expected.level||
               (sparse.walk_to_pending?
                    (sparse.motion.phase!=WalkMotionPhase::walking&&sparse.motion.phase!=WalkMotionPhase::collision_required&&
                     sparse.motion.phase!=WalkMotionPhase::finished):
                    (!failure_rejected_motion&&sparse.motion.phase!=(sparse.lifecycle==2?WalkMotionPhase::finished:WalkMotionPhase::walking)&&
                     !(sparse.lifecycle==0&&sparse.collision&&sparse.collision->phase==SourceRouteCollisionPhase::waiting&&
                       sparse.motion.phase==WalkMotionPhase::collision_required)))||
               verifier.restore(sparse.motion)!=WalkMotionStatus::success)
                throw FormatError("source sparse route motion geometry/counters are invalid");
        }else if(!shoo&&!externally_parked&&route.frame.state!=RouteFrameState::turn_only&&sparse.lifecycle!=2)
            throw FormatError("source sparse route has no movement path outside its turning-only lifecycle");
        if(sparse.turn){const auto& turn=*sparse.turn;
            if(!std::isfinite(turn.start_radian_direction)||!std::isfinite(turn.target_radian_direction)||
               !std::isfinite(turn.turn_tweak)||!std::isfinite(turn.animation_speed)||
               turn.animation_index<4||turn.animation_index>9||turn.animation_num_frames>limits.max_playback_frames||
               (turn.animation_speed!=1&&turn.animation_speed!=2)||turn.in_pool!=sparse.profile.in_pool)
                throw FormatError("source sparse route turn animation is invalid");
        }
        if(sparse.choices){
            const auto& choices=*sparse.choices;
            if(source_circle||source_range||!route45||choices.slot_version!=10||choices.slot.type!=3||
               choices.slot.standing<=0||choices.slot_index>65535||choices.fallback_goals.size()>=4096||
               !std::isfinite(choices.target_radian_direction)||route.path.empty()||
               !(choices.selected.position==route.path.back())||choices.selected.entry_flags!=route.entry_flags||
               choices.selected.facing!=route.facing)
                throw FormatError("sparse ranked SLOT choices do not match the selected standing goal");
            const auto& instruction=*pending_instruction;
            const auto operand=static_cast<std::size_t>(instruction.operands[0]|(instruction.operands[1]<<8));
            const auto scope=static_cast<unsigned>(instruction.operands[2]|(instruction.operands[3]<<8));
            const auto resolved=scope==0?(operand<vm.stack.back().arguments.size()&&vm.stack.back().arguments[operand]>=0?
                static_cast<std::size_t>(vm.stack.back().arguments[operand]):std::size_t{65536}):operand;
            if(resolved!=choices.slot_index)throw FormatError("sparse ranked SLOT index differs from the pending instruction");
            std::set<std::pair<std::int32_t,std::int32_t>> positions;
            const auto choice_valid=[&](const SourceSlotCircleGoal& goal){
                return goal.position.x>=INT16_MIN&&goal.position.x<=INT16_MAX&&goal.position.y>=INT16_MIN&&
                    goal.position.y<=INT16_MAX&&goal.entry_flags&&goal.facing<8&&std::isfinite(goal.radian_direction)&&
                    positions.insert({goal.position.x,goal.position.y}).second;
            };
            if(!choice_valid(choices.selected))throw FormatError("sparse selected SLOT choice is invalid");
            for(const auto& goal:choices.fallback_goals)if(!choice_valid(goal))throw FormatError("sparse remaining SLOT choice is invalid");
        }
        if(sparse.collision){
            SourceRouteCollision verifier;if(!verifier.restore(*sparse.collision))throw FormatError("invalid source collision continuation");
            if(route.frame.wait_time.value_or(0)!=static_cast<std::uint32_t>(sparse.collision->wait_time)||
               sparse.collision->considered.size()!=sparse.avatars_to_consider.size())
                throw FormatError("source collision policy differs from route wait or consideration state");
            for(std::size_t i=0;i<sparse.collision->considered.size();++i)
                if(sparse.collision->considered[i].id!=sparse.avatars_to_consider[i].id||
                   sparse.collision->considered[i].generation!=sparse.avatars_to_consider[i].generation)
                    throw FormatError("source collision consideration order differs from route state");
            for(const auto& identity:sparse.collision->considered){const auto found=std::find_if(world.begin(),world.end(),[&](const auto& value){
                    return value.id==identity.id&&value.generation==identity.generation&&value.kind==EntityKind::avatar;});
                if(found==world.end())throw FormatError("source collision consideration identity is stale");}
            if(!sparse.walk_to_pending&&!failure_rejected_motion&&
               (sparse.collision->phase==SourceRouteCollisionPhase::waiting)!=(sparse.motion.phase==WalkMotionPhase::collision_required))
                throw FormatError("source collision policy and motion phase differ");
        }
        if(sparse.avatars_to_consider.size()>4096)throw FormatError("sparse avatar consideration exceeds bounds");
        std::set<std::int16_t> considered;
        for(const auto identity:sparse.avatars_to_consider){
            const auto found=std::find_if(world.begin(),world.end(),[&](const auto& value){
                return value.id==identity.id&&value.generation==identity.generation&&value.kind==EntityKind::avatar;});
            if(identity.id<=0||!identity.generation||!considered.insert(identity.id).second||found==world.end())
                throw FormatError("sparse avatar consideration identity is stale or duplicated");
        }
    }
    // CanPortalTurn on a nested source route consumes the ROOM_PORTAL
    // parent's counter. An EP26 parent has no walking motion of its own, but
    // must retain that counter while its genuine external child executes.
    const bool chair_parent_turns=external_frame_active&&source_choices&&
        route.source_choices->selected.chair&&route.source_choices->chair_attempted&&
        route.frame.state==RouteFrameState::room_portal;
    if((!source_sparse&&((route.frame.portal_turns&&!chair_parent_turns)||route.frame.wait_time||route.frame.retries_remaining||
       (!look_towards&&route.frame.state!=RouteFrameState::initial&&route.frame.state!=RouteFrameState::walking&&
        route.frame.state!=RouteFrameState::turn_only&&
        !(external_frame_active&&route.frame.state==RouteFrameState::room_portal)&&
        !(external_standing&&route.frame.state==RouteFrameState::stand_func))||route.next==0||route.next>route.path.size()||
       !(route.path[route.next-1]==RoutePoint{route.expected.x,route.expected.y})))||route.path.empty()||
       route.path.size()>limits.max_route_points||route.next==0||route.next>UINT32_MAX||
       route.facing>7||(route.entry_flags<=0&&!((route.call_failure_trees||exact_relative||source_point||source_circle||source_choices)&&route.entry_flags==0)))
        throw FormatError("session local Route45 path or arrival state is invalid actor="+std::to_string(route.actor)+
            " target="+std::to_string(route.target)+" state="+std::to_string(static_cast<unsigned>(route.frame.state))+
            " portal_turns="+std::to_string(route.frame.portal_turns)+" wait="+(route.frame.wait_time?std::to_string(*route.frame.wait_time):"absent")+
            " retries="+(route.frame.retries_remaining?std::to_string(*route.frame.retries_remaining):"absent")+
            " next="+std::to_string(route.next)+" path="+std::to_string(route.path.size())+
            " chair="+std::to_string(source_choices&&route.source_choices->selected.chair?route.source_choices->selected.chair->id:0)+
            " external="+std::to_string(external_frame_active));
    const auto target=std::find_if(world.begin(),world.end(),[&](const auto& value){
        return value.id==route.target&&value.generation==route.target_generation;
    });
    if(target==world.end()||(!source_circle&&!source_range&&!source_choices&&!target->position)||
       (!self_target&&!deferred&&!route_has_captured_source_goals(route)&&route.carried_target.empty()&&!(*target->position==route.target_position)))
        throw FormatError("session local Route45 target identity or position is stale");
}

void validate_route_links(const SessionRuntimeEntity& entity,const Entity& actor,
                          const std::vector<Entity>& world,std::uint64_t tick,const SessionBundleLimits& limits) {
    if(!entity.route_stack){
        if(entity.route&&entity.route->room_route_replan_active)throw FormatError("architecture room replan requires its full route stack");
        if(entity.route&&entity.route->source_choices&&entity.route->source_choices->chair_attempted)
            throw FormatError("source chair attempt requires an authenticated external route frame");
        validate_local_route_links(entity,actor,world,limits);return;
    }
    if(entity.route)throw FormatError("legacy route and route stack are mutually exclusive");
    const auto& stack=*entity.route_stack;const auto& vm=entity.vm;
    if(stack.jobs.size()>limits.vm.vm.stack_depth||
       (stack.last_tick&&*stack.last_tick>tick))throw FormatError("route stack size or clock is invalid");
    if(stack.portal_owners.size()>limits.max_resource_owners_per_thread||stack.portal_returns>stack.portal_planned_calls||
       stack.portal_transitions>stack.portal_returns)throw FormatError("route portal host counters are inconsistent");
    std::set<std::tuple<std::string,std::string,std::uint16_t>> portal_owners;
    for(const auto& owner:stack.portal_owners){
        if(owner.source.empty()||owner.source.front()=='/'||owner.source.find('\\')!=std::string::npos||
           owner.source.find(':')!=std::string::npos||owner.source.find("..")!=std::string::npos||
           !owner.action||!owner.owner||!portal_owners.insert({owner.source,owner.asset,owner.action}).second)
            throw FormatError("route portal owner manifest is invalid or duplicated");
        const auto installed=std::find_if(entity.resource_owners.begin(),entity.resource_owners.end(),[&](const auto& resource){
            return resource.owner==owner.owner&&resource.source==owner.source&&resource.asset==owner.asset;});
        if(installed==entity.resource_owners.end())throw FormatError("route portal owner has no exact resource provenance");
    }
    if(stack.jobs.empty()){
        if(!stack.portal_contexts.empty()||(stack.portal_owners.empty()&&!stack.portal_planned_calls&&
           !stack.portal_returns&&!stack.portal_transitions))throw FormatError("cache-only route stack has no meaningful host state");
        validate_local_route_links(entity,actor,world,limits);return;
    }

    std::set<std::uint64_t> tickets,context_tickets,suspended;
    for(const auto& pending:vm.suspended_pending)if(!suspended.insert(pending.ticket).second)
        throw FormatError("duplicate suspended route ticket");
    std::size_t previous_depth=0;std::set<std::uint64_t> external_tickets;
    const auto live=[&](std::int16_t id,std::uint64_t generation=0)->const Entity*{
        const auto found=std::find_if(world.begin(),world.end(),[&](const auto& value){return value.id==id&&(!generation||value.generation==generation);});
        return found==world.end()?nullptr:&*found;};
    for(std::size_t i=0;i<stack.jobs.size();++i){
        const auto& job=stack.jobs[i];const bool active=i+1==stack.jobs.size();const auto depth=job.pending.instruction.depth;
        const auto parked=std::find_if(vm.immediate_continuations.begin(),vm.immediate_continuations.end(),
            [&](const auto& continuation){return continuation.parent_pending&&continuation.parent_pending->ticket==job.ticket;});
        const bool immediate_parent=parked!=vm.immediate_continuations.end();
        if(immediate_parent&&(parked->parent_status!=VmStatus::waiting||parked->parent_depth!=depth||
           !route_pending_equal(job.pending,*parked->parent_pending,limits)))
            throw FormatError("route parked parent differs from its exact immediate pending");
        const bool shoo=job.external_frame_purpose==RouteExternalFramePurpose::shoo;
        const bool standing=job.external_frame_purpose==RouteExternalFramePurpose::standing;
        if((job.external_frame_purpose&&!shoo&&!standing)||(!shoo&&!standing&&job.external_frame_callee_generation))
            throw FormatError("route external purpose/callee generation is invalid");
        if(standing){
            if(!job.external_frame_active||job.external_frame_sitting||!job.external_frame_call||!job.external_frame_callee_generation||
               !is_source_standing_before_walk(job)||job.frame.state!=RouteFrameState::stand_func)
                throw FormatError("source standing external phase is inconsistent");
            const auto& call=*job.external_frame_call;const auto chair=live(call.context.callee,job.external_frame_callee_generation);
            if(!chair||!chair->entry_points||chair->entry_points->size()<=27||call.key.id!=chair->entry_points->at(27).action||
               !context_role_matches(call.context,VmEntityRole::caller,entity.id,actor.generation)||
               !context_role_matches(call.context,VmEntityRole::callee,chair->id,chair->generation)||
               !context_role_matches(call.context,VmEntityRole::stack_object,chair->id,chair->generation)||!call.exact_arguments||call.arguments.size()>255||
               std::any_of(call.arguments.begin(),call.arguments.end(),[](auto value){return value!=0;}))
                throw FormatError("source standing external EP27 identity or arguments differ");
        }
        if(shoo){
            if(!job.external_frame_active||!job.source_sparse||!job.source_sparse->walk_to_pending||
               job.source_sparse->walk_to_purpose!=SourceWalkToPurpose::shoo||job.frame.state!=RouteFrameState::shooed||
               job.failure_tree_invoked||job.external_frame_sitting||!job.external_frame_call||!job.external_frame_callee_generation)
                throw FormatError("source shoo external frame lifecycle is inconsistent");
            const auto& call=*job.external_frame_call;
            if(call.key.scope!=RoutineScope::private_object||call.key.id!=4107||call.context.caller!=entity.id||
               call.context.stack_object!=call.context.callee||!call.exact_arguments||call.arguments.size()<4||call.arguments.size()>255)
                throw FormatError("source shoo external frame call identity or extent changed");
            for(std::size_t n=0;n<call.arguments.size();++n)if(call.arguments[n]!=(n<4?0:-1))
                throw FormatError("source shoo external default arguments changed");
            const auto destination=live(call.context.callee);
            const bool returned=active&&vm.external_frame_return&&vm.external_frame_return->ticket==job.ticket;
            if((!destination&&!returned)||(destination&&(destination->generation!=job.external_frame_callee_generation||
               destination->kind!=EntityKind::object||!destination->definition||destination->definition->guid!=0x7c4)))
                throw FormatError("source shoo external Destination generation is stale");
        }
        if(job.room_route_replan_active&&(job.deferred_room_route||job.look_towards||job.failure_tree_invoked||
           (job.frame.state!=RouteFrameState::initial&&job.frame.state!=RouteFrameState::room_portal)||
           (job.external_frame_active&&job.frame.state!=RouteFrameState::room_portal)||
           (!job.source_choices&&!job.source_sparse&&job.path.empty())))
            throw FormatError("architecture room replan has no source goal or supported phase");
        if(!job.ticket||!tickets.insert(job.ticket).second||job.pending.ticket!=job.ticket||depth<=previous_depth||depth>vm.stack.size()||
           !route_frame_equal(job.route_frame,vm.stack[depth-1],limits)||!std::isfinite(job.expected_radian_direction)||
           job.portal_retries>32||job.portal_steps>32||
           job.replay_frame.has_value()!=job.replay_instruction.has_value()||(!active&&!job.external_frame_active&&!immediate_parent))
            throw FormatError("route job frozen VM frame or nesting is inconsistent");
        if(job.pending.instruction.key.scope!=job.route_frame.key.scope||job.pending.instruction.key.id!=job.route_frame.key.id||
           job.pending.instruction.pc!=job.route_frame.pc||job.pending.instruction.code_owner!=job.route_frame.context.code_owner)
            throw FormatError("route pending trace differs from its frozen instruction frame");
        previous_depth=depth;
        const auto target=live(job.target,job.target_generation);
        if(!target||job.actor!=entity.id||job.actor_generation!=actor.generation||job.run_generation!=vm.run_generation)
            throw FormatError("route stack actor or target identity is stale");
        if(job.selected_sitting_object&&!live(*job.selected_sitting_object))throw FormatError("route selected sitting object is stale");
        for(const auto& goal:job.fallback_goals){
            if((!goal.entry_flags&&!job.source_choices)||goal.facing>7||(goal.radian_direction&&!std::isfinite(*goal.radian_direction))||
               (goal.sitting_object&&!live(*goal.sitting_object))||(goal.sitting_approach&&!goal.sitting_object))
                throw FormatError("route fallback goal is invalid");
        }
        if(job.source_choices){
            const auto& choices=*job.source_choices;const auto& selected=choices.selected;
            if(job.failure_pending){
                const auto& instruction=job.pending.instruction.instruction;
                const auto relative=instruction.opcode==27?decode_source_goto_relative_position(instruction):std::nullopt;
                const bool failure_policy=instruction.opcode==27?relative&&!relative->no_failure_trees:
                    instruction.opcode==45&&(instruction.operands[4]&1U)==0;
                if(!job.call_failure_trees||!failure_policy||job.failure_tree_invoked||job.external_frame_active||
                   job.deferred_room_route||job.room_context_open||job.source_sparse||job.walking_started||
                   job.frame.state!=RouteFrameState::initial||job.failure_code<=0||job.failure_reason.empty()||
                   choices.chair_attempted||!choices.remaining.empty()||job.path.size()!=1||job.next!=1||
                   !(job.path.front()==RoutePoint{job.expected.x,job.expected.y}))
                    throw FormatError("source selected failed attempt lacks its unstarted pending/failure policy");
            }
            const auto selected_id=selected.chair?std::optional<std::int16_t>{selected.chair->id}:std::nullopt;
            if((job.failure_tree_invoked&&!is_source_failure_tree_continuation(job))||
               job.selected_sitting_object!=selected_id||choices.remaining.size()!=job.fallback_goals.size()||
               (job.external_frame_sitting&&!choices.chair_attempted)||
               (job.external_frame_sitting&&!selected.chair))
                throw FormatError("source SLOT chair/remaining roster differs from route job phase");
            for(std::size_t n=0;n<choices.remaining.size();++n){const auto& a=choices.remaining[n];const auto& b=job.fallback_goals[n];
                if(!(a.position==b.position)||a.entry_flags!=b.entry_flags||a.facing!=b.facing||b.sitting_approach||
                   b.sitting_object!=(a.chair?std::optional<std::int16_t>{a.chair->id}:std::nullopt)||
                   !b.radian_direction||!same_float_bits(a.radian_direction,*b.radian_direction))
                    throw FormatError("source SLOT ordered remaining choices differ from legacy route values");
            }
            if(job.external_frame_sitting){
                const auto chair=live(selected.chair->id,selected.chair->generation);
                if(!chair||!chair->entry_points||chair->entry_points->size()<=26||!job.external_frame_call||
                   job.external_frame_call->key.scope!=routine_key(chair->entry_points->at(26).action).scope||
                   job.external_frame_call->key.id!=routine_key(chair->entry_points->at(26).action).id||
                   job.external_frame_call->context.caller!=entity.id||
                   job.external_frame_call->context.callee!=chair->id||job.external_frame_call->context.stack_object!=chair->id||
                   !job.external_frame_call->exact_arguments||job.external_frame_call->arguments.size()>255||
                   std::any_of(job.external_frame_call->arguments.begin(),job.external_frame_call->arguments.end(),[](auto value){return value!=0;}))
                    throw FormatError("source chair EP26 external call differs from selected chair identity");
            }
        }
        if(job.sparse_consideration){std::set<std::int16_t> identities;
            if(job.sparse_consideration->size()>4096)throw FormatError("route consideration exceeds bounds");
            for(const auto& identity:*job.sparse_consideration){const auto value=live(identity.id,identity.generation);
                if(!identity.generation||!value||value->kind!=EntityKind::avatar||!identities.insert(identity.id).second)
                    throw FormatError("route consideration identity is stale or duplicated");}}
        if(job.external_frame_active){
            external_tickets.insert(job.ticket);
            if(!job.external_frame_call||job.external_frame_owner!=job.external_frame_call->context.code_owner)
                throw FormatError("route external call has no exact owner");
            const auto suspended_pending=std::find_if(vm.suspended_pending.begin(),vm.suspended_pending.end(),
                [&](const auto& pending){return pending.ticket==job.ticket;});
            const bool returned=active&&vm.external_frame_return&&vm.external_frame_return->ticket==job.ticket;
            if(returned){if(vm.status!=VmStatus::yielded||!vm.pending||!route_pending_equal(job.pending,*vm.pending,limits)||
                    vm.external_frame_return->code_owner!=job.external_frame_owner||vm.external_frame_return->run_generation!=vm.run_generation||
                    depth!=vm.stack.size()||suspended_pending!=vm.suspended_pending.end())
                    throw FormatError("returned route external frame linkage is invalid");
            }else{
                if(suspended_pending==vm.suspended_pending.end()||!route_pending_equal(job.pending,*suspended_pending,limits)||depth>=vm.stack.size())
                    throw FormatError("route parent has no matching suspended VM pending call");
                const auto& boundary=vm.stack[depth];auto expected=route_call_frame(*job.external_frame_call);
                expected.return_mode=VmCallReturnMode::external_frame_next_tick;
                expected.locals=boundary.locals;expected.arguments=boundary.arguments;expected.context.stack_object=boundary.context.stack_object;
                expected.context.stack_object_generation=boundary.context.stack_object_generation;
                expected.action_tree=vm.stack[depth-1].action_tree;
                if(job.external_frame_call->action_tree_override&&*job.external_frame_call->action_tree_override!=expected.action_tree)
                    throw FormatError("source route external call changes inherited ActionTree");
                expected.pc=boundary.pc;expected.invocation_ticket=job.ticket;
                if(boundary.return_mode!=VmCallReturnMode::external_frame_next_tick||
                   !route_frame_equal(boundary,expected,limits))throw FormatError("route external invocation boundary is invalid");
            }
        }else if(!immediate_parent&&(!active||vm.status!=VmStatus::waiting||!vm.pending||!route_pending_equal(job.pending,*vm.pending,limits)||
                 depth!=vm.stack.size()||vm.external_frame_return))
            throw FormatError("active route has no matching VM wait");
        if(job.replay_frame){auto replay_pending=job.pending;replay_pending.instruction.instruction=*job.replay_instruction;
            if(!route_frame_equal(*job.replay_frame,job.route_frame,limits)||!route_pending_equal(replay_pending,job.pending,limits))
                throw FormatError("route replay does not match its frozen original instruction");}
        if(job.deferred_room_route&&(!job.room_context_open||!job.replay_frame||!job.replay_instruction))
            throw FormatError("deferred route lacks replay or portal context");
        // Authenticate each frozen request using the same resource, operand,
        // geometry and identity rules as legacy routes. Full live VM linkage
        // was checked above; a suspended parent's pose is historical.
        if(job.unplaced_room_failure){
            const auto& op=job.pending.instruction.instruction;
            if(is_legacy_ui_go_here_request(op,target->ui_ghost))
                throw FormatError("legacy UI-only marker cannot carry source unplaced relative failure");
            const auto policy=source_route_failure_tree_policy(op);
            if(!policy||!is_source_unplaced_room_failure(job)||
               job.call_failure_trees!=(*policy&&i==0)||
               !context_role_matches(job.pending.context,VmEntityRole::caller,job.actor,job.actor_generation)||
               !context_role_matches(job.pending.context,VmEntityRole::stack_object,job.target,job.target_generation))
                throw FormatError("unplaced room failure lost its exact source request or entity authority");
            // The controller authenticates the installed45 or exact transient27 SLOT before restore.
            // This wire validator grants no movement or placement authority.
            continue;
        }
        auto request=entity;request.route_stack.reset();request.route=static_cast<const LocalRouteSnapshot&>(job);
        request.vm.pending=job.pending;request.vm.stack.resize(depth);request.vm.status=VmStatus::waiting;
        request.vm.suspended_pending.clear();request.vm.external_frame_return.reset();
        validate_local_route_links(request,actor,world,limits,active&&!immediate_parent&&!job.external_frame_active,job.deferred_room_route,i!=0,job.external_frame_active,is_source_failure_tree_continuation(job),standing,job.failure_pending&&job.source_choices.has_value());
    }
    std::set<std::uint64_t> expected_suspended;
    for(const auto& job:stack.jobs)if(job.external_frame_active&&
        !(vm.external_frame_return&&vm.external_frame_return->ticket==job.ticket))expected_suspended.insert(job.ticket);
    if(suspended!=expected_suspended)throw FormatError("VM suspended service roster differs from route stack");
    for(const auto& frame:vm.stack)if(frame.return_mode==VmCallReturnMode::external_frame_next_tick&&
        !external_tickets.count(frame.invocation_ticket))throw FormatError("VM external frame has no route job");
    for(const auto& context:stack.portal_contexts){
        const auto found=std::find_if(stack.jobs.begin(),stack.jobs.end(),[&](const auto& job){return job.ticket==context.ticket&&job.room_context_open;});
        if(found==stack.jobs.end()||!context_tickets.insert(context.ticket).second||context.ignored.size()>4096||
           context.pending_edge.has_value()!=context.pending_call.has_value())throw FormatError("route host portal context has invalid ownership");
        std::set<std::pair<std::int16_t,std::uint16_t>> ignored;
        for(const auto& portal:context.ignored)if(!live(portal.portal_id)||!ignored.insert({portal.portal_id,portal.target_room}).second)
            throw FormatError("ignored route portal is stale or duplicated");
        if(context.pending_edge){const auto& edge=*context.pending_edge;const auto portal=live(edge.portal_id,context.portal_generation);
            if(!context.portal_generation||!portal||!portal->position||!(*portal->position==edge.position)||
               edge.source_room==edge.target_room||(!found->failure_tree_invoked&&(!found->external_frame_call||
               route_vm_values(route_call_frame(*context.pending_call),std::nullopt,limits)!=
                   route_vm_values(route_call_frame(*found->external_frame_call),std::nullopt,limits)))||
               !context_role_matches(context.pending_call->context,VmEntityRole::caller,entity.id,actor.generation)||
               !context_role_matches(context.pending_call->context,VmEntityRole::callee,edge.portal_id,context.portal_generation)||
               !context_role_matches(context.pending_call->context,VmEntityRole::stack_object,edge.portal_id,context.portal_generation))
                throw FormatError("route host pending portal identity or call is stale");
        }else if(context.portal_generation)throw FormatError("idle route host context has a portal generation");
    }
    for(const auto& job:stack.jobs)if(job.room_context_open!=bool(context_tickets.count(job.ticket)))
        throw FormatError("route job and host portal context roster differ");
}
bool valid_playback_key(const PlaybackResourceKey& key) {
    return (key.kind==PlaybackResourceKey::Kind::reach&&key.source==0&&key.id<=3&&key.posture==0)||
           (key.kind==PlaybackResourceKey::Kind::animation&&((key.source||key.id)&&key.posture==0))||
           (key.kind==PlaybackResourceKey::Kind::posture_reset&&!key.source&&!key.id&&
            (key.posture==1||key.posture==2||key.posture==3))||
           (key.kind==PlaybackResourceKey::Kind::route_walk&&key.source<=1&&
            key.id>=3&&key.id<=9&&key.posture==0)||
           (key.kind==PlaybackResourceKey::Kind::source_locomotion&&key.source<=1&&key.posture==0&&
            (key.id==3||key.id==20||key.id==21||key.id==25));
}

bool nonzero_digest(const std::array<std::uint8_t,32>& digest) {
    return std::any_of(digest.begin(),digest.end(),[](auto value){return value!=0;});
}
bool valid_carry_key(const PlaybackResourceKey& key) {
    return (key.kind==PlaybackResourceKey::Kind::animation&&((key.source||key.id)&&key.posture==0))||
           (key.kind==PlaybackResourceKey::Kind::posture_reset&&!key.source&&!key.id&&key.posture==-1);
}
const LocalRouteSnapshot* active_session_route(const SessionRuntimeEntity& entity) {
    if(entity.route)return &*entity.route;
    if(entity.route_stack&&!entity.route_stack->jobs.empty())return &entity.route_stack->jobs.back();
    return nullptr;
}
void validate_playback_links(const SessionRuntimeEntity& entity,const Entity& actor,
                             const SessionHostState& host,const SessionBundleLimits& limits,bool locomotion_lane=false) {
    const auto& p=entity.playback;const auto& vm=entity.vm;
    if(p.last_tick&&*p.last_tick>host.tick)throw FormatError("playback tick is ahead of session tick");
    const auto key_actor_matches=[&](const PlaybackResourceKey& key){
        return key.caller==entity.id&&
               key.actor_age==(actor.person_data.size()>58?actor.person_data[58]:0);
    };
    if(p.source_walk){
        if(!p.state||locomotion_lane)throw FormatError("locomotion collection has no primary clip");
        const auto& walking=*p.source_walk;
        if(!source_walk_weights_valid(walking.weights))
            throw FormatError("locomotion collection weights differ from the source velocity profile");
        const auto& full=walking.secondary[0].resource;const auto& half=walking.secondary[1].resource;
        const auto same_context=[](const PlaybackResourceKey& a,const PlaybackResourceKey& b){
            return a.source==b.source&&a.caller==b.caller&&a.callee==b.callee&&a.stack_object==b.stack_object&&
                a.actor_age==b.actor_age&&a.code_owner==b.code_owner;
        };
        if(full.kind!=PlaybackResourceKey::Kind::source_locomotion||
           half.kind!=PlaybackResourceKey::Kind::source_locomotion||!same_context(full,half)||
           (full.id!=20&&full.id!=21)||(half.id!=20&&half.id!=21&&half.id!=25)||
           (p.state->resource.kind!=PlaybackResourceKey::Kind::source_locomotion&&
            p.state->resource.kind!=PlaybackResourceKey::Kind::animation)||
           (p.state->resource.kind==PlaybackResourceKey::Kind::source_locomotion&&
            (p.state->resource.id!=3||!same_context(p.state->resource,full)||p.state->speed!=1||p.state->backwards||!p.state->loop||p.state->wait)))
            throw FormatError("locomotion primary/secondary resource relationship is invalid");
        for(const auto& lane:walking.secondary){
            if(lane.wait||lane.backwards||!lane.loop||lane.speed!=1.5f||!lane.queue.empty()||
               lane.events_run||lane.ticks!=p.state->ticks)
                throw FormatError("locomotion secondary playback state is invalid");
            auto secondary=entity;secondary.playback.state=lane;secondary.playback.source_walk.reset();
            validate_playback_links(secondary,actor,host,limits,true);
        }
    }
    if(p.state){const auto& s=*p.state;
        if(!p.source_walk&&!locomotion_lane&&s.resource.kind==PlaybackResourceKey::Kind::source_locomotion)
            throw FormatError("locomotion primary resource has no collection");
        const auto terminal_frame=std::trunc(static_cast<double>(s.frame));
        const bool invalid_frame_state=(s.end&&s.loop)||
            (!s.loop&&(s.end?(s.backwards?terminal_frame>=0:terminal_frame<s.frames):
                              (s.backwards?(terminal_frame<0||s.frame>static_cast<float>(s.frames)):
                                            (s.frame<0||terminal_frame>=s.frames))));
        const bool invalid_route_clip=s.resource.kind==PlaybackResourceKey::Kind::route_walk&&
            (s.wait||s.backwards||s.loop!=(s.resource.id==3)||
             (s.speed!=1.0f&&s.speed!=2.0f)||(s.resource.id==3&&s.speed!=1.0f));
        const bool invalid_reach_clip=s.resource.kind==PlaybackResourceKey::Kind::reach&&
            (s.wait||s.backwards||s.loop||s.speed!=1.0f);
        if(!valid_playback_key(s.resource)||invalid_route_clip||invalid_reach_clip||!key_actor_matches(s.resource)||
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
        }else if(!locomotion_lane&&vm.pending&&vm.pending->context.caller==entity.id&&
                 vm.pending->instruction.instruction.opcode==44)
            throw FormatError("session pending animation VM call has no playback wait");
    }else if(!locomotion_lane&&vm.pending&&vm.pending->context.caller==entity.id&&
             vm.pending->instruction.instruction.opcode==44)
        throw FormatError("session pending animation VM call has no playback state");
    const auto active_route=active_session_route(entity);
    if(active_route&&active_route->look_towards&&
       active_route->look_towards->animation_index>=4&&
       (!p.state||p.state->wait||!vm.pending||vm.pending->instruction.instruction.opcode!=22))
        throw FormatError("started LookTowards turn has no current shared primary animation state");
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
            (owner.bank_kind==SessionCodeBankKind::complete_private ||
             owner.bank_kind==SessionCodeBankKind::private_and_semiglobal);});
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

bool linked_semiglobal_entry_matches(const SessionLinkedSemiglobal& link) {
    // Physical archive spelling is preserved; only the extension is insensitive.
    const auto n=link.name.size();
    return link.entry.size()==n+4&&link.entry.compare(0,n,link.name)==0&&
        link.entry[n]=='.'&&(link.entry[n+1]=='i'||link.entry[n+1]=='I')&&
        (link.entry[n+2]=='f'||link.entry[n+2]=='F')&&
        (link.entry[n+3]=='f'||link.entry[n+3]=='F');
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
    for(const auto& continuation:entity.vm.immediate_continuations)
        if(continuation.parent_pending)note(continuation.parent_pending->context.code_owner);
    if(entity.vm.immediate_return)note(entity.vm.immediate_return->code_owner);
    for(const auto& action:entity.controllers.interactions.actions)note(action.call.context.code_owner);
    if(entity.playback.state)note(entity.playback.state->resource.code_owner);
    if(entity.playback.carry)note(entity.playback.carry->code_owner);
    if(entity.playback.source_walk)for(const auto& lane:entity.playback.source_walk->secondary)note(lane.resource.code_owner);
    if(entity.route_stack)for(const auto& job:entity.route_stack->jobs){
        note(job.pending.context.code_owner);note(job.route_frame.context.code_owner);
        if(job.replay_frame)note(job.replay_frame->context.code_owner);
        if(job.external_frame_call)note(job.external_frame_call->context.code_owner);
    }
    for(const auto& owner:entity.resource_owners)referenced.erase(owner.owner);
    // SEAT has a separate strict owner roster for the completed Sit carry.
    // Only bindings already verified by validate_seated_session_continuation
    // are handled there; do not misclassify them as Go Here DYNO owners.
    if(verified_seated&&entity.id==verified_seated->actor_id)
        for(const auto& owner:verified_seated->code_owners)referenced.erase(owner.owner);
    if(!referenced.empty()&&!entity.route&&(!entity.route_stack||entity.route_stack->jobs.empty()))
        throw FormatError("dynamic action owners are supported only for a saved local route");
    std::set<CodeOwner> declared;CodeOwner previous=0;
    for(const auto& owner:entity.dynamic_action_owners){
        const auto target=std::find_if(world.begin(),world.end(),[&](const auto& value){return value.id==owner.target_id&&value.generation==owner.target_generation;});
        const auto scope=routine_key(owner.root_routine).scope;
        const bool link_bad=owner.linked_semiglobal&&
            (owner.linked_semiglobal->name.empty()||owner.linked_semiglobal->archive.empty()||
             owner.linked_semiglobal->entry.empty()||owner.linked_semiglobal->name.find_first_of("/\\.:")!=std::string::npos||
             owner.linked_semiglobal->archive.front()=='/'||owner.linked_semiglobal->archive.find('\\')!=std::string::npos||
             owner.linked_semiglobal->archive.find(':')!=std::string::npos||
             owner.linked_semiglobal->archive.find("..")!=std::string::npos||
             !linked_semiglobal_entry_matches(*owner.linked_semiglobal));
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
        linked_semiglobal_entry_matches(*link));
}
std::set<CodeOwner> referenced_owners(const VmSnapshot& vm) {
    std::set<CodeOwner> result;
    const auto note=[&](CodeOwner owner){if(owner)result.insert(owner);};
    for(const auto& frame:vm.stack)note(frame.context.code_owner);
    if(vm.pending)note(vm.pending->context.code_owner);
    for(const auto& wait:vm.suspended_pending)note(wait.context.code_owner);
    for(const auto& continuation:vm.immediate_continuations)
        if(continuation.parent_pending)note(continuation.parent_pending->context.code_owner);
    if(vm.interaction_return)note(vm.interaction_return->code_owner);
    if(vm.external_frame_return)note(vm.external_frame_return->code_owner);
    if(vm.immediate_return)note(vm.immediate_return->code_owner);
    return result;
}
void validate_resource_owners(const std::vector<SessionResourceCodeOwner>& owners,
    const std::set<CodeOwner>& referenced,const std::set<CodeOwner>& implicit,
    const SessionBundleLimits& limits) {
    if(owners.size()>limits.max_resource_owners_per_thread||owners.size()>UINT16_MAX)
        throw FormatError("session resource owner count exceeds bounds");
    std::set<CodeOwner> declared;CodeOwner previous=0;
    for(const auto& owner:owners){
        const auto scope=session_resource_root_key(owner).scope;
        const bool named_private=owner.bank_kind==SessionCodeBankKind::complete_named_private;
        const bool named_semiglobal=owner.bank_kind==SessionCodeBankKind::complete_named_semiglobal;
        const bool named=named_private||named_semiglobal;
        const bool empty_bank=owner.root_routine==0 &&
            (owner.bank_kind==SessionCodeBankKind::complete_private ||
             owner.bank_kind==SessionCodeBankKind::private_and_semiglobal) &&
            owner.routine_fingerprint==session_loaded_bank_fingerprint(RoutineSet{});
        const bool private_root=scope==RoutineScope::private_object||named_private;
        const bool semiglobal_root=scope==RoutineScope::semiglobal||named_semiglobal;
        const bool kind_valid=owner.bank_kind==SessionCodeBankKind::private_closure||
            owner.bank_kind==SessionCodeBankKind::complete_private||
            owner.bank_kind==SessionCodeBankKind::private_and_semiglobal||named;
        const bool identity_valid=owner.identity==SessionCodeOwnerIdentity::process?
            owner.owner>=0x80000000U:(owner.identity==SessionCodeOwnerIdentity::resource_guid&&owner.owner==owner.resource_guid);
        if(!identity_valid||!owner.owner||owner.owner<=previous||implicit.count(owner.owner)||!owner.resource_guid||
           !owner.definition_chunk_id||!valid_resource_text(owner.source)||
           !valid_resource_text(owner.asset,true)||!valid_resource_link(owner.linked_semiglobal)||
           !nonzero_digest(owner.routine_fingerprint)||!kind_valid||(!private_root&&!semiglobal_root&&!empty_bank)||
           (named?(named_semiglobal&&!owner.linked_semiglobal):
                (owner.bank_kind==SessionCodeBankKind::private_and_semiglobal?!owner.linked_semiglobal:
                    (owner.linked_semiglobal.has_value()||(!private_root&&!empty_bank)))))
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
// Shared execution validation includes visitors while authority stays in HHRD.
std::vector<const SessionRuntimeEntity*> avatar_runtime_states(const SessionBundle& bundle) {
    std::vector<const SessionRuntimeEntity*> result;
    result.reserve(bundle.runtime_entities.size()+bundle.npc_avatars.size());
    for(const auto& state:bundle.runtime_entities)result.push_back(&state);
    for(const auto& npc:bundle.npc_avatars)result.push_back(&npc.runtime);
    std::sort(result.begin(),result.end(),[](const auto* a,const auto* b){return a->id<b->id;});
    return result;
}
bool canonical_npc_path(const std::string& value,bool allow_empty=false) {
    if(value.empty())return allow_empty;
    if(!valid_resource_text(value)||value.front()=='/'||value.back()=='/'||
       value.find('\\')!=std::string::npos||value.find(':')!=std::string::npos)return false;
    std::size_t start=0;
    while(start<value.size()) {
        const auto end=value.find('/',start);const auto part=value.substr(start,end==std::string::npos?end:end-start);
        if(part.empty()||part=="."||part=="..")return false;
        if(end==std::string::npos)break;
        start=end+1;
    }
    return true;
}
void validate_npc_avatars(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    if(bundle.npc_avatars.empty())return;
    if(!bundle.generic_execution||(bundle.household_roster.empty()&&!bundle.npc_primary)||
       bundle.npc_avatars.size()>std::min<std::size_t>(64,limits.max_npc_avatars))
        throw FormatError("NPC avatars require a bounded generic household profile");
    std::set<std::int16_t> neighbors;std::set<std::uint32_t> guids;
    std::set<std::pair<std::string,std::string>> resources;
    for(const auto& member:bundle.household_roster){neighbors.insert(member.neighbor_id);guids.insert(member.guid);}
    std::int16_t previous=0;
    for(const auto& npc:bundle.npc_avatars) {
        const auto& origin=npc.origin;const auto& state=npc.runtime;
        const auto entity=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& value){return value.id==origin.id;});
        const bool bad_origin=origin.id<=previous||origin.id!=state.id||!origin.generation||
            origin.generation>bundle.world.generation_sequence||!origin.guid||origin.neighbor_id<0;
        const bool bad_resource=!origin.object_definition_id||!canonical_npc_path(origin.source)||
            !valid_resource_text(origin.asset,true)||!nonzero_digest(origin.character_fingerprint);
        const bool duplicate_neighbor=origin.neighbor_id>0&&!neighbors.insert(origin.neighbor_id).second;
        const bool duplicate_guid=!guids.insert(origin.guid).second;
        const bool duplicate_resource=!bad_resource&&!resources.emplace(origin.source,origin.asset).second;
        const bool absent=entity==bundle.world.entities.end();
        const bool bad_kind=absent||entity->kind!=EntityKind::avatar||entity->ui_ghost;
        const bool bad_generation=absent||entity->generation!=origin.generation;
        const bool bad_guid=absent||!entity->definition||entity->definition->guid!=origin.guid;
        const bool original_person=bundle.neighbor_inventory&&bundle.neighbor_inventory->version==2;
        const bool bad_person=absent||(entity->person_data.size()!=88&&entity->person_data.size()!=101&&
            !(original_person&&entity->person_data.size()==256));
        const bool bad_neighbor=bad_person||entity->person_data[31]!=origin.neighbor_id;
        const bool bad_motives=absent||entity->motives.size()!=16;
        const bool runtime_collision=std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
            [&](const auto& value){return value.id==origin.id;});
        const bool thread_collision=std::any_of(bundle.object_threads.begin(),bundle.object_threads.end(),
            [&](const auto& value){return value.id==origin.id;});
        const bool created_collision=std::any_of(bundle.created_objects.begin(),bundle.created_objects.end(),
            [&](const auto& value){return value.id==origin.id;});
        // Source IDs may be recycled only after the original generation was
        // removed. Restore authenticates that removed identity against the lot
        // baseline before constructing this independent, newer NPC generation.
        const bool reusable=bundle.object_history&&bundle.object_history->reusable_entity_ids;
        const bool removed_collision=std::any_of(bundle.removed_original_objects.begin(),bundle.removed_original_objects.end(),
            [&](const auto& value){return value.id==origin.id&&(!reusable||value.generation>=origin.generation);});
        if(bad_origin||bad_resource||duplicate_neighbor||duplicate_guid||duplicate_resource||bad_kind||bad_generation||
           bad_guid||bad_person||bad_neighbor||bad_motives||runtime_collision||thread_collision||created_collision||removed_collision)
            throw FormatError("NPC avatar origin differs from World, resource or independent visitor identity (id="+
                std::to_string(origin.id)+", NeighborID="+std::to_string(origin.neighbor_id)+
                ", OBJD="+std::to_string(origin.object_definition_id)+", source="+origin.source.substr(0,128)+
                ", asset="+origin.asset.substr(0,128)+", badOrigin="+std::to_string(bad_origin)+
                ", badResource="+std::to_string(bad_resource)+", dupeNeighbor="+std::to_string(duplicate_neighbor)+
                ", dupeGuid="+std::to_string(duplicate_guid)+", dupeResource="+std::to_string(duplicate_resource)+
                ", worldKind="+std::to_string(bad_kind)+", generation="+std::to_string(bad_generation)+
                ", guid="+std::to_string(bad_guid)+", personShape="+std::to_string(bad_person)+
                ", NID="+std::to_string(bad_neighbor)+", motives="+std::to_string(bad_motives)+
                ", runtime="+std::to_string(runtime_collision)+", objectThread="+std::to_string(thread_collision)+
                ", created="+std::to_string(created_collision)+", removed="+std::to_string(removed_collision)+")");
        if(npc.selected_name&&(!valid_resource_text(*npc.selected_name)||npc.selected_name->size()>4096))
            throw FormatError("NPC avatar body name choice is invalid");
        Ts1MotiveDecay decay;if(!decay.restore(npc.decay))throw FormatError("NPC motive decay state is invalid");
        const auto primary=std::find_if(state.resource_owners.begin(),state.resource_owners.end(),
            [&](const auto& owner){return owner.owner==npc.primary_owner;});
        if(!npc.primary_owner||primary==state.resource_owners.end()||primary->resource_guid!=origin.guid||
           primary->source!=origin.source||primary->asset!=origin.asset||primary->definition_chunk_id!=origin.object_definition_id||
           (primary->bank_kind!=SessionCodeBankKind::complete_private&&primary->bank_kind!=SessionCodeBankKind::private_and_semiglobal))
            throw FormatError("NPC primary owner is not its exact complete private resource bank");
        previous=origin.id;
    }
}
void validate_generic_execution(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    const auto avatars=avatar_runtime_states(bundle);
    const bool has_resource_owners=!bundle.npc_avatars.empty()||std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [](const auto& state){return !state.resource_owners.empty();})||
        std::any_of(bundle.object_threads.begin(),bundle.object_threads.end(),
            [](const auto& state){return !state.resource_owners.empty()||state.primary_owner!=0;});
    if(!bundle.generic_execution){
        if(has_resource_owners||!bundle.created_objects.empty()||!bundle.removed_original_objects.empty()||
           !bundle.deferred_removals.empty()||bundle.next_code_owner!=0x80000000U||bundle.code_owner_ids_exhausted)
            throw FormatError("generic execution values require GEXP profile");
        return;
    }
    if(avatars.size()+bundle.object_threads.size()>UINT16_MAX)
        throw FormatError("generic execution thread count exceeds format");
    if(bundle.next_code_owner<0x80000000U||
       (bundle.code_owner_ids_exhausted&&bundle.next_code_owner!=UINT32_MAX))
        throw FormatError("generic code owner allocator state is invalid");
    const auto allocated=[&](CodeOwner owner){
        if(owner>=0x80000000U&&!bundle.code_owner_ids_exhausted&&owner>=bundle.next_code_owner)
            throw FormatError("persisted process owner is ahead of allocator cursor");
    };
    for(const auto* state_ptr:avatars){const auto& state=*state_ptr;
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
    for(const auto* state:avatars)resident_owners.insert(static_cast<CodeOwner>(state->id));
    std::map<CodeOwner,const SessionDynamicActionOwner*> legacy_actions;
    std::map<CodeOwner,const SessionActionCodeOwner*> legacy_objects;
    for(const auto* state:avatars)for(const auto& owner:state->dynamic_action_owners){
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
    for(const auto* state:avatars)register_owners(state->resource_owners);
    for(const auto& state:bundle.object_threads)register_owners(state.resource_owners);
    for(const auto* state_ptr:avatars){const auto& state=*state_ptr;
        auto referenced=referenced_owners(state.vm);std::set<CodeOwner> implicit;
        const bool visitor=std::any_of(bundle.npc_avatars.begin(),bundle.npc_avatars.end(),
            [&](const auto& npc){return npc.runtime.id==state.id;});
        if(!visitor)implicit.insert(static_cast<CodeOwner>(state.id));
        for(const auto& action:state.controllers.interactions.actions)
            if(action.call.context.code_owner)referenced.insert(action.call.context.code_owner);
        if(state.playback.state&&state.playback.state->resource.code_owner)
            referenced.insert(state.playback.state->resource.code_owner);
        if(state.playback.carry&&state.playback.carry->code_owner)referenced.insert(state.playback.carry->code_owner);
        if(state.playback.source_walk)for(const auto& lane:state.playback.source_walk->secondary)
            if(lane.resource.code_owner)referenced.insert(lane.resource.code_owner);
        if(state.route_stack)for(const auto& job:state.route_stack->jobs){
            for(const auto owner:{job.pending.context.code_owner,job.route_frame.context.code_owner})
                if(owner)referenced.insert(owner);
            if(job.replay_frame&&job.replay_frame->context.code_owner)referenced.insert(job.replay_frame->context.code_owner);
            if(job.external_frame_call&&job.external_frame_call->context.code_owner)referenced.insert(job.external_frame_call->context.code_owner);
        }
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
               (primary->bank_kind!=SessionCodeBankKind::complete_private &&
                primary->bank_kind!=SessionCodeBankKind::private_and_semiglobal))
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
           entity->generation!=origin.generation||
           (origin.master_guid&&(!*origin.master_guid||*origin.master_guid==origin.guid||entity->definition->master_guid!=origin.master_guid)))
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

void validate_object_history(const SessionBundle& bundle) {
    if(!bundle.object_history){
        if(!bundle.world.retained_entities.empty())throw FormatError("retained bodies require object history");
        return;
    }
    if(!bundle.generic_execution)throw FormatError("object history requires GEXP profile");
    const auto& history=*bundle.object_history;
    if(history.retired_created_objects.size()>4096||history.retired_npc_avatars.size()>4096||history.retired_household_avatars.size()>4096||history.reserved_entity_ids.size()>4096)
        throw FormatError("object history exceeds identity limits");
    if(!history.reusable_entity_ids&&(!bundle.world.retained_entities.empty()||!history.retired_npc_avatars.empty()||!history.retired_household_avatars.empty()))
        throw FormatError("legacy object history cannot describe retained bodies or visitors");
    using Identity=std::pair<std::int16_t,std::uint64_t>;
    std::set<std::int16_t> live_ids,legacy_origin_ids;std::set<Identity> live,origins;
    std::set<std::uint64_t> generations;
    for(const auto& entity:bundle.world.entities){live_ids.insert(entity.id);live.emplace(entity.id,entity.generation);generations.insert(entity.generation);}
    std::int16_t previous_id=0;
    for(const auto id:history.reserved_entity_ids){
        if(id<=previous_id||live_ids.count(id))throw FormatError("object history reservation is invalid, duplicated or live");
        previous_id=id;
    }
    const auto reserved=[&](std::int16_t id){return std::binary_search(history.reserved_entity_ids.begin(),history.reserved_entity_ids.end(),id);};
    for(const auto& origin:bundle.created_objects){origins.emplace(origin.id,origin.generation);legacy_origin_ids.insert(origin.id);}
    for(const auto& origin:bundle.removed_original_objects){
        legacy_origin_ids.insert(origin.id);
        if((!history.reusable_entity_ids&&!reserved(origin.id))||!generations.insert(origin.generation).second||
           !origins.emplace(origin.id,origin.generation).second)
            throw FormatError("removed original history lacks legacy reservation or has a reused identity");
    }
    Identity previous{};
    for(const auto& origin:history.retired_created_objects){
        const Identity identity{origin.id,origin.generation};
        if(origin.id<=0||identity<=previous||(!history.reusable_entity_ids&&origin.id<=previous.first)||
           !origin.generation||origin.generation>bundle.world.generation_sequence||!origin.guid||!origin.definition_chunk_id||
           !valid_resource_text(origin.source)||!valid_resource_text(origin.asset,true)||live.count(identity)||
           (origin.master_guid&&(!*origin.master_guid||*origin.master_guid==origin.guid))||
           (!history.reusable_entity_ids&&(live_ids.count(origin.id)||!reserved(origin.id)||!legacy_origin_ids.insert(origin.id).second))||
           !origins.insert(identity).second||!generations.insert(origin.generation).second)
            throw FormatError("retired creation history identity is invalid, duplicated, live or unreserved");
        previous=identity;
    }
    previous={};
    for(const auto& origin:history.retired_npc_avatars){
        const Identity identity{origin.id,origin.generation};
        if(origin.id<=0||identity<=previous||!origin.generation||origin.generation>bundle.world.generation_sequence||
           !origin.guid||origin.neighbor_id<0||!origin.object_definition_id||!canonical_npc_path(origin.source)||
           !valid_resource_text(origin.asset,true)||!nonzero_digest(origin.character_fingerprint)||live.count(identity)||
           !origins.insert(identity).second||!generations.insert(origin.generation).second)
            throw FormatError("retired NPC provenance is invalid or aliases another identity");
        previous=identity;
    }
    previous={};
    for(const auto& record:history.retired_household_avatars){const auto& origin=record.member;const auto& lease=record.source_lease;
        const Identity identity{origin.id,origin.generation};
        if(origin.id<=0||identity<=previous||!origin.generation||origin.generation>bundle.world.generation_sequence||
           !origin.guid||origin.neighbor_id<=0||!origin.object_definition_id||!canonical_npc_path(origin.character_asset)||
           !nonzero_digest(origin.character_fingerprint)||!nonzero_digest(record.source_checkpoint_sha256)||
           !nonzero_digest(lease.neighborhood_identity)||!nonzero_digest(lease.family_identity)||
           !lease.occupancy_epoch||live.count(identity)||
           !origins.insert(identity).second||!generations.insert(origin.generation).second)
            throw FormatError("retired household provenance is invalid or aliases another identity");
        previous=identity;
    }
    for(const auto& body:bundle.world.retained_entities){
        if(!origins.count({body.id,body.generation})||!body.definition)
            throw FormatError("retained body has no exact origin history");
        const auto created=std::find_if(history.retired_created_objects.begin(),history.retired_created_objects.end(),
            [&](const auto& o){return o.id==body.id&&o.generation==body.generation;});
        const auto npc=std::find_if(history.retired_npc_avatars.begin(),history.retired_npc_avatars.end(),
            [&](const auto& o){return o.id==body.id&&o.generation==body.generation;});
        const auto original=std::find_if(bundle.removed_original_objects.begin(),bundle.removed_original_objects.end(),
            [&](const auto& o){return o.id==body.id&&o.generation==body.generation;});
        const auto household=std::find_if(history.retired_household_avatars.begin(),history.retired_household_avatars.end(),
            [&](const auto& o){return o.member.id==body.id&&o.member.generation==body.generation;});
        const auto guid=created!=history.retired_created_objects.end()?created->guid:
            npc!=history.retired_npc_avatars.end()?npc->guid:household!=history.retired_household_avatars.end()?household->member.guid:
            original!=bundle.removed_original_objects.end()?original->guid:0;
        if(body.definition->guid!=guid||
           (created!=history.retired_created_objects.end()&&body.definition->master_guid!=created->master_guid)||
           ((npc!=history.retired_npc_avatars.end()||household!=history.retired_household_avatars.end())?body.kind!=EntityKind::avatar:body.kind!=EntityKind::object))
            throw FormatError("retained body differs from its exact resource origin");
    }
}


const Entity* reference_entity(const WorldSnapshot& world,WorldEntityReference ref) {
    if(ref.id<=0||!ref.generation)return nullptr;
    const auto match=[&](const Entity& e){return e.id==ref.id&&e.generation==ref.generation;};
    const auto live=std::find_if(world.entities.begin(),world.entities.end(),match);
    if(live!=world.entities.end())return &*live;
    const auto dead=std::find_if(world.retained_entities.begin(),world.retained_entities.end(),match);
    return dead==world.retained_entities.end()?nullptr:&*dead;
}
template<class Visit> void session_contexts(const SessionBundle& bundle,const Visit& visit) {
    const auto vm=[&](const VmSnapshot& state){
        for(const auto& f:state.stack)visit(f.context);
        if(state.pending)visit(state.pending->context);
        for(const auto& p:state.suspended_pending)visit(p.context);
        for(const auto& c:state.immediate_continuations)if(c.parent_pending)visit(c.parent_pending->context);
    };
    const auto runtime=[&](const SessionRuntimeEntity& state){
        vm(state.vm);
        for(const auto& a:state.controllers.interactions.actions)visit(a.call.context);
        if(state.route_stack){
            for(const auto& job:state.route_stack->jobs){
                visit(job.pending.context);visit(job.route_frame.context);
                if(job.replay_frame)visit(job.replay_frame->context);
                if(job.external_frame_call)visit(job.external_frame_call->context);
            }
            for(const auto& portal:state.route_stack->portal_contexts)if(portal.pending_call)visit(portal.pending_call->context);
        }
    };
    for(const auto& state:bundle.runtime_entities)runtime(state);
    for(const auto& npc:bundle.npc_avatars)runtime(npc.runtime);
    for(const auto& state:bundle.object_threads)vm(state.vm);
}
void validate_reference_closure(const SessionBundle& bundle) {
    std::set<WorldEntityReference> roots;
    session_contexts(bundle,[&](const VmContext& c){
        if(!c.entity_references_bound){
            if(c.caller_generation||c.callee_generation||c.stack_object_generation)
                throw FormatError("legacy context contains partially bound references");
            return;
        }
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
            const auto ref=vm_context_reference(c,role);
            if(!ref.generation)continue; // Explicit nil preserves its raw ID without lookup.
            if(!reference_entity(bundle.world,ref))throw FormatError("session captured entity reference is absent or reused: role="+std::to_string(static_cast<unsigned>(role))+" id="+std::to_string(ref.id)+" generation="+std::to_string(ref.generation));
            roots.insert(ref);
        }
    });
    // A retained body can also be owned by an actual pending route identity.
    const auto local_roots=[&](const LocalRouteSnapshot& j){
        roots.insert({j.actor,j.actor_generation});roots.insert({j.target,j.target_generation});
        for(const auto& link:j.carried_target){roots.insert({link.child.id,link.child.generation});roots.insert({link.parent.id,link.parent.generation});}
    };
    const auto route_roots=[&](const SessionRuntimeEntity& r){
        if(r.route)local_roots(*r.route);
        if(r.route_stack)for(const auto& j:r.route_stack->jobs)local_roots(j);
    };
    for(const auto& r:bundle.runtime_entities)route_roots(r);
    for(const auto& npc:bundle.npc_avatars)route_roots(npc.runtime);
    for(const auto& body:bundle.world.retained_entities)if(!roots.count({body.id,body.generation}))
        throw FormatError("retained body is not owned by an actual execution reference");
}

void validate_dialog_links(const SessionBundle& bundle) {
    const auto* dialog=bundle.dialog?&*bundle.dialog:nullptr;
    if(dialog&&!dialog_state_valid(*dialog))throw FormatError("invalid session dialog state");
    std::size_t matches=0;
    const auto thread=[&](std::int16_t id,const VmSnapshot& vm){
        const bool dialog_wait=vm.pending&&
            (vm.pending->instruction.instruction.opcode==36||
             vm.pending->instruction.instruction.opcode==38||
             vm.pending->instruction.instruction.opcode==39);
        if(!dialog_wait&&(!dialog||dialog->thread_owner.id!=id))return;
        if(!dialog||dialog->thread_owner.id!=id||!dialog_matches_vm_snapshot(*dialog,vm))
            throw FormatError("session dialog and actual VM wait do not match");
        const auto owner=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& e){
            return e.id==id&&e.generation==dialog->thread_owner.generation&&!e.dead&&!e.ui_ghost;
        });
        if(owner==bundle.world.entities.end())throw FormatError("session dialog thread owner is missing or replaced");
        ++matches;
    };
    for(const auto& state:bundle.runtime_entities)thread(state.id,state.vm);
    for(const auto& npc:bundle.npc_avatars)thread(npc.runtime.id,npc.runtime.vm);
    for(const auto& object:bundle.object_threads)thread(object.id,object.vm);
    if(dialog&&matches!=1)throw FormatError("session dialog has no unique waiting thread");
}
void validate_object_group_links(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    std::set<std::int16_t> grouped_created;
    if(bundle.object_groups) {
        validate_object_groups(*bundle.object_groups,bundle.world,limits.object_groups);
        for(const auto& group:bundle.object_groups->groups) {
            const bool association=group.kind==ObjectGroupKind::live_association;
            if(association&&(!bundle.object_trade||bundle.object_trade->version!=2))
                throw FormatError("live object association requires its complete v2 value authority");
            for(const auto& member:group.members) {
                const auto entity=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
                    [&](const auto& e){return e.id==member.id&&e.generation==member.generation;});
                const auto origin=std::find_if(bundle.created_objects.begin(),bundle.created_objects.end(),
                    [&](const auto& o){return o.id==member.id&&o.generation==member.generation;});
                if(!association) {
                    if(origin==bundle.created_objects.end()||origin->generation!=member.generation||
                       origin->guid!=entity->definition->guid||(origin->master_guid&&origin->master_guid!=entity->definition->master_guid))
                        throw FormatError("dynamic group member lacks its exact created part resource provenance");
                    grouped_created.insert(member.id);
                    continue;
                }
                const auto& saved=*member.origin;
                const SessionObjectValueMember* priced=nullptr;
                for(const auto& value:bundle.object_trade->groups)for(const auto& part:value.members)
                    if(part.object.id==member.id&&part.object.generation==member.generation){
                        if(priced)throw FormatError("live association member has ambiguous value provenance");priced=&part;}
                if(!priced||priced->object.guid!=saved.guid||priced->object.master_guid!=saved.master_guid||
                   priced->object.source!=saved.source||priced->object.asset!=saved.asset||
                   priced->object.definition_chunk_id!=saved.definition_chunk_id)
                    throw FormatError("live association member differs from its individual value provenance");
                if(saved.source_kind==ObjectGroupMemberSource::native_created) {
                    if(origin==bundle.created_objects.end()||origin->guid!=saved.guid||
                       origin->source!=saved.source||origin->asset!=saved.asset||origin->definition_chunk_id!=saved.definition_chunk_id||
                       (origin->master_guid&&origin->master_guid!=saved.master_guid))
                        throw FormatError("live native association member lacks exact created provenance");
                    grouped_created.insert(member.id);
                }else {
                    // The serialized original-import tag is NOT proof of original
                    // membership. The loader must authenticate the original source
                    // and resource before adopting this explicit live override.
                    if(origin!=bundle.created_objects.end()||
                       std::any_of(bundle.removed_original_objects.begin(),bundle.removed_original_objects.end(),[&](const auto& old){
                           return old.id==member.id&&old.generation==member.generation;}))
                        throw FormatError("live original association contradicts creation/removal authority");
                    if(bundle.object_history&&std::any_of(bundle.object_history->retired_created_objects.begin(),
                       bundle.object_history->retired_created_objects.end(),[&](const auto& old){return old.id==member.id&&old.generation==member.generation;}))
                        throw FormatError("live original association claims a retired created identity");
                    for(const auto& thread:bundle.object_threads)if(thread.id==member.id&&thread.generation==member.generation){
                        if(thread.guid!=saved.guid)throw FormatError("live original association differs from its process resource");
                        for(const auto& bank:thread.resource_owners)if(bank.owner==thread.primary_owner&&
                            (bank.resource_guid!=saved.guid||bank.source!=saved.source||bank.asset!=saved.asset||bank.definition_chunk_id!=saved.definition_chunk_id))
                            throw FormatError("live original association differs from its primary program provenance");
                    }
                }
            }
        }
        // Older sessions may have physical groups without known prices. When
        // a v2 value group represents any of those members, both authoritative
        // records must describe the same whole ordered physical cohort.
        if(bundle.object_trade&&bundle.object_trade->version==2)
            for(const auto& physical:bundle.object_groups->groups)for(const auto& value:bundle.object_trade->groups){
                const bool overlaps=std::any_of(value.members.begin(),value.members.end(),[&](const auto& priced){
                    return std::any_of(physical.members.begin(),physical.members.end(),[&](const auto& body){
                        return body.id==priced.object.id&&body.generation==priced.object.generation;});});
                if(!overlaps)continue;
                if(value.multitile!=physical.multitile||value.members.size()!=physical.members.size()||
                   (value.base&&value.base->id!=physical.base_id)||
                   (physical.kind==ObjectGroupKind::live_association&&
                    (!value.base||!object_value_source_base(value.members))))
                    throw FormatError("physical/value group extent or base differs");
                for(std::size_t n=0;n<physical.members.size();++n){const auto& body=physical.members[n];const auto& priced=value.members[n];
                    if(body.id!=priced.object.id||body.generation!=priced.object.generation||body.offset_x!=priced.offset_x||
                       body.offset_y!=priced.offset_y||body.offset_level!=priced.offset_level)
                        throw FormatError("physical/value group ordered member or offset differs");
                }
            }
    }
    for(const auto& origin:bundle.created_objects) {
        const auto entity=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
            [&](const auto& e){return e.id==origin.id&&e.generation==origin.generation;});
        if(entity!=bundle.world.entities.end()&&entity->kind==EntityKind::object&&
           entity->definition&&entity->definition->master_guid&&!grouped_created.count(origin.id))
            throw FormatError("created multipart object dropped its dynamic group authority");
    }
}
void validate_links(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    validate_house_layout_links(bundle);
    validate_dialog_links(bundle);
    validate_object_group_links(bundle,limits);
    if(bundle.family_spells){
        validate_family_spells_snapshot(*bundle.family_spells);
        if(bundle.family_spells->family_chunk_id!=bundle.host.family_chunk_id||
           bundle.family_spells->family_number!=bundle.host.family_number||
           (bundle.travel&&(bundle.family_spells->family_chunk_id!=bundle.travel->family_chunk_id||
                            bundle.family_spells->family_number!=bundle.travel->family_number)))
            throw FormatError("family spell carrier differs from the authenticated host/travel family");
    }
    const auto validate_sleep_schedule=[](const SleepSnapshot& sleep){
        if(sleep.job && sleep.job->next_evaluation) {
            const auto next=*sleep.job->next_evaluation;
            if(sleep.job->idle_start>=UINT32_MAX || next<=sleep.clock ||
               next>=sleep.job->deadline || next!=sleep.job->idle_start+1)
                throw FormatError("session Sleep override is outside its scheduled wait");
        }
    };
    const auto avatars=avatar_runtime_states(bundle);
    // Reuse the native continuation proof for every local/stack/NPC route.
    // The frozen chain is authority; never infer a missing chain on restore.
    std::optional<World> carried_world;
    const auto validate_carried=[&](const LocalRouteSnapshot& route){
        validate_carried_route_value(route,limits);
        if(route.carried_target.empty())return;
        if(!carried_world){carried_world.emplace(limits.world.world,bundle.world.attribute_policy);
            if(!carried_world->restore_snapshot(bundle.world))throw FormatError("carried route World snapshot is invalid");}
        if(carried_world->reference_is_dead({route.target,route.target_generation}))return;
        if(!route_carried_target_matches(*carried_world,{route.actor,route.actor_generation},
                {route.target,route.target_generation},route.carried_target,route.target_position))
            throw FormatError("carried route live ancestry, generation or pose is stale");
    };
    for(const auto* state:avatars){
        if(state->route)validate_carried(*state->route);
        if(state->route_stack)for(const auto& job:state->route_stack->jobs)validate_carried(job);
    }
    for(const auto& object:bundle.object_threads)validate_sleep_schedule(object.sleep);
    for(const auto* state:avatars)validate_sleep_schedule(state->controllers.sleep);
    if(!valid_balloon_headline_snapshot(bundle.headlines))throw FormatError("invalid session headlines");
    for(const auto& headline:bundle.headlines) {
        const auto identity_present=[&](std::int16_t id,std::uint64_t generation){
            return std::any_of(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& entity){
                return entity.id==id && entity.generation==generation;});
        };
        if(!identity_present(headline.target,headline.target_generation))
            throw FormatError("session headline owner is missing or reused");
        // Icon provenance is weak metadata. It cannot retain an object body or
        // resolve a new occupant of the same numeric ID after deletion.
    }
    validate_host(bundle.host,limits);
    if(std::any_of(bundle.world.entities.begin(),bundle.world.entities.end(),[](const auto& e){return e.dead;}))
        throw FormatError("safe session checkpoint cannot interrupt the indexed Dead deletion phase");
    if(bundle.world.lockout_count_clock&&*bundle.world.lockout_count_clock!=static_cast<std::uint32_t>(bundle.host.tick))
        throw FormatError("World lockout clock differs from the session scheduler tick");
    validate_household(bundle);
    validate_npc_avatars(bundle,limits);
    validate_generic_execution(bundle,limits);
    validate_object_history(bundle);
    validate_reference_closure(bundle);
    validate_object_trade(bundle);
    validate_runtime_globals(bundle);
    validate_seated_session_continuations(bundle);
    if((bundle.runtime_entities.empty()&&!bundle.npc_primary)||bundle.runtime_entities.size()>limits.max_runtime_entities||
       bundle.object_threads.size()>limits.max_object_threads||
       bundle.runtime_entities.size()>UINT16_MAX||
       avatars.size()+bundle.object_threads.size()!=bundle.scheduler.entities.size()||
       (bundle.scheduler.last_tick?*bundle.scheduler.last_tick!=bundle.host.tick:bundle.host.tick!=0)) {
        throw FormatError("session runtime and scheduler do not match (runtime="+
            std::to_string(bundle.runtime_entities.size())+", scheduler="+
            std::to_string(bundle.scheduler.entities.size())+", host_tick="+
            std::to_string(bundle.host.tick)+", scheduler_tick="+
            (bundle.scheduler.last_tick?std::to_string(*bundle.scheduler.last_tick):"unset")+")");
    }
    auto reference_entities=bundle.world.entities;
    reference_entities.insert(reference_entities.end(),bundle.world.retained_entities.begin(),bundle.world.retained_entities.end());
    std::int16_t previous=0;
    bool found_primary=false;
    for(const auto* state:avatars) {
        const auto& entity=*state;
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
            const auto target=reference_entity(bundle.world,{action.call.context.callee,action.target_generation});
            const auto& c=action.call.context;
            if(!target||(c.entity_references_bound&&(c.caller_generation!=world_actor->generation||
               c.callee_generation!=action.target_generation||c.stack_object_generation!=action.target_generation)))
                throw FormatError("session queued interaction target is stale or mixes entity generations");
        }
        validate_controller_vm_links(entity,bundle.host.tick);
        validate_dynamic_action_owners(entity,reference_entities,
            find_seated_session_continuation(bundle,entity.id));
        validate_route_links(entity,*world_actor,reference_entities,bundle.host.tick,limits);
        validate_playback_links(entity,*world_actor,bundle.host,limits);
        found_primary|=entity.id==bundle.host.runtime_avatar_id;
        previous=entity.id;
    }
    if(!found_primary)throw FormatError("session primary resident is absent from runtime roster");
    std::int16_t previous_object=0;
    for(const auto& thread:bundle.object_threads){
        const auto resident=std::lower_bound(avatars.begin(),avatars.end(),
            thread.id,[](const auto* value,std::int16_t id){return value->id<id;});
        if(thread.id<=previous_object||(resident!=avatars.end()&&(*resident)->id==thread.id))
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
    std::vector<std::int16_t> expected_ids;expected_ids.reserve(avatars.size()+bundle.object_threads.size());
    for(const auto* entity:avatars)expected_ids.push_back(entity->id);
    for(const auto& thread:bundle.object_threads)expected_ids.push_back(thread.id);
    std::sort(expected_ids.begin(),expected_ids.end());
    for(std::size_t i=0;i<expected_ids.size();++i)
        if(bundle.scheduler.entities[i].id!=expected_ids[i])
            throw FormatError("session scheduler contains an unrepresented VM thread");
    for(const auto& entity:bundle.world.entities)if(!entity.static_footprint){
        // Full native actor state owns this mutable class independently of
        // whether the current route has begun walking. Other route/link and
        // spatial-registry checks remain responsible for their own state.
        if(bundle.generic_execution&&entity.kind==EntityKind::avatar&&
           std::any_of(avatars.begin(),avatars.end(),[&](const auto* runtime){
               return runtime->id==entity.id&&runtime->controllers.interactions.actor_id==entity.id&&
                   runtime->controllers.interactions.actor_generation==entity.generation;}))continue;
        if(std::any_of(bundle.npc_avatars.begin(),bundle.npc_avatars.end(),[&](const auto& npc){
            return npc.origin.id==entity.id&&npc.origin.generation==entity.generation;}))continue;
        if(bundle.generic_execution&&std::any_of(bundle.created_objects.begin(),bundle.created_objects.end(),
            [&](const auto& origin){return origin.id==entity.id&&origin.generation==entity.generation;}))continue;
        const auto route=std::find_if(avatars.begin(),avatars.end(),
            [&](const auto* runtime){return runtime->id==entity.id&&(runtime->route.has_value()||(runtime->route_stack&&!runtime->route_stack->jobs.empty()));});
        const bool go_here_marker=entity.kind==EntityKind::object&&entity.ui_ghost&&entity.position&&entity.definition&&
            entity.definition->guid==0x7c4&&std::any_of(avatars.begin(),avatars.end(),
                [&](const auto* runtime){const auto matches=[&](const LocalRouteSnapshot& route){return route.target==entity.id&&
                    route.target_generation==entity.generation&&route.target_position==*entity.position;};
                    const bool route_matches=(runtime->route&&matches(*runtime->route))||(runtime->route_stack&&
                        !runtime->route_stack->jobs.empty()&&matches(runtime->route_stack->jobs.front()));
                    return route_matches&&
                    std::any_of(runtime->dynamic_action_owners.begin(),runtime->dynamic_action_owners.end(),
                        [&](const auto& owner){return owner.target_id==entity.id&&owner.target_generation==entity.generation&&
                            owner.target_guid==0x7c4&&owner.root_routine==4096;});});
        if((entity.kind!=EntityKind::avatar||route==avatars.end())&&!go_here_marker)
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
std::vector<std::uint8_t> encode_character_family(const SessionCharacterFamily& state,std::size_t limit) {
    const std::set<std::uint32_t> unique(state.runtime_subset.begin(),state.runtime_subset.end());
    if(state.runtime_subset.size()>256||unique.size()!=state.runtime_subset.size()||std::any_of(state.runtime_subset.begin(),state.runtime_subset.end(),[](auto guid){return !guid;}))
        throw FormatError("current family runtime subset exceeds source bounds");
    const auto shared=encode_ts1_character_family_snapshot(state.shared);
    Writer w(limit);w.u16(1);w.u32(static_cast<std::uint32_t>(shared.size()));w.append(shared.data(),shared.size());
    w.u16(static_cast<std::uint16_t>(state.runtime_subset.size()));for(auto guid:state.runtime_subset)w.u32(guid);return w.take();
}
SessionCharacterFamily decode_character_family(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);if(r.u16()!=1)throw FormatError("unsupported current family carrier version");
    const auto size=r.u32();if(size>4u*1024u*1024u||size>r.remaining())throw FormatError("current family shared state exceeds bounds");
    SessionCharacterFamily out;out.shared=decode_ts1_character_family_snapshot(r.bytes(size));
    const auto n=r.u16();if(n>256||n>r.remaining()/4)throw FormatError("current family runtime subset exceeds bounds");
    for(unsigned i=0;i<n;++i){const auto guid=r.u32();if(!guid)throw FormatError("current family runtime GUID is zero");out.runtime_subset.push_back(guid);}
    if(!r.done())throw FormatError("current family carrier has trailing data");
    if(std::set<std::uint32_t>(out.runtime_subset.begin(),out.runtime_subset.end()).size()!=out.runtime_subset.size())throw FormatError("current family runtime subset repeats a GUID");return out;
}
void validate_family_lease(const SessionBundle& bundle) {
    if(bundle.npc_primary){
        const auto ref=*bundle.npc_primary;
        const auto npc=std::find_if(bundle.npc_avatars.begin(),bundle.npc_avatars.end(),[&](const auto& value){return value.origin.id==ref.id&&value.origin.generation==ref.generation;});
        if(!bundle.generic_execution||!bundle.family_lease||!bundle.character_family||!bundle.travel||!bundle.travel->family_roster_revision||
           bundle.travel->current_house==bundle.travel->home_house||!bundle.travel->family_activated||
           !bundle.household_roster.empty()||!bundle.runtime_entities.empty()||npc==bundle.npc_avatars.end()||
           ref.id!=bundle.host.runtime_avatar_id||npc->origin.guid!=bundle.host.resident_guid||
           npc->origin.neighbor_id!=bundle.host.resident_neighbor_id)
            throw FormatError("NPC primary lacks its exact visited execution/family/NPCA binding");
        const auto& origin=npc->origin;
        const Ts1TravelMember member{origin.guid,origin.neighbor_id,origin.object_definition_id,native_personal_resource_key(origin.source,origin.asset),origin.character_fingerprint};
        if(std::none_of(bundle.travel->travelers.begin(),bundle.travel->travelers.end(),[&](const auto& traveler){return same_ts1_travel_member(member,traveler);}))
            throw FormatError("NPC primary is outside the authenticated traveling cohort");
    }
    if(bundle.character_family){
        const auto& shared=bundle.character_family->shared;validate_ts1_character_family_snapshot(shared);
        (void)encode_character_family(*bundle.character_family,4u*1024u*1024u);
        if(!bundle.generic_execution||!bundle.family_lease||shared.neighborhood_identity!=bundle.family_lease->neighborhood_identity||
           (bundle.travel&&shared.neighborhood_identity!=bundle.travel->neighborhood_identity))
            throw FormatError("current family carrier lacks exact managed neighborhood provenance");
        for(const auto& created:shared.characters){
            const auto& identity=created.identity;
            const auto exact=[&](std::uint32_t guid,std::int16_t nid,std::uint16_t chunk,const std::string& asset,const auto& hash){
                if((guid==identity.guid||nid==identity.neighbor_id)&&
                   (guid!=identity.guid||nid!=identity.neighbor_id||chunk!=identity.definition_chunk_id||
                    asset!=identity.character_asset||hash!=identity.character_fingerprint))
                    throw FormatError("created character receipt differs from its execution resource identity");
            };
            for(const auto& member:bundle.household_roster)
                exact(member.guid,member.neighbor_id,member.object_definition_id,member.character_asset,member.character_fingerprint);
            for(const auto& npc:bundle.npc_avatars){const auto& origin=npc.origin;
                exact(origin.guid,origin.neighbor_id,origin.object_definition_id,native_personal_resource_key(origin.source,origin.asset),origin.character_fingerprint);
            }
            if(bundle.personal_state)for(const auto& record:bundle.personal_state->records)
                exact(record.identity.guid,record.identity.neighbor_id,record.identity.definition_chunk_id,record.identity.character_asset,record.identity.character_fingerprint);
        }
        for(const auto guid:bundle.character_family->runtime_subset){
            const auto household=std::count_if(bundle.household_roster.begin(),bundle.household_roster.end(),[&](const auto& m){return m.guid==guid;});
            const auto npc=std::count_if(bundle.npc_avatars.begin(),bundle.npc_avatars.end(),[&](const auto& n){return n.origin.guid==guid;});
            if(household+npc==1)continue;
            const auto created=std::find_if(shared.characters.begin(),shared.characters.end(),[&](const auto& c){return c.identity.guid==guid;});
            const auto* family=find_ts1_family_roster(shared,bundle.host.family_chunk_id);
            const auto body=[&](const auto& e){return e.kind==EntityKind::avatar&&e.definition&&e.definition->guid==guid;};
            const bool retired=bundle.object_history&&(std::any_of(bundle.object_history->retired_npc_avatars.begin(),bundle.object_history->retired_npc_avatars.end(),[&](const auto& n){return n.guid==guid;})||
                std::any_of(bundle.object_history->retired_household_avatars.begin(),bundle.object_history->retired_household_avatars.end(),[&](const auto& h){return h.member.guid==guid;}));
            // Make19 -> Generic4 -> constructor42 can legitimately stop between
            // source instructions. An exact new receipt does not invent a body,
            // rerun a constructor or reclassify a retired/existing NPC origin.
            if(household+npc!=0||created==shared.characters.end()||!family||
               std::find(family->members.begin(),family->members.end(),guid)==family->members.end()||
               std::any_of(bundle.world.entities.begin(),bundle.world.entities.end(),body)||
               std::any_of(bundle.world.retained_entities.begin(),bundle.world.retained_entities.end(),body)||retired)
                throw FormatError("current family execution GUID lacks an exact carrier or pending created-neighbor receipt");
        }
        const auto* row=find_ts1_family_roster(shared,bundle.host.family_chunk_id);
        if(row&&row->source_identity!=bundle.family_lease->family_identity)throw FormatError("current family roster changes the stable session identity");
    }
    if(bundle.family_vacation){
        validate_family_vacation_snapshot(*bundle.family_vacation);
        if((bundle.family_lease&&bundle.family_vacation->neighborhood_identity!=bundle.family_lease->neighborhood_identity)||
           (bundle.travel&&bundle.family_vacation->neighborhood_identity!=bundle.travel->neighborhood_identity))
            throw FormatError("vacation snapshot neighborhood differs from session");
        const auto row=std::find_if(bundle.family_vacation->families.begin(),bundle.family_vacation->families.end(),[&](const auto& f){return f.family_chunk_id==bundle.host.family_chunk_id;});
        if(row==bundle.family_vacation->families.end()||!row->active||row->serialized_family_number!=bundle.host.family_number||
           (bundle.family_lease&&row->family_identity!=bundle.family_lease->family_identity)||
           (bundle.travel&&row->family_identity!=bundle.travel->family_identity))
            throw FormatError("vacation snapshot selected family provenance differs");
    }
    if(bundle.personal_state){
        validate_native_personal_snapshot(*bundle.personal_state);
        if((bundle.family_lease&&bundle.personal_state->neighborhood_identity!=bundle.family_lease->neighborhood_identity)||
           (bundle.travel&&bundle.personal_state->neighborhood_identity!=bundle.travel->neighborhood_identity))
            throw FormatError("personal carrier neighborhood differs from session authority");
    }
    if(!bundle.family_lease)return;
    const auto& lease=*bundle.family_lease;
    const auto zero=[](const auto& hash){return std::all_of(hash.begin(),hash.end(),[](auto b){return b==0;});};
    if(zero(lease.neighborhood_identity)||zero(lease.family_identity)||!lease.occupancy_epoch||
       lease.family_chunk_id!=bundle.host.family_chunk_id||bundle.host.house_number<1||
       bundle.host.house_number>98||(bundle.household_roster.empty()&&!bundle.npc_primary)||!bundle.generic_execution||
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
void validate_ui_state(const SessionBundle& bundle) {
    if(!bundle.ui_state)return;
    const auto& ui=*bundle.ui_state;
    const auto actor=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),
        [&](const auto& entity){return entity.id==ui.active_actor;});
    if(ui.active_actor<=0||!ui.generation||!ui.guid||actor==bundle.world.entities.end()||
       actor->kind!=EntityKind::avatar||actor->ui_ghost||actor->generation!=ui.generation||
       !actor->definition||actor->definition->guid!=ui.guid)
        throw FormatError("session UI selected avatar is missing or stale");
    // CFAM stores changed family rows, not the complete original FAMI
    // catalog. A creation-only receipt has no membership change. When a
    // current row exists it governs selection for HHRD and NPCA alike.
    const auto* family=bundle.character_family?find_ts1_family_roster(bundle.character_family->shared,bundle.host.family_chunk_id):nullptr;
    if(family&&std::find(family->members.begin(),family->members.end(),ui.guid)==family->members.end())
        throw FormatError("session UI selection is outside current FAMI");
    if(bundle.household_roster.empty()&&!bundle.npc_primary){
        if(ui.active_actor!=bundle.host.runtime_avatar_id||ui.guid!=bundle.host.resident_guid)
            throw FormatError("legacy session UI selection differs from HOST resident");
    }else{
        const auto member=std::find_if(bundle.household_roster.begin(),bundle.household_roster.end(),
            [&](const auto& entry){return entry.id==ui.active_actor&&entry.generation==ui.generation&&entry.guid==ui.guid;});
        if(member==bundle.household_roster.end()){
            const auto npc=std::find_if(bundle.npc_avatars.begin(),bundle.npc_avatars.end(),[&](const auto& n){return n.origin.id==ui.active_actor&&n.origin.generation==ui.generation&&n.origin.guid==ui.guid;});
            if(npc==bundle.npc_avatars.end()||!family)
                throw FormatError("session UI selection is outside current FAMI/exact constructor identity");
        }
    }
}
std::vector<std::uint8_t> encode_ui_state(const SessionUiState& ui,std::size_t limit) {
    Writer w(limit);w.u16(1);w.i16(ui.active_actor);w.u64(ui.generation);w.u32(ui.guid);return w.take();
}
SessionUiState decode_ui_state(const std::vector<std::uint8_t>& data) {
    if(data.size()!=16)throw FormatError("invalid session UI state extent");
    Reader r(data);if(r.u16()!=1)throw FormatError("unsupported session UI state version");
    SessionUiState ui;ui.active_actor=r.i16();ui.generation=r.u64();ui.guid=r.u32();return ui;
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
    Writer w(limit);w.u16(2);const auto& c=sim.clock;
    w.u64(c.ticks);w.u16(c.minute_fraction);w.u8(c.minute);w.u8(c.hour);w.u8(c.day);w.u8(c.month);w.i32(c.year);w.i32(c.fire_percent);
    w.u16(static_cast<std::uint16_t>(sim.avatars.size()));
    for(const auto& a:sim.avatars){w.i16(a.id);w.u8(a.decay.last_two_minute_bucket);for(auto f:a.decay.fractions)w.i16(f);}
    return w.take();
}
SessionSimulationState decode_simulation(const std::vector<std::uint8_t>& bytes,std::size_t max_avatars) {
    Reader r(bytes);const auto version=r.u16();
    if(version!=1&&version!=2)throw FormatError("unsupported session simulation version");
    SessionSimulationState sim;auto& c=sim.clock;c.ticks=r.u64();c.minute_fraction=r.u16();c.minute=r.u8();c.hour=r.u8();c.day=r.u8();c.month=r.u8();c.year=r.i32();
    // SIMS1 was the native clock schema before Burn existed: it had neither
    // a fire threshold nor a mutator. Bridge that native model to VMClock's
    // constructor value; this does not recover an original-game fire history.
    c.fire_percent=version>=2?r.i32():20000;
    const auto count=r.u16();if(count>max_avatars)throw FormatError("session simulation roster exceeds bounds");
    for(unsigned i=0;i<count;++i){SessionAvatarMotiveState a;a.id=r.i16();a.decay.last_two_minute_bucket=r.u8();for(auto& f:a.decay.fractions)f=r.i16();sim.avatars.push_back(a);}
    if(!r.done())throw FormatError("session simulation has unclaimed bytes");
    return sim;
}
void write_source_sparse(Writer& w,const SourceSparseRouteSnapshot& sparse,bool allow_dormant_turn=false){
    const auto& m=sparse.motion;
    const bool no_motion=!m.current&&m.remaining.empty()&&m.phase==WalkMotionPhase::uninitialized&&
        !m.move_frames&&!m.move_total_frames&&!m.velocity&&!m.turn_frames&&
        m.walk_direction==0&&m.target_direction==0;
    WalkMotion check;if((!no_motion&&check.restore(m)!=WalkMotionStatus::success)||!std::isfinite(sparse.target_radian_direction)||
       sparse.lifecycle>2||((sparse.turn.has_value()!=(sparse.lifecycle!=0))&&
        !(allow_dormant_turn&&sparse.lifecycle==0&&sparse.turn))||m.remaining.size()>4096)
        throw FormatError("source sparse motion continuation is invalid");
    const auto f32=[&](float value){std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));w.u32(bits);};
    const auto f64=[&](double value){std::uint64_t bits=0;static_assert(sizeof(value)==sizeof(bits));
        std::memcpy(&bits,&value,sizeof(bits));w.u64(bits);};
    const auto geometry=[&](const WalkSegmentGeometry& g){w.u8(static_cast<std::uint8_t>(g.kind));
        for(const auto point:{g.a,g.b,g.c,g.d}){w.i32(point.x);w.i32(point.y);}};
    if(sparse.avatars_to_consider.size()>4096)throw FormatError("sparse avatar consideration exceeds bounds");
    w.u16(sparse.room);w.u16(static_cast<std::uint16_t>(sparse.avatars_to_consider.size()));
    std::set<std::int16_t> considered;
    for(const auto identity:sparse.avatars_to_consider){
        if(identity.id<=0||!identity.generation||!considered.insert(identity.id).second)
            throw FormatError("sparse avatar consideration identity invalid or duplicated");
        w.i16(identity.id);w.u64(identity.generation);
    }
    w.u8(sparse.profile.is_pet);w.i16(sparse.profile.person_type);
    w.u8(sparse.profile.in_pool);w.i16(sparse.profile.walk_style);f32(sparse.target_radian_direction);
    w.u8(sparse.face_anywhere);w.u8(sparse.hard_start);w.u8(sparse.locomotion_started);w.u8(sparse.lifecycle);
    w.u8(sparse.turn?1:0);if(sparse.turn)write_source_look_route(w,*sparse.turn);
    w.u8(sparse.choices?1:0);if(sparse.choices)write_source_range(w,*sparse.choices);
    w.u8(m.current?1:0);if(m.current)geometry(*m.current);
    w.u32(static_cast<std::uint32_t>(m.remaining.size()));for(const auto& g:m.remaining)geometry(g);
    w.i32(m.previous_position.position.x);w.i32(m.previous_position.position.y);w.i16(m.previous_position.level);f32(m.previous_position.radians);
    w.i32(m.move_frames);w.i32(m.move_total_frames);w.i32(m.velocity);w.i32(m.turn_frames);
    f64(m.walk_direction);f64(m.target_direction);w.u8(static_cast<std::uint8_t>(m.phase));
}
SourceSparseRouteSnapshot read_source_sparse(Reader& r,bool allow_shoo=false){
    SourceSparseRouteSnapshot sparse;
    const auto flag=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid sparse motion boolean");return value!=0;};
    const auto f32=[&](){const auto bits=r.u32();float value=0;std::memcpy(&value,&bits,sizeof(value));return value;};
    const auto f64=[&](){const auto bits=r.u64();double value=0;std::memcpy(&value,&bits,sizeof(value));return value;};
    const auto geometry=[&](){WalkSegmentGeometry g;const auto kind=r.u8();
        if(kind>static_cast<std::uint8_t>(WalkSegmentKind::cubic))throw FormatError("invalid sparse path segment kind");
        g.kind=static_cast<WalkSegmentKind>(kind);g.a={r.i32(),r.i32()};g.b={r.i32(),r.i32()};
        g.c={r.i32(),r.i32()};g.d={r.i32(),r.i32()};return g;};
    sparse.room=r.u16();const auto avatars=r.u16();if(avatars>4096||avatars>r.remaining()/10)
        throw FormatError("sparse avatar consideration exceeds section bounds");
    sparse.avatars_to_consider.reserve(avatars);for(std::uint16_t i=0;i<avatars;++i)
        sparse.avatars_to_consider.push_back({r.i16(),r.u64()});
    sparse.profile.is_pet=flag();sparse.profile.person_type=r.i16();
    sparse.profile.in_pool=flag();sparse.profile.walk_style=r.i16();sparse.target_radian_direction=f32();
    sparse.face_anywhere=flag();sparse.hard_start=flag();sparse.locomotion_started=flag();sparse.lifecycle=r.u8();
    if(flag())sparse.turn=read_source_look_route(r);
    if(flag())sparse.choices=read_source_range(r);
    auto& m=sparse.motion;if(flag())m.current=geometry();
    const auto count=r.u32();if(count>4096||count>r.remaining()/33)throw FormatError("sparse path exceeds segment bounds");
    m.remaining.reserve(count);for(std::uint32_t i=0;i<count;++i)m.remaining.push_back(geometry());
    m.previous_position.position={r.i32(),r.i32()};m.previous_position.level=r.i16();m.previous_position.radians=f32();
    m.move_frames=r.i32();m.move_total_frames=r.i32();m.velocity=r.i32();m.turn_frames=r.i32();
    m.walk_direction=f64();m.target_direction=f64();const auto phase=r.u8();
    if(phase>static_cast<std::uint8_t>(WalkMotionPhase::finished))throw FormatError("invalid sparse motion phase");
    m.phase=static_cast<WalkMotionPhase>(phase);Writer validation(1024*1024);write_source_sparse(validation,sparse,allow_shoo);
    return sparse;
}
void write_source_collision(Writer& w,const SourceRouteCollisionSnapshot& collision) {
    SourceRouteCollision verifier;if(!verifier.restore(collision)||collision.considered.size()>4096)
        throw FormatError("source route collision state is invalid");
    w.i32(collision.retries);w.i32(collision.wait_time);w.i32(collision.timeout);w.u8(static_cast<std::uint8_t>(collision.phase));
    w.u16(static_cast<std::uint16_t>(collision.considered.size()));
    for(const auto& identity:collision.considered){w.i16(identity.id);w.u64(identity.generation);}
}
SourceRouteCollisionSnapshot read_source_collision(Reader& r) {
    SourceRouteCollisionSnapshot collision;collision.retries=r.i32();collision.wait_time=r.i32();collision.timeout=r.i32();
    const auto phase=r.u8();if(phase>static_cast<std::uint8_t>(SourceRouteCollisionPhase::soft_failure))
        throw FormatError("invalid source route collision phase");
    collision.phase=static_cast<SourceRouteCollisionPhase>(phase);const auto count=r.u16();
    if(count>4096||count>r.remaining()/10)throw FormatError("source route collision identities exceed bounds");
    collision.considered.reserve(count);for(unsigned n=0;n<count;++n)collision.considered.push_back({r.i16(),r.u64()});
    Writer validation(65536);write_source_collision(validation,collision);return collision;
}
std::vector<std::uint8_t> encode_local_route(const LocalRouteSnapshot& route,
                                             const SessionBundleLimits& limits,bool allow_empty=false) {
    if(!route.carried_target.empty()){
        validate_carried_route_value(route,limits);
        auto base=route;base.carried_target.clear();const auto bytes=encode_local_route(base,limits,allow_empty);
        Writer wrapped(limits.archive.max_section_bytes);wrapped.u16(15);wrapped.u32(static_cast<std::uint32_t>(bytes.size()));wrapped.append(bytes);
        wrapped.u16(static_cast<std::uint16_t>(route.carried_target.size()));
        for(const auto& link:route.carried_target){
            wrapped.i16(link.child.id);wrapped.u64(link.child.generation);wrapped.i16(link.parent.id);wrapped.u64(link.parent.generation);
            wrapped.u32(static_cast<std::uint32_t>(link.slot));
            wrapped.i32(link.original.x);wrapped.i32(link.original.y);wrapped.i16(link.original.level);wrapped.u8(link.original.direction);
            std::uint32_t bits=0;std::memcpy(&bits,&link.radian_direction,sizeof(bits));wrapped.u32(bits);wrapped.u8(link.positioned?1:0);
        }
        return wrapped.take();
    }
    const bool architecture_walk=route.source_sparse&&route.source_sparse->walk_to_purpose==SourceWalkToPurpose::architecture_invalidation;
    if(route.room_route_invalid||architecture_walk||route.room_route_replan_active){
        if(architecture_walk&&!route.source_sparse->walk_to_pending)throw FormatError("architecture WalkTo purpose has no pending walk");
        auto base=route;base.room_route_invalid=false;base.room_route_replan_active=false;
        if(route.room_route_replan_active)base.frame.state=RouteFrameState::initial;
        if(architecture_walk)base.source_sparse->walk_to_purpose=SourceWalkToPurpose::ranked_alternative;
        const auto bytes=encode_local_route(base,limits,allow_empty);
        Writer wrapped(limits.archive.max_section_bytes);wrapped.u16(14);wrapped.u32(static_cast<std::uint32_t>(bytes.size()));wrapped.append(bytes);
        wrapped.u8((route.room_route_invalid?1U:0U)|(architecture_walk?2U:0U)|(route.room_route_replan_active?4U:0U));
        if(route.room_route_replan_active)wrapped.u8(static_cast<std::uint8_t>(route.frame.state));
        return wrapped.take();
    }
    if(route.source_choices){
        validate_source_choices_value(route);
        auto base=route;base.source_choices.reset();const auto bytes=encode_local_route(base,limits,allow_empty);
        Writer wrapped(limits.archive.max_section_bytes);wrapped.u16(12);wrapped.u32(static_cast<std::uint32_t>(bytes.size()));
        wrapped.append(bytes);write_source_choices(wrapped,*route.source_choices);return wrapped.take();
    }
    if(route.source_sparse){
        auto base=route;base.source_sparse.reset();const auto bytes=encode_local_route(base,limits,allow_empty);
        const auto& sparse=*route.source_sparse;
        const bool shoo=sparse.walk_to_purpose==SourceWalkToPurpose::shoo;
        if(sparse.walk_to_purpose!=SourceWalkToPurpose::ranked_alternative&&!shoo)
            throw FormatError("source pending walk purpose is unsupported");
        if(shoo&&!sparse.walk_to_pending)throw FormatError("source shoo walk purpose requires a null WalkTo");
        const bool fallback_extension=sparse.walk_to_pending||!sparse.fallback_room_route.empty();
        Writer wrapped(limits.archive.max_section_bytes);wrapped.u16(shoo?11:fallback_extension?10:sparse.collision?9:8);
        wrapped.u32(static_cast<std::uint32_t>(bytes.size()));wrapped.append(bytes);write_source_sparse(wrapped,sparse,shoo);
        if(fallback_extension)wrapped.u8(sparse.collision?1:0);
        if(sparse.collision)write_source_collision(wrapped,*sparse.collision);
        if(fallback_extension){
            if(sparse.fallback_room_route.size()>32)throw FormatError("fallback room route exceeds bounds");
            wrapped.u8(sparse.walk_to_pending);wrapped.u8(static_cast<std::uint8_t>(sparse.fallback_room_route.size()));
            for(const auto& edge:sparse.fallback_room_route){
                wrapped.u16(edge.source_room);wrapped.u16(edge.target_room);wrapped.i16(edge.portal_id);
                wrapped.i32(edge.position.x);wrapped.i32(edge.position.y);wrapped.i16(edge.position.level);wrapped.u8(edge.position.direction);
            }
        }
        if(shoo)wrapped.u8(static_cast<std::uint8_t>(sparse.walk_to_purpose));
        return wrapped.take();
    }
    if((route.path.empty()&&!allow_empty)||route.path.size()>limits.max_route_points||
       route.path.size()>UINT32_MAX||route.next>UINT32_MAX)
        throw FormatError("local route snapshot exceeds path limits");
    Writer w(limits.archive.max_section_bytes);
    if(route.arrival_radian_direction&&!std::isfinite(*route.arrival_radian_direction))
        throw FormatError("local route has a non-finite continuous arrival direction");
    if(route.target_anchor_radian_direction&&!std::isfinite(*route.target_anchor_radian_direction))
        throw FormatError("local route has a non-finite self-target anchor direction");
    if(route.source_circle)validate_source_circle_value(route);
    if(route.look_towards)validate_source_look_route_value(route,limits);
    if(route.source_range)validate_source_range_value(route);
    if(route.source_circle&&route.look_towards)
        throw FormatError("local route cannot combine source-circle and LookTowards profiles");
    const auto version=route.source_range?std::uint16_t{7}:route.look_towards?std::uint16_t{6}:route.source_circle?std::uint16_t{5}:
        route.target_anchor_radian_direction?std::uint16_t{4}:
        route.arrival_radian_direction?std::uint16_t{3}:std::uint16_t{2};
    w.u16(version);w.u64(route.ticket);w.u64(route.actor_generation);w.u64(route.target_generation);
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
    if(version==7){
        std::uint32_t arrival=0;std::memcpy(&arrival,&*route.arrival_radian_direction,sizeof(arrival));w.u32(arrival);
        w.u8(route.target_anchor_radian_direction?1:0);
        if(route.target_anchor_radian_direction){std::uint32_t anchor=0;
            std::memcpy(&anchor,&*route.target_anchor_radian_direction,sizeof(anchor));w.u32(anchor);}
        write_source_range(w,*route.source_range);
    }else if(version==6){
        w.u8(1);write_source_look_route(w,*route.look_towards);
    }else if(version==5){
        std::uint32_t arrival=0;std::memcpy(&arrival,&*route.arrival_radian_direction,sizeof(arrival));w.u32(arrival);
        w.u8(route.target_anchor_radian_direction?1:0);
        if(route.target_anchor_radian_direction){std::uint32_t anchor=0;
            std::memcpy(&anchor,&*route.target_anchor_radian_direction,sizeof(anchor));w.u32(anchor);}
        write_source_circle(w,*route.source_circle);
    }else {
        if(route.target_anchor_radian_direction)w.u8(route.arrival_radian_direction?1:0);
        if(route.arrival_radian_direction){std::uint32_t arrival=0;
        std::memcpy(&arrival,&*route.arrival_radian_direction,sizeof(arrival));w.u32(arrival);}
        if(route.target_anchor_radian_direction){std::uint32_t anchor=0;
            std::memcpy(&anchor,&*route.target_anchor_radian_direction,sizeof(anchor));w.u32(anchor);}
    }
    return w.take();
}
LocalRouteSnapshot decode_local_route(const std::vector<std::uint8_t>& bytes,
                                       const SessionBundleLimits& limits,bool allow_empty=false) {
    Reader r(bytes);const auto version=r.u16();
    if(version==15){
        const auto size=r.u32();if(size>limits.archive.max_section_bytes||size>r.remaining())throw FormatError("carried route base exceeds bounds");
        const auto base=r.bytes(size);if(base.size()<2||(base[0]|(base[1]<<8))>=15)throw FormatError("recursive carried route envelope");
        auto route=decode_local_route(base,limits,allow_empty);const auto count=r.u16();
        if(!count||count>4096||count>r.remaining()/40)throw FormatError("carried route link count exceeds bounds or is noncanonical");
        route.carried_target.reserve(count);
        for(unsigned n=0;n<count;++n){RouteCarriedTargetLink link;
            link.child={r.i16(),r.u64()};link.parent={r.i16(),r.u64()};link.slot=r.u32();
            link.original={r.i32(),r.i32(),r.i16(),r.u8()};const auto bits=r.u32();std::memcpy(&link.radian_direction,&bits,sizeof(bits));
            const auto positioned=r.u8();if(positioned>1)throw FormatError("invalid carried route positioned flag");link.positioned=positioned!=0;
            route.carried_target.push_back(link);
        }
        if(!r.done())throw FormatError("carried route has trailing bytes");
        validate_carried_route_value(route,limits);return route;
    }
    if(version==14){
        const auto size=r.u32();if(size>limits.archive.max_section_bytes||size>r.remaining())throw FormatError("architecture route base exceeds bounds");
        const auto base=r.bytes(size);if(base.size()<2||(base[0]|(base[1]<<8))>=14)throw FormatError("recursive architecture route envelope");
        auto route=decode_local_route(base,limits,allow_empty);const auto flags=r.u8();if(!flags||flags>7)throw FormatError("invalid architecture route flags");
        route.room_route_invalid=(flags&1)!=0;
        if(flags&4){const auto state=r.u8();if(state!=static_cast<std::uint8_t>(RouteFrameState::initial)&&state!=static_cast<std::uint8_t>(RouteFrameState::room_portal))throw FormatError("invalid architecture replan frame state");
            route.room_route_replan_active=true;route.frame.state=static_cast<RouteFrameState>(state);}
        if(!r.done())throw FormatError("architecture route has trailing bytes");
        if(flags&2){if(!route.source_sparse||!route.source_sparse->walk_to_pending)throw FormatError("architecture walk purpose lacks source motion");route.source_sparse->walk_to_purpose=SourceWalkToPurpose::architecture_invalidation;}
        return route;
    }
    if(version==12){
        const auto size=r.u32();if(size>limits.archive.max_section_bytes||size>r.remaining())throw FormatError("source SLOT base exceeds bounds");
        const auto base=r.bytes(size);if(base.size()<2||(base[0]|(base[1]<<8))>=12)throw FormatError("recursive source SLOT choice envelope");
        auto route=decode_local_route(base,limits,allow_empty);route.source_choices=read_source_choices(r);
        if(!r.done())throw FormatError("source SLOT choices have trailing bytes");
        validate_source_choices_value(route);return route;
    }
    if(version==8||version==9||version==10||version==11){
        const auto size=r.u32();if(size>limits.archive.max_section_bytes||size>r.remaining())
            throw FormatError("source sparse base route exceeds section bounds");
        const auto base=r.bytes(size);if(base.size()<2||(base[0]|(base[1]<<8))>=8)
            throw FormatError("recursive source sparse route envelope");
        auto route=decode_local_route(base,limits,allow_empty);route.source_sparse=read_source_sparse(r,version==11);
        const auto flag=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid sparse fallback flag");return value!=0;};
        if(version==9||((version==10||version==11)&&flag()))route.source_sparse->collision=read_source_collision(r);
        if(version==10||version==11){auto& sparse=*route.source_sparse;sparse.walk_to_pending=flag();const auto edges=r.u8();
            if(edges>32||edges>r.remaining()/17)throw FormatError("fallback room route exceeds bounded payload");
            sparse.fallback_room_route.reserve(edges);for(unsigned n=0;n<edges;++n){RoomPortalEdge edge;
                edge.source_room=r.u16();edge.target_room=r.u16();edge.portal_id=r.i16();
                edge.position={r.i32(),r.i32(),r.i16(),r.u8()};sparse.fallback_room_route.push_back(edge);}
            if(!sparse.walk_to_pending&&sparse.fallback_room_route.empty())throw FormatError("noncanonical sparse fallback extension");
        }
        if(version==11){const auto purpose=r.u8();
            if(purpose!=static_cast<std::uint8_t>(SourceWalkToPurpose::shoo)||!route.source_sparse->walk_to_pending)
                throw FormatError("noncanonical source shoo pending walk purpose");
            route.source_sparse->walk_to_purpose=SourceWalkToPurpose::shoo;
        }
        if(!r.done())throw FormatError("source sparse route has trailing bytes");
        return route;
    }
    if(version<1||version>7)throw FormatError("unsupported local route snapshot version");
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
    if((count==0&&!allow_empty)||count>limits.max_route_points||count>r.remaining()/8)
        throw FormatError("saved route path exceeds bounds");
    route.path.reserve(count);for(std::uint32_t n=0;n<count;++n)route.path.push_back({r.i32(),r.i32()});
    route.next=r.u32();route.entry_flags=r.i16();route.facing=r.u8();
    const auto face_anywhere=r.u8(),walking_started=r.u8(),same_room=r.u8();
    const auto call_failure_trees=version>=2?r.u8():0;
    if(version==7){
        const auto arrival_bits=r.u32();float arrival=0;std::memcpy(&arrival,&arrival_bits,sizeof(arrival));
        if(!std::isfinite(arrival))throw FormatError("non-finite saved source range arrival direction");
        route.arrival_radian_direction=arrival;
        const auto has_anchor=r.u8();if(has_anchor>1)throw FormatError("invalid source range anchor marker");
        if(has_anchor){const auto anchor_bits=r.u32();float anchor=0;std::memcpy(&anchor,&anchor_bits,sizeof(anchor));
            if(!std::isfinite(anchor))throw FormatError("non-finite saved source range anchor direction");
            route.target_anchor_radian_direction=anchor;}
        route.source_range=read_source_range(r);
    }else if(version==6){
        const auto has_look=r.u8();if(has_look!=1)throw FormatError("invalid LookTowards route marker");
        route.look_towards=read_source_look_route(r);
    }else if(version==5){
        const auto arrival_bits=r.u32();float arrival=0;std::memcpy(&arrival,&arrival_bits,sizeof(arrival));
        if(!std::isfinite(arrival))throw FormatError("non-finite saved source circle arrival direction");
        route.arrival_radian_direction=arrival;
        const auto has_anchor=r.u8();if(has_anchor>1)throw FormatError("invalid source circle anchor marker");
        if(has_anchor){const auto anchor_bits=r.u32();float anchor=0;
            std::memcpy(&anchor,&anchor_bits,sizeof(anchor));
            if(!std::isfinite(anchor))throw FormatError("non-finite saved source circle anchor direction");
            route.target_anchor_radian_direction=anchor;}
        route.source_circle=read_source_circle(r);
    }else {
        const auto has_arrival=version>=4?r.u8():static_cast<std::uint8_t>(version>=3);
        if(has_arrival>1)throw FormatError("invalid saved route arrival marker");
        if(has_arrival){const auto arrival_bits=r.u32();float arrival=0;
            std::memcpy(&arrival,&arrival_bits,sizeof(arrival));
            if(!std::isfinite(arrival))throw FormatError("non-finite saved route arrival direction");
            route.arrival_radian_direction=arrival;}
        if(version==4){const auto anchor_bits=r.u32();float anchor=0;
            std::memcpy(&anchor,&anchor_bits,sizeof(anchor));
            if(!std::isfinite(anchor))throw FormatError("non-finite saved self-target anchor direction");
            route.target_anchor_radian_direction=anchor;}
    }
    if(face_anywhere>1||walking_started>1||same_room>1||call_failure_trees>1||!r.done())
        throw FormatError("invalid saved route flags or trailing bytes");
    route.face_anywhere=face_anywhere!=0;route.walking_started=walking_started!=0;
    route.same_room_verified=same_room!=0;route.call_failure_trees=call_failure_trees!=0;
    if(route.source_circle)validate_source_circle_value(route);
    if(route.look_towards)validate_source_look_route_value(route,limits);
    if(route.source_range)validate_source_range_value(route);
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
void route_blob(Writer& w,const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()>UINT32_MAX)throw FormatError("route stack blob exceeds wire bounds");
    w.u32(static_cast<std::uint32_t>(bytes.size()));w.append(bytes);
}
std::vector<std::uint8_t> route_blob(Reader& r,const SessionBundleLimits& limits) {
    const auto size=r.u32();if(size>limits.archive.max_section_bytes||size>r.remaining())
        throw FormatError("route stack blob exceeds bounds");
    return r.bytes(size);
}
bool route_flag(Reader& r) {const auto flag=r.u8();if(flag>1)throw FormatError("invalid route stack flag");return flag!=0;}
void route_position(Writer& w,const EntityPosition& p) {w.i32(p.x);w.i32(p.y);w.i16(p.level);w.u8(p.direction);}
EntityPosition route_position(Reader& r) {return {r.i32(),r.i32(),r.i16(),r.u8()};}
void route_call(Writer& w,const VmCall& call,const SessionBundleLimits& limits,bool modern) {
    route_blob(w,route_vm_values(route_call_frame(call),std::nullopt,limits));
    if(modern){w.u8(call.action_tree_override.has_value());if(call.action_tree_override)w.u8(*call.action_tree_override);}
}
VmCall route_call(Reader& r,const SessionBundleLimits& limits,bool modern) {
    const auto state=decode_vm_snapshot(route_blob(r,limits),limits.vm);
    if(state.stack.size()!=1||state.pending||!state.suspended_pending.empty()||state.external_frame_return||
       state.interaction_return||!state.immediate_continuations.empty()||state.immediate_return||state.advertisement_changes)
        throw FormatError("invalid route call value envelope");
    const auto& frame=state.stack.front();
    if(frame.pc||!frame.locals.empty()||frame.invocation_ticket)
        throw FormatError("route call envelope contains frame-only state");
    auto call=route_frame_call(frame);
    if(modern&&route_flag(r))call.action_tree_override=route_flag(r);
    if(frame.action_tree!=call.action_tree_override.value_or(false))throw FormatError("route call ActionTree override differs from frame envelope");
    return call;
}
bool modern_route_stack(const RouteStackSnapshot& stack){
    const auto frame=[](const VmFrame& f){return f.action_tree||f.context.entity_references_bound;};
    const auto call=[](const VmCall& c){return c.context.entity_references_bound||c.action_tree_override.has_value();};
    for(const auto& j:stack.jobs)if(frame(j.route_frame)||j.pending.context.entity_references_bound||
        (j.replay_frame&&frame(*j.replay_frame))||(j.external_frame_call&&call(*j.external_frame_call)))return true;
    for(const auto& p:stack.portal_contexts)if(p.pending_call&&call(*p.pending_call))return true;
    return false;
}
std::vector<std::uint8_t> encode_route_stack(const RouteStackSnapshot& stack,const SessionBundleLimits& limits) {
    if(stack.jobs.size()>limits.vm.vm.stack_depth||stack.jobs.size()>UINT16_MAX||
       stack.portal_contexts.size()>stack.jobs.size())throw FormatError("route stack count exceeds bounds");
    const bool shoo=std::any_of(stack.jobs.begin(),stack.jobs.end(),[](const auto& job){return job.external_frame_purpose.has_value();});
    const bool architecture=std::any_of(stack.jobs.begin(),stack.jobs.end(),[](const auto&job){return job.room_route_replan_active;});
    const bool unplaced=std::any_of(stack.jobs.begin(),stack.jobs.end(),[](const auto& job){return job.unplaced_room_failure.has_value();});
    const bool modern=unplaced||modern_route_stack(stack);
    Writer w(limits.archive.max_section_bytes);w.u16(unplaced?5:modern?4:architecture?3:shoo?2:1);w.u8(stack.last_tick?1:0);if(stack.last_tick)w.u64(*stack.last_tick);
    w.u16(static_cast<std::uint16_t>(stack.jobs.size()));
    for(const auto& job:stack.jobs){
        route_blob(w,encode_local_route(job,limits,true));
        VmSnapshot values;values.stack.push_back(job.route_frame);values.pending=job.pending;
        if(job.replay_frame)values.stack.push_back(*job.replay_frame);
        values.action_tree_known=std::any_of(values.stack.begin(),values.stack.end(),[](const auto& f){return f.action_tree||f.context.entity_references_bound;})||job.pending.context.entity_references_bound;
        w.u8(job.replay_frame?1:0);w.u8(job.replay_instruction?1:0);
        if(job.replay_instruction){auto replay=job.pending;replay.instruction.instruction=*job.replay_instruction;
            values.suspended_pending.push_back(replay);}
        route_blob(w,encode_vm_snapshot(values,limits.vm));
        w.u8(job.external_frame_active);w.u32(job.external_frame_owner);w.u8(job.external_frame_call?1:0);
        if(job.external_frame_call)route_call(w,*job.external_frame_call,limits,modern);
        if(job.portal_retries>32||job.portal_steps>32||job.fallback_goals.size()>4096)
            throw FormatError("route job counters exceed bounds");
        w.u8(static_cast<std::uint8_t>(job.portal_retries));w.u8(job.portal_replan_required);w.u8(job.deferred_room_route);
        w.u8(static_cast<std::uint8_t>(job.portal_steps));w.u8(job.room_context_open);w.u8(job.failure_tree_invoked);
        w.u8(job.failure_pending);w.u8(job.interrupt_pending);w.i16(job.failure_code);w.i16(job.failure_blocker);
        w.string(job.failure_reason);w.u8(job.selected_sitting_object?1:0);if(job.selected_sitting_object)w.i16(*job.selected_sitting_object);
        w.u16(static_cast<std::uint16_t>(job.fallback_goals.size()));
        for(const auto& goal:job.fallback_goals){
            w.i32(goal.position.x);w.i32(goal.position.y);w.u8(goal.entry_flags);w.u8(goal.facing);
            w.u8(goal.sitting_object?1:0);if(goal.sitting_object)w.i16(*goal.sitting_object);
            w.u8(goal.sitting_approach?1:0);if(goal.sitting_approach){w.i32(goal.sitting_approach->x);w.i32(goal.sitting_approach->y);}
            w.u8(goal.radian_direction?1:0);if(goal.radian_direction){std::uint32_t bits;std::memcpy(&bits,&*goal.radian_direction,4);w.u32(bits);}
        }
        w.u8(job.external_frame_sitting);w.u8(job.sparse_consideration?1:0);
        if(job.sparse_consideration){if(job.sparse_consideration->size()>4096)throw FormatError("route consideration exceeds bounds");
            w.u16(static_cast<std::uint16_t>(job.sparse_consideration->size()));
            for(const auto& identity:*job.sparse_consideration){w.i16(identity.id);w.u64(identity.generation);}}
        if(shoo||architecture||modern){w.u8(job.external_frame_purpose?1:0);if(job.external_frame_purpose){
            if(*job.external_frame_purpose!=RouteExternalFramePurpose::shoo&&*job.external_frame_purpose!=RouteExternalFramePurpose::standing)
                throw FormatError("route external purpose is unsupported");
            w.u8(static_cast<std::uint8_t>(*job.external_frame_purpose));w.u64(job.external_frame_callee_generation);
        }}else if(job.external_frame_callee_generation)throw FormatError("route external callee generation has no purpose");
        if(architecture||modern)w.u8(job.room_route_replan_active);
        if(unplaced){w.u8(job.unplaced_room_failure?1:0);if(job.unplaced_room_failure){
            if(!is_source_unplaced_room_failure(job))throw FormatError("invalid unplaced room failure continuation");
            const auto& origin=*job.unplaced_room_failure;
            w.u32(origin.slot_version);w.u32(static_cast<std::uint32_t>(origin.slot_index));
            write_slot_record(w,origin.slot);w.u16(origin.target_room);
        }}
    }
    w.u16(static_cast<std::uint16_t>(stack.portal_contexts.size()));
    for(const auto& context:stack.portal_contexts){
        w.u64(context.ticket);if(context.ignored.size()>4096)throw FormatError("ignored route portals exceed bounds");
        w.u16(static_cast<std::uint16_t>(context.ignored.size()));
        for(const auto& ignored:context.ignored){w.i16(ignored.portal_id);w.u16(ignored.target_room);}
        w.u8(context.pending_edge?1:0);if(context.pending_edge){const auto& edge=*context.pending_edge;
            w.u16(edge.source_room);w.u16(edge.target_room);w.i16(edge.portal_id);route_position(w,edge.position);}
        w.u8(context.pending_call?1:0);if(context.pending_call)route_call(w,*context.pending_call,limits,modern);
        w.u64(context.portal_generation);
    }
    if(stack.portal_owners.size()>limits.max_resource_owners_per_thread||stack.portal_owners.size()>UINT16_MAX)
        throw FormatError("route portal owner count exceeds bounds");
    w.u16(static_cast<std::uint16_t>(stack.portal_owners.size()));
    for(const auto& owner:stack.portal_owners){w.string(owner.source);w.string(owner.asset);w.u16(owner.action);w.u32(owner.owner);}
    w.u64(stack.portal_planned_calls);w.u64(stack.portal_returns);w.u64(stack.portal_transitions);
    return w.take();
}
RouteStackSnapshot decode_route_stack(const std::vector<std::uint8_t>& bytes,const SessionBundleLimits& limits) {
    Reader r(bytes);const auto version=r.u16();
    if(version<1||version>5)throw FormatError("unsupported route stack version");RouteStackSnapshot stack;
    if(route_flag(r))stack.last_tick=r.u64();const auto count=r.u16();
    if(count>limits.vm.vm.stack_depth||count>r.remaining()/16)throw FormatError("route stack count exceeds bounds");
    stack.jobs.reserve(count);
    for(unsigned i=0;i<count;++i){
        RouteJobSnapshot job;static_cast<LocalRouteSnapshot&>(job)=decode_local_route(route_blob(r,limits),limits,true);
        const bool replay_frame=route_flag(r),replay_instruction=route_flag(r);
        const auto values=decode_vm_snapshot(route_blob(r,limits),limits.vm);
        if(values.stack.size()!=(replay_frame?2U:1U)||!values.pending||
           values.suspended_pending.size()!=(replay_instruction?1U:0U)||values.external_frame_return||values.interaction_return||
           !values.immediate_continuations.empty()||values.immediate_return||values.advertisement_changes)
            throw FormatError("invalid route job VM value envelope");
        job.route_frame=values.stack.front();job.pending=*values.pending;
        if(replay_frame)job.replay_frame=values.stack.back();
        if(replay_instruction)job.replay_instruction=values.suspended_pending.front().instruction.instruction;
        job.external_frame_active=route_flag(r);job.external_frame_owner=r.u32();if(route_flag(r))job.external_frame_call=route_call(r,limits,version>=4);
        job.portal_retries=r.u8();job.portal_replan_required=route_flag(r);job.deferred_room_route=route_flag(r);
        job.portal_steps=r.u8();job.room_context_open=route_flag(r);job.failure_tree_invoked=route_flag(r);
        job.failure_pending=route_flag(r);job.interrupt_pending=route_flag(r);job.failure_code=r.i16();job.failure_blocker=r.i16();
        job.failure_reason=r.string();if(route_flag(r))job.selected_sitting_object=r.i16();
        const auto goals=r.u16();if(goals>4096||goals>r.remaining()/13)throw FormatError("route fallback count exceeds bounds");
        job.fallback_goals.reserve(goals);for(unsigned g=0;g<goals;++g){PreRankedSlotGoal goal;
            goal.position={r.i32(),r.i32()};goal.entry_flags=r.u8();goal.facing=r.u8();
            if(route_flag(r))goal.sitting_object=r.i16();if(route_flag(r))goal.sitting_approach=RoutePoint{r.i32(),r.i32()};
            if(route_flag(r)){const auto bits=r.u32();float value;std::memcpy(&value,&bits,4);goal.radian_direction=value;}
            job.fallback_goals.push_back(goal);}
        job.external_frame_sitting=route_flag(r);if(route_flag(r)){const auto identities=r.u16();
            if(identities>4096||identities>r.remaining()/10)throw FormatError("route consideration count exceeds bounds");
            job.sparse_consideration.emplace();job.sparse_consideration->reserve(identities);
            for(unsigned n=0;n<identities;++n)job.sparse_consideration->push_back({r.i16(),r.u64()});}
        if(version>=2&&route_flag(r)){const auto purpose=r.u8();
            if(purpose!=static_cast<std::uint8_t>(RouteExternalFramePurpose::shoo)&&purpose!=static_cast<std::uint8_t>(RouteExternalFramePurpose::standing))
                throw FormatError("unknown route external purpose");
            job.external_frame_purpose=static_cast<RouteExternalFramePurpose>(purpose);job.external_frame_callee_generation=r.u64();
        }
        if(version>=3){const auto phase=route_flag(r);if(phase!=job.room_route_replan_active)throw FormatError("architecture route stack/local phase markers disagree");}
        if(version>=5&&route_flag(r)){
            SourceUnplacedRoomFailure origin;origin.slot_version=r.u32();origin.slot_index=r.u32();
            origin.slot=read_slot_record(r);origin.target_room=r.u16();job.unplaced_room_failure=std::move(origin);
            if(!is_source_unplaced_room_failure(job))throw FormatError("invalid unplaced room failure continuation");
        }
        stack.jobs.push_back(std::move(job));
    }
    if(version==5&&std::none_of(stack.jobs.begin(),stack.jobs.end(),[](const auto& job){return job.unplaced_room_failure.has_value();}))
        throw FormatError("noncanonical unplaced room failure extension");
    if(version<3&&std::any_of(stack.jobs.begin(),stack.jobs.end(),[](const auto&job){return job.room_route_replan_active;}))
        throw FormatError("architecture room replan requires route stack version3");
    if(version==3&&std::none_of(stack.jobs.begin(),stack.jobs.end(),[](const auto&job){return job.room_route_replan_active;}))
        throw FormatError("noncanonical architecture room replan extension");
    if(version==2&&std::none_of(stack.jobs.begin(),stack.jobs.end(),[](const auto& job){return job.external_frame_purpose.has_value();}))
        throw FormatError("noncanonical route stack purpose extension");
    const auto contexts=r.u16();if(contexts>count||contexts>r.remaining()/20)throw FormatError("route portal context count exceeds bounds");
    stack.portal_contexts.reserve(contexts);for(unsigned i=0;i<contexts;++i){RoutePortalContextSnapshot context;
        context.ticket=r.u64();const auto ignored=r.u16();if(ignored>4096||ignored>r.remaining()/4)throw FormatError("ignored portal count exceeds bounds");
        context.ignored.reserve(ignored);for(unsigned n=0;n<ignored;++n)context.ignored.push_back({r.i16(),r.u16()});
        if(route_flag(r)){RoomPortalEdge edge;edge.source_room=r.u16();edge.target_room=r.u16();edge.portal_id=r.i16();edge.position=route_position(r);context.pending_edge=edge;}
        if(route_flag(r))context.pending_call=route_call(r,limits,version>=4);context.portal_generation=r.u64();stack.portal_contexts.push_back(std::move(context));}
    const auto owners=r.u16();if(owners>limits.max_resource_owners_per_thread||owners>r.remaining()/10)
        throw FormatError("route portal owner count exceeds bounds");
    stack.portal_owners.reserve(owners);for(unsigned n=0;n<owners;++n){RoutePortalOwnerSnapshot owner;
        owner.source=r.string();owner.asset=r.string();owner.action=r.u16();owner.owner=r.u32();stack.portal_owners.push_back(std::move(owner));}
    stack.portal_planned_calls=r.u64();stack.portal_returns=r.u64();stack.portal_transitions=r.u64();
    if(!r.done())throw FormatError("route stack has trailing bytes");
    if(version==4&&!modern_route_stack(stack))throw FormatError("noncanonical reference-aware route stack");
    return stack;
}
std::vector<std::uint8_t> encode_route_stacks(const std::vector<SessionRuntimeEntity>& entities,const SessionBundleLimits& limits) {
    return encode_roster(entities,limits,[&](const auto& entity){Writer w(limits.archive.max_section_bytes);
        w.u8(entity.route_stack?1:0);if(entity.route_stack)w.append(encode_route_stack(*entity.route_stack,limits));return w.take();});
}
void decode_route_stacks(const std::vector<std::uint8_t>& bytes,std::vector<SessionRuntimeEntity>& entities,const SessionBundleLimits& limits) {
    decode_roster(bytes,entities,limits,[&](auto& entity,const auto& payload){Reader r(payload);
        if(route_flag(r))entity.route_stack=decode_route_stack(r.bytes(r.remaining()),limits);
        else if(!r.done())throw FormatError("empty route stack roster entry has trailing bytes");});
}
void validate_architecture_object_links(const SessionBundle& bundle){
    if(!bundle.architecture_object_state)return;
    if(bundle.architecture_object_support)throw FormatError("competing architecture object state owners");
    const auto& state=*bundle.architecture_object_state;
    const auto live=[&](ObjectPositionIdentity id)->const Entity*{
        const auto at=std::lower_bound(bundle.world.entities.begin(),bundle.world.entities.end(),id.id,[](const auto& e,auto selected){return e.id<selected;});
        if(at==bundle.world.entities.end()||at->id!=id.id||at->generation!=id.generation||at->ui_ghost)
            throw FormatError("architecture object event references a stale entity");
        return &*at;
    };
    for(const auto& face:state.wall_appearance.faces){
        if(!is_canonical_cleared_wall_face(face)){
            const auto* owner=live({face.owner,face.generation});
            if(owner->kind!=EntityKind::object||!owner->definition||
               (face.style.resource_guid&&owner->definition->guid!=face.style.resource_guid))
                throw FormatError("architecture wall event resource identity differs from its object");
        }
        if(face.door_owner)live({face.door_owner,face.door_generation});
    }
    if(state.position_registry){const auto& registry=*state.position_registry;
        const auto historical=[&](ObjectPositionIdentity id){return !registry.complete&&std::any_of(registry.retired_owners.begin(),registry.retired_owners.end(),[&](auto old){return old.id==id.id&&old.generation==id.generation;});};
        const auto registered=[&](ObjectPositionIdentity id){
            if(historical(id))return;
            if(!registry.complete){const auto ghost=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& e){return e.id==id.id&&e.generation==id.generation&&e.ui_ghost;});
                if(ghost!=bundle.world.entities.end())return; // Explicit live ghost, never a retired identity or query authority.
            }
            live(id);
        };
        for(const auto id:registry.retired_owners){const auto at=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& e){return e.id==id.id&&e.generation==id.generation;});
            if(at!=bundle.world.entities.end())throw FormatError("retired registry identity is still live");}
        for(const auto& room:registry.rooms){
            for(const auto id:room.entities)registered(id);
            for(const auto& item:room.static_obstacles)registered(item.owner);
            for(const auto& item:room.dynamic_obstacles)registered(item.owner);
            for(const auto& item:room.portals)registered(item.owner);
            for(const auto& item:room.window_portals)registered(item.owner);
        }
        for(const auto& tile:registry.tiles)for(const auto id:tile.entities)registered(id);
        for(const auto& motion:registry.motion)registered(motion.owner);
        for(const auto& binding:registry.footprints)registered(binding.owner);
    }
}
bool recognized(const std::array<char,4>& id) {
    return id==npc_primary_id||id==character_family_id||id==family_vacation_id||id==personal_state_id||id==house_layout_id||id==object_groups_id||id==type_attributes_id||id==family_spells_id||id==build_buy_id||id==travel_id||id==inventory_id||id==visited_id||id==home_id||id==simulation_id||id==meta_id||id==host_id||id==world_id||id==relation_id||
           id==household_id||id==vm_id||id==scheduler_id||id==controller_id||id==playback_id||
           id==seated_id||id==route_id||id==route_stack_id||id==object_vm_id||id==dynamic_owner_id||id==generic_execution_id||id==object_trade_id||id==runtime_globals_id||id==object_history_id||id==family_lease_id||id==ui_state_id||id==dialog_id||id==headline_id||id==npc_avatar_id||id==architecture_id||id==architecture_support_id||id==architecture_objects_id;
}

void write_sleep_snapshot(Writer& w,const SleepSnapshot& state,std::uint16_t version) {
    w.u8(state.actor?1:0);
    if(state.actor){w.i16(state.actor->id);w.u64(state.actor->generation);w.u64(state.actor->run_generation);}
    w.u8(state.job?1:0);
    if(state.job){w.u64(state.job->ticket);w.u64(state.job->idle_start);w.u64(state.job->deadline);
        w.u64(static_cast<std::uint64_t>(state.job->argument));
        if(version>=2){w.u8(state.job->next_evaluation?1:0);if(state.job->next_evaluation)w.u64(*state.job->next_evaluation);}}
    w.u8(state.last_tick?1:0);if(state.last_tick)w.u64(*state.last_tick);
    w.u64(state.clock);w.u8(state.interrupted?1:0);w.u8(state.notify_resume?1:0);w.string(state.diagnostic);
}
SleepSnapshot read_sleep_snapshot(Reader& r,std::uint16_t version) {
    const auto flag=[&](){const auto value=r.u8();if(value>1)throw FormatError("invalid object Sleep flag");return value!=0;};
    SleepSnapshot state;
    if(flag())state.actor=SleepActorSnapshot{r.i16(),r.u64(),r.u64()};
    if(flag()){
        const auto ticket=r.u64(),idle=r.u64(),deadline=r.u64(),argument=r.u64();
        if(argument>SIZE_MAX)throw FormatError("object Sleep argument exceeds platform bounds");
        state.job=SleepJobSnapshot{ticket,idle,deadline,static_cast<std::size_t>(argument)};
        if(version>=2 && flag())state.job->next_evaluation=r.u64();
    }
    if(flag())state.last_tick=r.u64();
    state.clock=r.u64();state.interrupted=flag();
    state.notify_resume=flag();state.diagnostic=r.string();return state;
}

std::vector<std::uint8_t> encode_object_threads(const std::vector<SessionObjectThread>& threads,
                                                const SessionBundleLimits& limits) {
    const std::uint16_t version=std::any_of(threads.begin(),threads.end(),[](const auto& thread){
        return thread.sleep.job && thread.sleep.job->next_evaluation.has_value();})?2:1;
    Writer w(limits.archive.max_section_bytes);w.u16(version);
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
        write_sleep_snapshot(entry,thread.sleep,version);
        const auto data=entry.take();if(data.size()>UINT32_MAX)throw FormatError("object thread exceeds format");
        w.i16(thread.id);w.u32(static_cast<std::uint32_t>(data.size()));w.append(data);
    }
    return w.take();
}

std::vector<SessionObjectThread> decode_object_threads(const std::vector<std::uint8_t>& bytes,
                                                        const SessionBundleLimits& limits) {
    Reader r(bytes);const auto version=r.u16();if(version!=1 && version!=2)throw FormatError("unsupported object thread roster version");
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
        thread.vm=decode_vm_snapshot(entry.bytes(vm_size),limits.vm);thread.sleep=read_sleep_snapshot(entry,version);
        if(!entry.done())throw FormatError("object thread entry has trailing data");
        result.push_back(std::move(thread));previous=result.back().id;
    }
    if(!r.done())throw FormatError("object thread roster has trailing data");
    return result;
}
// NPCA owns its complete visitor payload. Existing VM/CTRL/PLAY, GEXP and SIMS
// sections retain their resident/object profiles and their old bytes.
void write_npc_resource_owner(Writer& w,const SessionResourceCodeOwner& owner) {
    w.u32(owner.owner);w.u32(owner.resource_guid);w.string(owner.source);w.string(owner.asset);
    w.u16(owner.definition_chunk_id);w.u16(owner.tree_table_id);w.u16(owner.root_routine);
    w.u8(static_cast<std::uint8_t>(owner.bank_kind));w.u8(static_cast<std::uint8_t>(owner.identity));
    write_linked_semiglobal(w,owner.linked_semiglobal);
    w.append(owner.routine_fingerprint.data(),owner.routine_fingerprint.size());
}
SessionResourceCodeOwner read_npc_resource_owner(Reader& r) {
    SessionResourceCodeOwner owner;owner.owner=r.u32();owner.resource_guid=r.u32();
    owner.source=r.string();owner.asset=r.string();owner.definition_chunk_id=r.u16();
    owner.tree_table_id=r.u16();owner.root_routine=r.u16();
    owner.bank_kind=static_cast<SessionCodeBankKind>(r.u8());
    owner.identity=static_cast<SessionCodeOwnerIdentity>(r.u8());owner.linked_semiglobal=read_linked_semiglobal(r);
    const auto hash=r.bytes(owner.routine_fingerprint.size());
    std::copy(hash.begin(),hash.end(),owner.routine_fingerprint.begin());return owner;
}
void write_npc_blob(Writer& w,const std::vector<std::uint8_t>& value) {
    if(value.size()>UINT32_MAX)throw FormatError("NPC payload exceeds wire bounds");
    w.u32(static_cast<std::uint32_t>(value.size()));w.append(value);
}
std::vector<std::uint8_t> read_npc_blob(Reader& r,const SessionBundleLimits& limits) {
    const auto size=r.u32();
    if(size>limits.archive.max_section_bytes||size>r.remaining())throw FormatError("NPC payload exceeds entry bounds");
    return r.bytes(size);
}
std::vector<std::uint8_t> encode_npc_avatars(const std::vector<SessionNpcAvatar>& npcs,
    const SessionBundleLimits& limits) {
    const bool stacks=std::any_of(npcs.begin(),npcs.end(),[](const auto& npc){return npc.runtime.route_stack.has_value();});
    const bool resource_only=std::any_of(npcs.begin(),npcs.end(),[](const auto& npc){return npc.origin.neighbor_id==0;});
    Writer w(limits.archive.max_section_bytes);w.u16(resource_only?3:stacks?2:1);w.u16(static_cast<std::uint16_t>(npcs.size()));
    for(const auto& npc:npcs) {
        Writer entry(limits.archive.max_section_bytes);const auto& origin=npc.origin;
        entry.i16(origin.id);entry.u64(origin.generation);entry.u32(origin.guid);entry.i16(origin.neighbor_id);
        entry.string(origin.source);entry.string(origin.asset);entry.u16(origin.object_definition_id);
        entry.append(origin.character_fingerprint.data(),origin.character_fingerprint.size());
        entry.u32(npc.primary_owner);entry.u8(npc.decay.last_two_minute_bucket);
        for(const auto fraction:npc.decay.fractions)entry.i16(fraction);
        entry.u8(npc.selected_name?1:0);if(npc.selected_name)entry.string(*npc.selected_name);
        write_npc_blob(entry,encode_vm_snapshot(npc.runtime.vm,limits.vm));
        write_npc_blob(entry,encode_entity_thread_controllers_snapshot(npc.runtime.controllers,limits.controllers));
        write_npc_blob(entry,encode_playback(npc.runtime.playback,limits));
        entry.u8(npc.runtime.route_stack?2:npc.runtime.route?1:0);
        if(npc.runtime.route)write_npc_blob(entry,encode_local_route(*npc.runtime.route,limits));
        if(npc.runtime.route_stack)write_npc_blob(entry,encode_route_stack(*npc.runtime.route_stack,limits));
        write_npc_blob(entry,encode_dynamic_owners({npc.runtime},limits));
        entry.u16(static_cast<std::uint16_t>(npc.runtime.resource_owners.size()));
        for(const auto& owner:npc.runtime.resource_owners)write_npc_resource_owner(entry,owner);
        write_npc_blob(w,entry.take());
    }
    return w.take();
}
std::vector<SessionNpcAvatar> decode_npc_avatars(const std::vector<std::uint8_t>& bytes,
    const SessionBundleLimits& limits) {
    Reader r(bytes);const auto version=r.u16();if(version!=1&&version!=2&&version!=3)throw FormatError("unsupported NPC avatar version");
    const auto count=r.u16();
    if(!count||count>std::min<std::size_t>(64,limits.max_npc_avatars)||count>r.remaining()/4)
        throw FormatError("NPC avatar count exceeds bounds");
    std::vector<SessionNpcAvatar> result;result.reserve(count);std::int16_t previous=0;
    for(unsigned n=0;n<count;++n) {
        const auto payload=read_npc_blob(r,limits);Reader entry(payload);SessionNpcAvatar npc;auto& origin=npc.origin;
        origin.id=entry.i16();origin.generation=entry.u64();origin.guid=entry.u32();origin.neighbor_id=entry.i16();
        if(origin.neighbor_id==0&&version<3)throw FormatError("resource-only NPC requires version3");
        if(origin.id<=previous)throw FormatError("NPC avatar manifest is unordered or duplicated");
        previous=origin.id;origin.source=entry.string();origin.asset=entry.string();origin.object_definition_id=entry.u16();
        const auto fingerprint=entry.bytes(origin.character_fingerprint.size());
        std::copy(fingerprint.begin(),fingerprint.end(),origin.character_fingerprint.begin());
        npc.primary_owner=entry.u32();npc.decay.last_two_minute_bucket=entry.u8();
        for(auto& fraction:npc.decay.fractions)fraction=entry.i16();
        const auto name=entry.u8();if(name>1)throw FormatError("invalid NPC name choice marker");
        if(name)npc.selected_name=entry.string();
        npc.runtime.id=origin.id;npc.runtime.vm=decode_vm_snapshot(read_npc_blob(entry,limits),limits.vm);
        npc.runtime.controllers=decode_entity_thread_controllers_snapshot(read_npc_blob(entry,limits),limits.controllers);
        npc.runtime.playback=decode_playback(read_npc_blob(entry,limits),limits);
        const auto route=entry.u8();if(route>(version>=2?2:1))throw FormatError("invalid NPC route marker");
        if(route==1)npc.runtime.route=decode_local_route(read_npc_blob(entry,limits),limits);
        if(route==2)npc.runtime.route_stack=decode_route_stack(read_npc_blob(entry,limits),limits);
        std::vector<SessionRuntimeEntity> dynamic(1);dynamic.front().id=origin.id;
        decode_dynamic_owners(read_npc_blob(entry,limits),dynamic,limits);
        npc.runtime.dynamic_action_owners=std::move(dynamic.front().dynamic_action_owners);
        const auto owners=entry.u16();
        if(owners>limits.max_resource_owners_per_thread||owners>entry.remaining()/53)
            throw FormatError("NPC resource owner count exceeds bounds");
        npc.runtime.resource_owners.reserve(owners);
        for(unsigned owner=0;owner<owners;++owner)npc.runtime.resource_owners.push_back(read_npc_resource_owner(entry));
        if(!entry.done())throw FormatError("NPC avatar entry has trailing data");
        result.push_back(std::move(npc));
    }
    if(!r.done())throw FormatError("NPC avatar roster has trailing data");
    return result;
}

}

bool same_session_retired_household_avatar(const SessionRetiredHouseholdAvatar& a,const SessionRetiredHouseholdAvatar& b){
    return same_session_household_roster({a.member},{b.member})&&a.source_checkpoint_sha256==b.source_checkpoint_sha256&&
        a.source_lease.neighborhood_identity==b.source_lease.neighborhood_identity&&a.source_lease.family_identity==b.source_lease.family_identity&&
        a.source_lease.occupancy_epoch==b.source_lease.occupancy_epoch&&a.source_lease.family_chunk_id==b.source_lease.family_chunk_id;
}
void validate_retired_household_avatar_source(const SessionRetiredHouseholdAvatar& origin,const std::vector<std::uint8_t>& bytes){
    if(sha256(bytes.data(),bytes.size())!=origin.source_checkpoint_sha256)
        throw FormatError("retired household source archive digest differs");
    const auto source=decode_session_bundle(bytes);
    const auto& lease=origin.source_lease;
    if(!source.family_lease||source.family_lease->neighborhood_identity!=lease.neighborhood_identity||
       source.family_lease->family_identity!=lease.family_identity||source.family_lease->occupancy_epoch!=lease.occupancy_epoch||
       source.family_lease->family_chunk_id!=lease.family_chunk_id||source.host.family_chunk_id!=lease.family_chunk_id)
        throw FormatError("retired household source lease differs");
    const auto member=std::find_if(source.household_roster.begin(),source.household_roster.end(),[&](const auto& value){
        return same_session_household_roster({value},{origin.member});});
    if(member==source.household_roster.end())throw FormatError("retired household source HHRD differs");
    const auto body=std::find_if(source.world.entities.begin(),source.world.entities.end(),[&](const auto& value){
        return value.id==member->id&&value.generation==member->generation;});
    if(body==source.world.entities.end()||body->kind!=EntityKind::avatar||!body->definition||body->definition->guid!=member->guid||
       body->person_data.size()<=61||body->person_data[31]!=member->neighbor_id||
       static_cast<std::uint16_t>(body->person_data[61])!=lease.family_chunk_id)
        throw FormatError("retired household source avatar differs from HHRD/lease");
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

void resolve_object_thread_action_tree(SessionObjectThread& object) noexcept {
    // Imported ordinary frames start clear; actions pushed since import may
    // already carry explicit ancestry even before the first native save.
    object.vm.action_tree_known=true;
}

void bind_legacy_session_entity_references(SessionBundle& bundle,const World& world){
    const auto bind=[&](VmContext& c){
        if(!c.entity_references_bound)for(const auto id:{c.caller,c.callee,c.stack_object})
            if(id>0&&!world.find(id))throw FormatError("legacy native context has an unresolved entity identity");
        if(!world.bind_context_live(c))throw FormatError("legacy session reference binding failed");
    };
    const auto vm=[&](VmSnapshot& v){
        for(auto& f:v.stack)bind(f.context);
        if(v.pending)bind(v.pending->context);
        for(auto& p:v.suspended_pending)bind(p.context);
        for(auto& c:v.immediate_continuations)if(c.parent_pending)bind(c.parent_pending->context);
    };
    const auto runtime=[&](SessionRuntimeEntity& r){
        vm(r.vm);
        if(!InteractionQueue::bind_legacy_references(r.controllers.interactions,world)||
           !InteractionQueue::upgrade_legacy_action_tree(r.vm,r.controllers.interactions))
            throw FormatError("legacy queued action/reference binding failed");
        if(r.route_stack&&!bind_legacy_route_references(*r.route_stack,world,r.vm))
            throw FormatError("legacy route reference binding failed");
    };
    for(auto& r:bundle.runtime_entities)runtime(r);
    for(auto& npc:bundle.npc_avatars)runtime(npc.runtime);
    for(auto& object:bundle.object_threads){
        vm(object.vm);
        resolve_object_thread_action_tree(object);
    }
    validate_reference_closure(bundle);
}

namespace {
std::vector<std::uint8_t> encode_neighbor_inventory(const NeighborInventorySnapshot& state,std::size_t limit) {
    NeighborInventory checked(Ngbh{});checked.restore(state);
    Writer w(limit);w.u32(state.version);w.u32(static_cast<std::uint32_t>(state.inventory_by_id.size()));
    for(const auto& pair:state.inventory_by_id){w.i16(pair.first);w.u32(static_cast<std::uint32_t>(pair.second.size()));for(const auto& item:pair.second){w.i32(item.type);w.u32(item.guid);w.u16(item.count);}}
    return w.take();
}
NeighborInventorySnapshot decode_neighbor_inventory(const std::vector<std::uint8_t>& bytes) {
    Reader r(bytes);NeighborInventorySnapshot state;state.version=r.u32();const auto count=r.u32();if(count>4096)throw FormatError("neighbor inventory count exceeds limits");std::size_t total=0;
    for(std::uint32_t i=0;i<count;++i){const auto nid=r.i16();const auto size=r.u32();if(size>65536-total||state.inventory_by_id.count(nid))throw FormatError("invalid neighbor inventory extent or duplicate NID");total+=size;auto& list=state.inventory_by_id[nid];for(std::uint32_t j=0;j<size;++j)list.push_back({r.i32(),r.u32(),r.u16()});}
    if(!r.done())throw FormatError("trailing neighbor inventory bytes");NeighborInventory checked(Ngbh{});checked.restore(state);return state;
}
}

std::vector<std::uint8_t> encode_session_bundle(const SessionBundle& bundle,
                                                 SessionBundleLimits limits) {
    validate_ts1_travel_bundle(bundle,limits);
    validate_links(bundle,limits);
    validate_simulation(bundle);
    validate_family_lease(bundle);
    validate_architecture_object_links(bundle);
    validate_ui_state(bundle);
    SessionArchive archive;
    archive.sections={
        {meta_id,true,encode_session_resource_identity(bundle.resources)},
        {host_id,true,encode_host(bundle.host,limits,!bundle.household_roster.empty(),bundle.object_trade.has_value(),bundle.runtime_globals.has_value())},
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
    if(bundle.house_layout_history)archive.sections.push_back({house_layout_id,true,
        encode_house_layout_section(bundle,limits.archive.max_section_bytes)});
    if(bundle.npc_primary){Writer w(12);w.u16(1);w.i16(bundle.npc_primary->id);w.u64(bundle.npc_primary->generation);
        archive.sections.push_back({npc_primary_id,true,w.take()});}
    if(bundle.character_family)archive.sections.push_back({character_family_id,true,encode_character_family(*bundle.character_family,limits.archive.max_section_bytes)});
    if(bundle.family_vacation)archive.sections.push_back({family_vacation_id,true,encode_family_vacation_snapshot(*bundle.family_vacation)});
    if(bundle.family_spells)archive.sections.push_back({family_spells_id,true,encode_family_spells_snapshot(*bundle.family_spells)});
    if(bundle.personal_state){
        if(bundle.family_lease&&bundle.personal_state->neighborhood_identity!=bundle.family_lease->neighborhood_identity)
            throw FormatError("personal state neighborhood differs from family lease");
        if(bundle.travel&&bundle.personal_state->neighborhood_identity!=bundle.travel->neighborhood_identity)
            throw FormatError("personal state neighborhood differs from travel");
        archive.sections.push_back({personal_state_id,true,encode_native_personal_snapshot(*bundle.personal_state)});
    }
    if(bundle.type_attributes)archive.sections.push_back({type_attributes_id,true,encode_type_attributes_snapshot(*bundle.type_attributes)});
    if(bundle.object_groups){auto group_limits=limits.object_groups;
        group_limits.payload_bytes=std::min<std::uint64_t>(group_limits.payload_bytes,limits.archive.max_section_bytes);
        archive.sections.push_back({object_groups_id,true,encode_object_group_snapshot(*bundle.object_groups,group_limits)});}
    if(bundle.build_buy_permission)archive.sections.push_back({build_buy_id,true,encode_build_buy_permission(*bundle.build_buy_permission)});
    if(bundle.neighbor_inventory)archive.sections.push_back({inventory_id,true,encode_neighbor_inventory(*bundle.neighbor_inventory,limits.archive.max_section_bytes)});
    if(bundle.travel)archive.sections.push_back({travel_id,true,encode_ts1_travel_state(*bundle.travel)});
    if(bundle.travel_visited_session)archive.sections.push_back({visited_id,true,*bundle.travel_visited_session});
    if(bundle.travel_home_session)archive.sections.push_back({home_id,true,*bundle.travel_home_session});
    if(!bundle.household_roster.empty())archive.sections.push_back({household_id,true,encode_household(bundle.household_roster,limits)});
    if(std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [](const auto& entity){return entity.route.has_value();}))
        archive.sections.push_back({route_id,false,encode_routes(bundle.runtime_entities,limits)});
    if(std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),[](const auto& entity){return entity.route_stack.has_value();}))
        archive.sections.push_back({route_stack_id,false,encode_route_stacks(bundle.runtime_entities,limits)});
    if(!bundle.object_threads.empty())
        archive.sections.push_back({object_vm_id,false,encode_object_threads(bundle.object_threads,limits)});
    if(std::any_of(bundle.runtime_entities.begin(),bundle.runtime_entities.end(),
        [](const auto& entity){return !entity.dynamic_action_owners.empty();}))
        archive.sections.push_back({dynamic_owner_id,false,encode_dynamic_owners(bundle.runtime_entities,limits)});
    if(bundle.seated_continuation)archive.sections.push_back({seated_id,true,encode_seated(*bundle.seated_continuation,limits)});
    if(!bundle.household_seated_continuations.empty())archive.sections.push_back({seated_id,true,encode_household_seated(bundle,limits)});
    if(bundle.simulation)archive.sections.push_back({simulation_id,true,encode_simulation(*bundle.simulation,limits.archive.max_section_bytes)});
    if(bundle.generic_execution)archive.sections.push_back({generic_execution_id,true,encode_generic_execution(bundle,limits)});
    if(bundle.object_trade)archive.sections.push_back({object_trade_id,true,encode_object_trade(*bundle.object_trade,limits)});
    if(bundle.runtime_globals)archive.sections.push_back({runtime_globals_id,true,encode_runtime_globals(*bundle.runtime_globals,limits)});
    if(bundle.object_history)archive.sections.push_back({object_history_id,true,encode_object_history(*bundle.object_history,limits)});
    if(bundle.family_lease)archive.sections.push_back({family_lease_id,true,encode_family_lease(*bundle.family_lease,limits.archive.max_section_bytes)});
    if(bundle.ui_state)archive.sections.push_back({ui_state_id,false,encode_ui_state(*bundle.ui_state,limits.archive.max_section_bytes)});
    if(bundle.dialog)archive.sections.push_back({dialog_id,true,encode_dialog_state(*bundle.dialog)});
    if(!bundle.headlines.empty())archive.sections.push_back({headline_id,true,encode_balloon_headline_snapshot(bundle.headlines)});
    if(!bundle.npc_avatars.empty())archive.sections.push_back({npc_avatar_id,true,encode_npc_avatars(bundle.npc_avatars,limits)});
    if(bundle.architecture)archive.sections.push_back({architecture_id,true,encode_architecture_snapshot(*bundle.architecture)});
    if(bundle.architecture_object_state){
        if(bundle.architecture_object_support)throw FormatError("competing architecture object state owners");
        const auto& state=*bundle.architecture_object_state;
        if(bundle.architecture&&(state.object_support.width!=bundle.architecture->prepared.width||state.object_support.height!=bundle.architecture->prepared.height))
            throw FormatError("architecture object state extent differs from edited architecture");
        archive.sections.push_back({architecture_objects_id,true,encode_architecture_object_state(state)});
    }
    if(bundle.architecture_object_support){
        const auto& support=*bundle.architecture_object_support;
        validate_architecture_object_support(support);
        if(bundle.architecture&&(support.width!=bundle.architecture->prepared.width||support.height!=bundle.architecture->prepared.height))
            throw FormatError("object support extent differs from edited architecture");
        Writer w(limits.archive.max_section_bytes);w.u16(1);w.u32(support.width);w.u32(support.height);
        for(const auto& level:support.levels)w.append(level);
        archive.sections.push_back({architecture_support_id,true,w.take()});
    }
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
    bool household=false,trade=false,globals=false;
    result.host=decode_host(required_section(archive,host_id).data,limits,household,trade,globals);
    const auto globals_section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& section){return section.id==runtime_globals_id;});
    if(globals)result.runtime_globals=decode_runtime_globals(required_section(archive,runtime_globals_id).data);
    else if(globals_section!=archive.sections.end())throw FormatError("runtime globals require HOST v6");
    const auto trade_section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==object_trade_id;});
    if(trade)result.object_trade=decode_object_trade(required_section(archive,object_trade_id).data);
    else if(trade_section!=archive.sections.end())throw FormatError("trade ledger requires HOST v5");
    const auto roster_section=std::find_if(archive.sections.begin(),archive.sections.end(),[](const auto& s){return s.id==household_id;});
    if(household)result.household_roster=decode_household(required_section(archive,household_id).data,limits);
    else if(roster_section!=archive.sections.end())throw FormatError("legacy session cannot contain household roster");
    for(const auto& section:archive.sections)if(section.id==npc_primary_id){
        if(!section.required)throw FormatError("NPC primary carrier must be required");
        Reader reader(section.data);if(reader.u16()!=1)throw FormatError("unsupported NPC primary profile");
        result.npc_primary=WorldEntityReference{reader.i16(),reader.u64()};
        if(!reader.done())throw FormatError("NPC primary carrier has trailing data");
    }
    result.world=decode_world_snapshot(required_section(archive,world_id).data,limits.world);
    result.relationships=decode_relationship_snapshot(required_section(archive,relation_id).data,
                                                       limits.relationships);
    decode_roster(required_section(archive,vm_id).data,result.runtime_entities,limits,
        [&](auto& entity,const auto& data){entity.vm=decode_vm_snapshot(data,limits.vm);},result.npc_primary.has_value());
    result.scheduler=decode_entity_vm_scheduler_snapshot(required_section(archive,scheduler_id).data,
                                                         limits.scheduler);
    decode_roster(required_section(archive,controller_id).data,result.runtime_entities,limits,
        [&](auto& entity,const auto& data){
            entity.controllers=decode_entity_thread_controllers_snapshot(data,limits.controllers);},result.npc_primary.has_value());
    decode_roster(required_section(archive,playback_id).data,result.runtime_entities,limits,
        [&](auto& entity,const auto& data){entity.playback=decode_playback(data,limits);},result.npc_primary.has_value());
    for(const auto& section:archive.sections)if(section.id==route_stack_id){
        if(section.required)throw FormatError("route stack section must remain optional");
        decode_route_stacks(section.data,result.runtime_entities,limits);
    }
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
    for(const auto& section:archive.sections)if(section.id==object_history_id){
        if(!section.required)throw FormatError("object history profile must be required");
        result.object_history=decode_object_history(section.data);
    }
    for(const auto& section:archive.sections)if(section.id==family_lease_id){
        if(!section.required)throw FormatError("family lease section must be required");
        result.family_lease=decode_family_lease(section.data);
    }
    for(const auto& section:archive.sections)if(section.id==ui_state_id){
        if(section.required)throw FormatError("session UI state section must remain optional");
        result.ui_state=decode_ui_state(section.data);
    }
    for(const auto& section:archive.sections)if(section.id==headline_id){
        if(!section.required)throw FormatError("session headline section must be required");
        result.headlines=decode_balloon_headline_snapshot(section.data);
        if(result.headlines.empty())throw FormatError("empty headline section must be omitted");
    }
    for(const auto& section:archive.sections)if(section.id==npc_avatar_id){
        if(!section.required)throw FormatError("NPC avatar section must be required");
        result.npc_avatars=decode_npc_avatars(section.data,limits);
    }
    for(const auto& section:archive.sections)if(section.id==architecture_id){
        if(!section.required)throw FormatError("edited architecture section must be required");
        result.architecture=decode_architecture_snapshot(section.data);
    }
    for(const auto& section:archive.sections)if(section.id==architecture_support_id){
        if(!section.required)throw FormatError("object support section must be required");
        Reader r(section.data);
        if(r.u16()!=1)throw FormatError("unsupported object support section version");
        ArchitectureObjectSupport support;support.width=r.u32();support.height=r.u32();
        if(!support.width||!support.height||support.width>64||support.height>64)
            throw FormatError("object support extent exceeds limits");
        const auto cells=std::size_t(support.width)*support.height;
        for(auto& level:support.levels)level=r.bytes(cells);
        if(!r.done())throw FormatError("object support section has trailing bytes");
        validate_architecture_object_support(support);
        if(result.architecture&&(support.width!=result.architecture->prepared.width||support.height!=result.architecture->prepared.height))
            throw FormatError("object support extent differs from edited architecture");
        result.architecture_object_support=std::move(support);
    }
    for(const auto& section:archive.sections)if(section.id==architecture_objects_id){
        if(!section.required)throw FormatError("architecture object state section must be required");
        if(result.architecture_object_support)throw FormatError("competing architecture object state owners");
        auto state=decode_architecture_object_state(section.data);
        if(result.architecture&&(state.object_support.width!=result.architecture->prepared.width||state.object_support.height!=result.architecture->prepared.height))
            throw FormatError("architecture object state extent differs from edited architecture");
        result.architecture_object_state=std::move(state);
    }
    for(const auto& section:archive.sections)if(section.id==dialog_id){
        if(!section.required)throw FormatError("session dialog section must be required");
        result.dialog=decode_dialog_state(section.data);
    }
    for(const auto& section:archive.sections){
        if(section.id==house_layout_id){if(!section.required)throw FormatError("House layout history must be required");
            result.house_layout_history=decode_house_layout_section(section.data,result);}
        if(section.id==character_family_id){if(!section.required)throw FormatError("current family carrier must be required");result.character_family=decode_character_family(section.data);}
        if(section.id==family_vacation_id){if(!section.required)throw FormatError("vacation carrier must be required");result.family_vacation=decode_family_vacation_snapshot(section.data);}
        if(section.id==family_spells_id){if(!section.required)throw FormatError("family spell carrier must be required");result.family_spells=decode_family_spells_snapshot(section.data);}
        if(section.id==personal_state_id){if(!section.required)throw FormatError("personal state must be required");result.personal_state=decode_native_personal_snapshot(section.data);}
        if(section.id==type_attributes_id){if(!section.required)throw FormatError("type attributes must be required");result.type_attributes=decode_type_attributes_snapshot(section.data);}
        if(section.id==object_groups_id){if(!section.required)throw FormatError("object groups must be required");
            auto group_limits=limits.object_groups;group_limits.payload_bytes=std::min<std::uint64_t>(group_limits.payload_bytes,limits.archive.max_section_bytes);
            result.object_groups=decode_object_group_snapshot(section.data,group_limits);}
        if(section.id==build_buy_id){if(!section.required)throw FormatError("Build/Buy permission must be required");result.build_buy_permission=decode_build_buy_permission(section.data);}
        if(section.id==inventory_id){if(!section.required)throw FormatError("neighbor inventory must be required");result.neighbor_inventory=decode_neighbor_inventory(section.data);}
        if(section.id==travel_id){if(!section.required)throw FormatError("travel state must be required");result.travel=decode_ts1_travel_state(section.data);}
        if(section.id==visited_id){if(!section.required)throw FormatError("visited session must be required");result.travel_visited_session=section.data;}
        if(section.id==home_id){if(!section.required)throw FormatError("home session must be required");result.travel_home_session=section.data;}
    }
    validate_ts1_travel_bundle(result,limits);
    validate_links(result,limits);
    validate_simulation(result);
    validate_family_lease(result);
    validate_architecture_object_links(result);
    validate_ui_state(result);
    return result;
}

SessionBundleLimits actorless_object_trade_limits() {
    SessionBundleLimits limits;limits.archive={16u*1024u*1024u,8u*1024u*1024u,16};
    limits.world.max_bytes=8u*1024u*1024u;return limits;
}
SessionBundle actorless_object_trade_components(const ActorlessObjectTradeState& state) {
    SessionBundle b;b.resources=state.resources;b.world=state.world;b.relationships=state.relationships;
    b.host.random_seed=state.random_seed;b.host.tick=state.tick;b.runtime_globals=state.globals;
    if(state.clock)b.simulation=SessionSimulationState{*state.clock,{}};
    b.scheduler=state.scheduler;b.object_threads=state.object_threads;b.generic_execution=true;
    b.next_code_owner=state.next_code_owner;b.code_owner_ids_exhausted=state.code_owner_ids_exhausted;
    b.created_objects=state.created_objects;b.removed_original_objects=state.removed_original_objects;
    b.object_history=state.history;b.object_trade=state.trade;b.architecture_object_support=state.object_support;
    b.architecture_object_state=state.object_state;b.build_buy_permission=state.build_buy_permission;
    b.object_groups=state.object_groups;return b;
}
ActorlessObjectTradeState actorless_object_trade_from_components(const SessionBundle& b) {
    if(b.host.neighborhood_mode||b.host.house_number||b.host.family_chunk_id||b.host.family_number||
       b.npc_primary||b.host.resident_guid||b.host.resident_neighbor_id||b.host.runtime_avatar_id||b.host.family_budget||b.family_lease||
       !b.host.global_values.empty()||!b.household_roster.empty()||!b.runtime_entities.empty()||!b.npc_avatars.empty()||
       b.travel||b.neighbor_inventory||!b.deferred_removals.empty()||b.dialog||!b.headlines.empty()||
       b.seated_continuation||!b.household_seated_continuations.empty()||b.family_spells||b.type_attributes||b.personal_state||b.family_vacation||b.character_family||b.house_layout_history||
       b.architecture||b.ui_state||b.travel_visited_session||b.travel_home_session||
       !b.runtime_globals||!b.object_history||!b.object_trade||!b.generic_execution||
       (b.simulation&&!b.simulation->avatars.empty()))
        throw FormatError("actorless object state contains household authority or unrepresented services");
    ActorlessObjectTradeState state;state.resources=b.resources;state.world=b.world;state.relationships=b.relationships;
    state.random_seed=b.host.random_seed;state.tick=b.host.tick;state.globals=*b.runtime_globals;
    if(b.simulation)state.clock=b.simulation->clock;
    state.scheduler=b.scheduler;state.object_threads=b.object_threads;state.next_code_owner=b.next_code_owner;
    state.code_owner_ids_exhausted=b.code_owner_ids_exhausted;state.created_objects=b.created_objects;
    state.removed_original_objects=b.removed_original_objects;state.history=*b.object_history;state.trade=*b.object_trade;
    state.object_support=b.architecture_object_support;state.object_state=b.architecture_object_state;
    state.build_buy_permission=b.build_buy_permission;state.object_groups=b.object_groups;return state;
}
void validate_actorless_object_trade_state(const ActorlessObjectTradeState& state,SessionBundleLimits limits) {
    const auto b=actorless_object_trade_components(state);
    (void)encode_session_resource_identity(state.resources);
    if(!state.history.complete||state.globals.words.size()<=32||state.globals.words[32]!=1)
        throw FormatError("actorless object state requires complete history and actual simless global bank");
    World world(limits.world.world,state.world.attribute_policy);
    RelationshipState relations(state.relationships.limits);
    if(!world.restore_snapshot(state.world)||!relations.restore_snapshot(state.relationships)||
       !world.install_reserved_entity_ids(state.history.reserved_entity_ids))
        throw FormatError("actorless object World/relationship/reservation state invalid");
    (void)encode_world_snapshot(state.world,limits.world);
    (void)encode_relationship_snapshot(state.relationships,limits.relationships);
    (void)encode_entity_vm_scheduler_snapshot(state.scheduler,limits.scheduler);
    if(state.scheduler.faulted||state.scheduler.entities.size()!=state.object_threads.size()||
       state.object_threads.size()>limits.max_object_threads)
        throw FormatError("actorless object scheduler must exactly cover its frozen object Threads");
    validate_runtime_globals(b);validate_generic_execution(b,limits);validate_object_history(b);
    validate_object_trade(b);validate_object_group_links(b,limits);validate_architecture_object_links(b);
    if(state.clock){Ts1Clock clock;if(!clock.restore(*state.clock)||state.clock->ticks!=state.tick)
        throw FormatError("actorless source clock invalid or differs from frozen tick");}
    std::int16_t previous=0;
    for(const auto& thread:state.object_threads){
        if(thread.id<=previous)throw FormatError("actorless object Threads are unordered or duplicate");
        const auto* entity=world.find(thread.id);
        const auto scheduled=std::find_if(state.scheduler.entities.begin(),state.scheduler.entities.end(),
            [&](const auto& row){return row.id==thread.id;});
        if(!entity||scheduled==state.scheduler.entities.end())throw FormatError("actorless object Thread lacks World/scheduler identity");
        validate_object_thread_links(thread,*entity,*scheduled,b.host);
        (void)encode_vm_snapshot(thread.vm,limits.vm);previous=thread.id;
    }
}
std::vector<std::uint8_t> encode_actorless_object_trade_state(const ActorlessObjectTradeState& state,SessionBundleLimits limits) {
    validate_actorless_object_trade_state(state,limits);const auto b=actorless_object_trade_components(state);
    Writer marker(limits.archive.max_section_bytes);marker.u16(1);marker.u64(state.tick);marker.u64(state.random_seed);
    SessionArchive a;a.sections={
        {{{'A','E','D','T'}},true,marker.take()},
        {meta_id,true,encode_session_resource_identity(state.resources)},
        {world_id,true,encode_world_snapshot(state.world,limits.world)},
        {relation_id,true,encode_relationship_snapshot(state.relationships,limits.relationships)},
        {scheduler_id,true,encode_entity_vm_scheduler_snapshot(state.scheduler,limits.scheduler)},
        {runtime_globals_id,true,encode_runtime_globals(state.globals,limits)},
        {generic_execution_id,true,encode_generic_execution(b,limits)},
        {object_history_id,true,encode_object_history(state.history,limits)},
        {object_trade_id,true,encode_object_trade(state.trade,limits)}};
    if(state.object_groups){auto group_limits=limits.object_groups;
        group_limits.payload_bytes=std::min<std::uint64_t>(group_limits.payload_bytes,limits.archive.max_section_bytes);
        a.sections.push_back({object_groups_id,true,encode_object_group_snapshot(*state.object_groups,group_limits)});}
    if(!state.object_threads.empty())a.sections.push_back({object_vm_id,true,encode_object_threads(state.object_threads,limits)});
    if(b.simulation)a.sections.push_back({simulation_id,true,encode_simulation(*b.simulation,limits.archive.max_section_bytes)});
    if(state.object_support){
        validate_architecture_object_support(*state.object_support);
        Writer w(limits.archive.max_section_bytes);w.u16(1);w.u32(state.object_support->width);w.u32(state.object_support->height);
        for(const auto& level:state.object_support->levels)w.append(level);
        a.sections.push_back({architecture_support_id,true,w.take()});
    }
    if(state.object_state)a.sections.push_back({architecture_objects_id,true,encode_architecture_object_state(*state.object_state)});
    if(state.build_buy_permission)a.sections.push_back({build_buy_id,true,encode_build_buy_permission(*state.build_buy_permission)});
    return encode_session_archive(a,limits.archive);
}
ActorlessObjectTradeState decode_actorless_object_trade_state(const std::vector<std::uint8_t>& bytes,
        const SessionResourceIdentity* expected,SessionBundleLimits limits) {
    const auto a=decode_session_archive(bytes,limits.archive);if(a.version!=1)throw FormatError("unsupported actorless archive version");
    const std::set<std::array<char,4>> allowed{{'A','E','D','T'},meta_id,world_id,relation_id,scheduler_id,
        runtime_globals_id,generic_execution_id,object_history_id,object_trade_id,object_vm_id,simulation_id,
        architecture_support_id,architecture_objects_id,build_buy_id,object_groups_id};
    std::set<std::array<char,4>> seen;
    for(const auto& section:a.sections)if(!section.required||!allowed.count(section.id)||!seen.insert(section.id).second)
        throw FormatError("actorless archive has unknown, optional or duplicate section");
    SessionBundle b;Reader marker(required_section(a,{'A','E','D','T'}).data);
    if(marker.u16()!=1)throw FormatError("unsupported actorless state version");
    b.host.tick=marker.u64();b.host.random_seed=marker.u64();if(!marker.done())throw FormatError("actorless marker trailing data");
    b.resources=decode_session_resource_identity(required_section(a,meta_id).data);
    if(expected&&encode_session_resource_identity(*expected)!=encode_session_resource_identity(b.resources))
        throw FormatError("actorless resource identity differs from selected game");
    b.world=decode_world_snapshot(required_section(a,world_id).data,limits.world);
    b.relationships=decode_relationship_snapshot(required_section(a,relation_id).data,limits.relationships);
    b.scheduler=decode_entity_vm_scheduler_snapshot(required_section(a,scheduler_id).data,limits.scheduler);
    b.runtime_globals=decode_runtime_globals(required_section(a,runtime_globals_id).data);
    if(seen.count(object_groups_id)){auto group_limits=limits.object_groups;
        group_limits.payload_bytes=std::min<std::uint64_t>(group_limits.payload_bytes,limits.archive.max_section_bytes);
        b.object_groups=decode_object_group_snapshot(required_section(a,object_groups_id).data,group_limits);}
    if(seen.count(object_vm_id))b.object_threads=decode_object_threads(required_section(a,object_vm_id).data,limits);
    decode_generic_execution(required_section(a,generic_execution_id).data,b,limits);
    b.object_history=decode_object_history(required_section(a,object_history_id).data);
    b.object_trade=decode_object_trade(required_section(a,object_trade_id).data);
    if(seen.count(simulation_id))b.simulation=decode_simulation(required_section(a,simulation_id).data,0);
    if(seen.count(architecture_support_id)){
        Reader r(required_section(a,architecture_support_id).data);if(r.u16()!=1)throw FormatError("unsupported actorless support version");
        ArchitectureObjectSupport support;support.width=r.u32();support.height=r.u32();
        if(!support.width||support.width>64||!support.height||support.height>64||r.remaining()!=std::size_t(support.width)*support.height*2)
            throw FormatError("actorless support extent invalid");
        for(auto& level:support.levels)level=r.bytes(std::size_t(support.width)*support.height);
        validate_architecture_object_support(support);b.architecture_object_support=std::move(support);
    }
    if(seen.count(architecture_objects_id))b.architecture_object_state=decode_architecture_object_state(required_section(a,architecture_objects_id).data);
    if(seen.count(build_buy_id))b.build_buy_permission=decode_build_buy_permission(required_section(a,build_buy_id).data);
    auto state=actorless_object_trade_from_components(b);validate_actorless_object_trade_state(state,limits);return state;
}

} // namespace odh

namespace odh {
void project_session_character_family(SessionBundle& target,const SessionCharacterFamily& source){
 validate_ts1_character_family_snapshot(source.shared);
 if(!target.family_lease||target.family_lease->neighborhood_identity!=source.shared.neighborhood_identity)
    throw FormatError("character/family transfer lacks the target's exact managed neighborhood");
 if(target.character_family){
    validate_ts1_character_family_successor(target.character_family->shared,source.shared);
    target.character_family->shared=source.shared;
 }else{
    SessionCharacterFamily local;local.shared=source.shared;
    for(const auto& member:target.household_roster)local.runtime_subset.push_back(member.guid);
    target.character_family=std::move(local);
 }
}
}
