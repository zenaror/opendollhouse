#include "odh/session_bundle.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace odh {
namespace {
constexpr std::array<char,4> meta_id{{'M','E','T','A'}};
constexpr std::array<char,4> host_id{{'H','O','S','T'}};
constexpr std::array<char,4> world_id{{'W','R','L','D'}};
constexpr std::array<char,4> relation_id{{'R','E','L','A'}};
constexpr std::array<char,4> vm_id{{'V','M',' ',' '}};
constexpr std::array<char,4> scheduler_id{{'S','C','H','D'}};
constexpr std::array<char,4> controller_id{{'C','T','R','L'}};
constexpr std::array<std::uint8_t,8> host_magic{{'O','D','H','H','S','T','\r','\n'}};
constexpr std::uint16_t payload_version = 1;

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
private:
    void require(std::size_t count) const {
        if(count>remaining())throw FormatError("session bundle section is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_=0;
};

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
                                      const SessionBundleLimits& limits) {
    validate_host(host,limits);
    Writer writer(limits.archive.max_section_bytes);
    writer.append(host_magic.data(),host_magic.size());
    writer.u16(payload_version);
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
    return writer.take();
}

SessionHostState decode_host(const std::vector<std::uint8_t>& bytes,
                             const SessionBundleLimits& limits) {
    Reader reader(bytes);
    if(reader.bytes(host_magic.size())!=std::vector<std::uint8_t>(host_magic.begin(),host_magic.end())||
       reader.u16()!=payload_version)
        throw FormatError("unsupported session host format");
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
    if(!reader.done())throw FormatError("session host has trailing data");
    validate_host(host,limits);
    return host;
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

void validate_links(const SessionBundle& bundle,const SessionBundleLimits& limits) {
    validate_host(bundle.host,limits);
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
        found_primary|=entity.id==bundle.host.runtime_avatar_id;
        previous=entity.id;
    }
    if(!found_primary)throw FormatError("session primary resident is absent from runtime roster");
}

const SessionArchiveSection& required_section(const SessionArchive& archive,
                                               const std::array<char,4>& id) {
    const auto it=std::find_if(archive.sections.begin(),archive.sections.end(),
        [&](const SessionArchiveSection& section){return section.id==id;});
    if(it==archive.sections.end()||!it->required)
        throw FormatError("session bundle is missing a required section");
    return *it;
}
bool recognized(const std::array<char,4>& id) {
    return id==meta_id||id==host_id||id==world_id||id==relation_id||
           id==vm_id||id==scheduler_id||id==controller_id;
}
}

std::vector<std::uint8_t> encode_session_bundle(const SessionBundle& bundle,
                                                 SessionBundleLimits limits) {
    validate_links(bundle,limits);
    SessionArchive archive;
    archive.sections={
        {meta_id,true,encode_session_resource_identity(bundle.resources)},
        {host_id,true,encode_host(bundle.host,limits)},
        {world_id,true,encode_world_snapshot(bundle.world,limits.world)},
        {relation_id,true,encode_relationship_snapshot(bundle.relationships,limits.relationships)},
        {vm_id,true,encode_roster(bundle.runtime_entities,limits,[&](const auto& entity){
            return encode_vm_snapshot(entity.vm,limits.vm);})},
        {scheduler_id,true,encode_entity_vm_scheduler_snapshot(bundle.scheduler,limits.scheduler)},
        {controller_id,true,encode_roster(bundle.runtime_entities,limits,[&](const auto& entity){
            return encode_entity_thread_controllers_snapshot(entity.controllers,limits.controllers);})}
    };
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
    result.host=decode_host(required_section(archive,host_id).data,limits);
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
    validate_links(result,limits);
    return result;
}

} // namespace odh
