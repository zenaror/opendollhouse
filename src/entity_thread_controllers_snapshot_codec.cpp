#include "odh/entity_thread_controllers_snapshot_codec.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 14;
constexpr std::uint16_t legacy_codec_version = 1;
constexpr std::size_t minimum_size = 84;
constexpr std::size_t minimum_action_size_v1 = 34;
constexpr std::size_t minimum_action_size_v2 = 44;
constexpr std::size_t minimum_action_size_v3 = 45;
constexpr std::size_t minimum_action_size_v4 = 46;
constexpr auto maximum_clock = std::numeric_limits<std::uint32_t>::max();
static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t), "controller codec requires size_t no wider than 64 bits");

class Writer {
public:
    explicit Writer(std::size_t limit) : limit_(limit) {}
    void u8(std::uint8_t value) { push(value); }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void u16(std::uint16_t value) {
        push(static_cast<std::uint8_t>(value));
        push(static_cast<std::uint8_t>(value >> 8));
    }
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            push(static_cast<std::uint8_t>(value >> shift));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            push(static_cast<std::uint8_t>(value >> shift));
    }
    void count(std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max())
            throw FormatError("controller count exceeds file format");
        u32(static_cast<std::uint32_t>(value));
    }
    void string(const std::string& value) {
        count(value.size());
        for (const unsigned char byte : value) push(byte);
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    void push(std::uint8_t value) {
        if (bytes_.size() >= limit_) throw FormatError("controller snapshot exceeds byte limit");
        bytes_.push_back(value);
    }
    std::size_t limit_;
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}
    std::uint8_t u8() { require(1); return bytes_[offset_++]; }
    bool boolean() {
        const auto value = u8();
        if (value > 1) throw FormatError("invalid controller snapshot boolean");
        return value != 0;
    }
    std::uint16_t u16() {
        const auto low = static_cast<std::uint16_t>(u8());
        const auto high = static_cast<std::uint16_t>(u8());
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8));
    }
    std::int16_t i16() {
        const auto bits = u16();
        const auto value = bits <= static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max())
            ? static_cast<std::int32_t>(bits)
            : static_cast<std::int32_t>(bits) - 65536;
        return static_cast<std::int16_t>(value);
    }
    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(u8()) << shift;
        return value;
    }
    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(u8()) << shift;
        return value;
    }
    std::size_t size(std::size_t maximum) {
        const auto value = u64();
        if (value > maximum || value > std::numeric_limits<std::size_t>::max())
            throw FormatError("controller size exceeds configured limit");
        return static_cast<std::size_t>(value);
    }
    std::uint32_t count(std::size_t maximum, std::size_t minimum_bytes) {
        const auto value = u32();
        if (value > maximum || (minimum_bytes && value > remaining() / minimum_bytes))
            throw FormatError("controller count exceeds limit or remaining data");
        return value;
    }
    std::string string(std::size_t maximum) {
        const auto length = count(maximum, 1);
        require(length);
        std::string value;
        value.resize(length);
        for (std::size_t i = 0; i < length; ++i)
            value[i] = static_cast<char>(bytes_[offset_++]);
        return value;
    }
    std::size_t remaining() const { return bytes_.size() - offset_; }
    bool done() const { return offset_ == bytes_.size(); }
private:
    void require(std::size_t count) {
        if (count > remaining()) throw FormatError("controller snapshot is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

bool valid_key(RoutineKey key) {
    return routine_key_valid(key);
}
void write_key(Writer& writer, RoutineKey key) {
    if (!valid_key(key)) throw FormatError("invalid queued interaction routine key");
    writer.u8(static_cast<std::uint8_t>(key.scope));
    writer.u16(key.id);
}
RoutineKey read_key(Reader& reader,std::uint16_t version) {
    const auto scope = reader.u8();
    const auto id = reader.u16();
    if (scope > static_cast<std::uint8_t>(version>=13?RoutineScope::named_semiglobal:RoutineScope::semiglobal))
        throw FormatError("invalid queued interaction routine scope");
    const RoutineKey key{static_cast<RoutineScope>(scope), id};
    if (!valid_key(key)) throw FormatError("queued interaction routine key does not match its ID");
    return key;
}
bool valid_context_references(const VmContext& value){
    if(!value.entity_references_bound)return !value.caller_generation&&!value.callee_generation&&!value.stack_object_generation;
    return (value.caller>0||!value.caller_generation)&&(value.callee>0||!value.callee_generation)&&
        (value.stack_object>0||!value.stack_object_generation);
}
void write_context(Writer& writer, const VmContext& context,std::uint16_t version) {
    if(!valid_context_references(context))throw FormatError("invalid queued entity reference tags");
    writer.i16(context.caller);
    writer.i16(context.callee);
    writer.i16(context.stack_object);
    writer.u32(context.code_owner);
    if(version>=12){writer.boolean(context.entity_references_bound);writer.u64(context.caller_generation);
        writer.u64(context.callee_generation);writer.u64(context.stack_object_generation);}
}
VmContext read_context(Reader& reader,std::uint16_t version) {
    VmContext value{reader.i16(), reader.i16(), reader.i16(), reader.u32()};
    if(version>=12){value.entity_references_bound=reader.boolean();value.caller_generation=reader.u64();
        value.callee_generation=reader.u64();value.stack_object_generation=reader.u64();}
    if(!valid_context_references(value))throw FormatError("invalid queued entity reference tags");
    return value;
}
void write_call(Writer& writer, const VmCall& call,std::uint16_t version) {
    if(call.exact_arguments&&version<10)throw FormatError("queued interaction exact extent requires controller codec10");
    write_key(writer, call.key);
    write_context(writer, call.context,version);
    writer.count(call.arguments.size());
    for (const auto value : call.arguments) writer.i16(value);
    if (call.return_mode > (version>=7?VmCallReturnMode::immediate:VmCallReturnMode::external_frame_next_tick))
        throw FormatError("invalid queued interaction return mode");
    writer.u8(static_cast<std::uint8_t>(call.return_mode));
    if(version>=10)writer.boolean(call.exact_arguments);
    if(version>=12){writer.boolean(call.action_tree_override.has_value());if(call.action_tree_override)writer.boolean(*call.action_tree_override);}
}
VmCall read_call(Reader& reader, const EntityThreadControllersSnapshotCodecLimits& limits,
                 std::uint16_t version) {
    VmCall call;
    call.key = read_key(reader,version);
    call.context = read_context(reader,version);
    const auto count = reader.count(limits.max_call_arguments, 2);
    call.arguments.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) call.arguments.push_back(reader.i16());
    const auto mode = reader.u8();
    if (mode > static_cast<std::uint8_t>(version>=7?VmCallReturnMode::immediate:VmCallReturnMode::external_frame_next_tick))
        throw FormatError("invalid queued interaction return mode");
    call.return_mode = static_cast<VmCallReturnMode>(mode);
    if(version>=10)call.exact_arguments=reader.boolean();
    if(version>=12&&reader.boolean())call.action_tree_override=reader.boolean();
    return call;
}

void write_sleep(Writer& writer, const SleepSnapshot& state,std::uint16_t version) {
    writer.boolean(state.actor.has_value());
    if (state.actor) {
        writer.i16(state.actor->id);
        writer.u64(state.actor->generation);
        writer.u64(state.actor->run_generation);
    }
    writer.boolean(state.job.has_value());
    if (state.job) {
        writer.u64(state.job->ticket);
        writer.u64(state.job->idle_start);
        writer.u64(state.job->deadline);
        writer.u64(static_cast<std::uint64_t>(state.job->argument));
        if(version>=6) {
            writer.boolean(state.job->next_evaluation.has_value());
            if(state.job->next_evaluation)writer.u64(*state.job->next_evaluation);
        }
    }
    writer.boolean(state.last_tick.has_value());
    if (state.last_tick) writer.u64(*state.last_tick);
    writer.u64(state.clock);
    writer.boolean(state.interrupted);
    writer.boolean(state.notify_resume);
    writer.string(state.diagnostic);
}
SleepSnapshot read_sleep(Reader& reader, const EntityThreadControllersSnapshotCodecLimits& limits,std::uint16_t version) {
    SleepSnapshot state;
    if (reader.boolean()) state.actor = SleepActorSnapshot{reader.i16(), reader.u64(), reader.u64()};
    if (reader.boolean()) state.job = SleepJobSnapshot{reader.u64(), reader.u64(), reader.u64(),
                                                       reader.size(32767)};
    if(state.job && version>=6 && reader.boolean())state.job->next_evaluation=reader.u64();
    if (reader.boolean()) state.last_tick = reader.u64();
    state.clock = reader.u64();
    state.interrupted = reader.boolean();
    state.notify_resume = reader.boolean();
    state.diagnostic = reader.string(limits.max_diagnostic_bytes);
    return state;
}

void write_interactions(Writer& writer, const InteractionQueueSnapshot& state,std::uint16_t version) {
    writer.i16(state.actor_id);
    writer.u64(static_cast<std::uint64_t>(state.capacity));
    writer.u64(state.actor_generation);
    writer.u64(state.run_generation);
    writer.u64(state.sequence);
    writer.u64(state.clock);
    writer.u64(state.idle_start);
    writer.boolean(state.last_tick.has_value());
    if (state.last_tick) writer.u64(*state.last_tick);
    writer.boolean(state.resume_tick.has_value());
    if (state.resume_tick) writer.u64(*state.resume_tick);
    writer.count(state.actions.size());
    for (const auto& action : state.actions) {
        writer.u64(action.uid);
        writer.u64(action.target_generation);
        write_call(writer, action.call,version);
        writer.u16(action.queue_uid);
        writer.i16(action.priority);
        if(action.mode>InteractionQueueMode::idle)
            throw FormatError("invalid interaction queue mode");
        writer.u8(static_cast<std::uint8_t>(action.mode));
        writer.u32(action.flags);
        writer.boolean(action.notify_idle);
        writer.boolean(action.check.has_value());
        if(action.check)write_key(writer,*action.check);
        writer.boolean(action.check_permissions);
        if(version>=8){
            writer.boolean(action.icon_owner.has_value());
            if(action.icon_owner){writer.i16(action.icon_owner->id);writer.u64(action.icon_owner->generation);}
        }
        if(version>=9){
            writer.boolean(action.interaction_number.has_value());
            if(action.interaction_number)writer.u32(static_cast<std::uint32_t>(*action.interaction_number));
        }
        if(version>=10)writer.u8(static_cast<std::uint8_t>(action.argument_mode));
        if(version>=11)writer.u8(static_cast<std::uint8_t>(action.check_context));
        if(version>=14){
            writer.boolean(action.label.has_value());
            if(action.label){
                const auto& origin=action.label->origin;
                writer.u8(static_cast<std::uint8_t>(origin.kind));writer.u8(origin.language);
                writer.u16(origin.table);writer.u32(origin.index);
                writer.string(origin.resource);writer.string(origin.asset);writer.string(action.label->text);
            }
        }
    }
    writer.boolean(state.active.has_value());
    if (state.active) { writer.u64(state.active->uid); writer.u64(state.active->ticket); }
    writer.count(state.suspended_active.size());
    for(const auto& parent:state.suspended_active){writer.u64(parent.uid);writer.u64(parent.ticket);}
    writer.boolean(state.wait.has_value());
    if (state.wait) { writer.u64(state.wait->ticket); writer.u64(state.wait->deadline); }
    writer.boolean(state.completion.has_value());
    if (state.completion) {
        writer.u64(state.completion->uid);
        writer.u64(state.completion->ticket);
        writer.u64(state.completion->tick);
        writer.boolean(state.completion->result);
    }
    writer.boolean(state.interrupted);
    writer.string(state.diagnostic);
    if(state.profile>InteractionQueueProfile::ts1_priority)
        throw FormatError("invalid interaction queue profile");
    writer.u8(static_cast<std::uint8_t>(state.profile));
    writer.u16(state.next_queue_uid);
}
InteractionQueueSnapshot read_interactions(Reader& reader,
        const EntityThreadControllersSnapshotCodecLimits& limits,std::uint16_t version) {
    InteractionQueueSnapshot state{};
    state.actor_id = reader.i16();
    state.capacity = reader.size(limits.max_queue_capacity);
    state.actor_generation = reader.u64();
    state.run_generation = reader.u64();
    state.sequence = reader.u64();
    state.clock = reader.u64();
    state.idle_start = reader.u64();
    if (reader.boolean()) state.last_tick = reader.u64();
    if (reader.boolean()) state.resume_tick = reader.u64();
    const auto count = reader.count(limits.max_queue_actions,
        version>=11?minimum_action_size_v4+5:version>=10?minimum_action_size_v4+4:version>=9?minimum_action_size_v4+2:version>=8?minimum_action_size_v4+1:version>=4?minimum_action_size_v4:version>=3?minimum_action_size_v3:
            version>=2?minimum_action_size_v2:minimum_action_size_v1);
    state.actions.resize(count);
    for (auto& action : state.actions) {
        action.uid = reader.u64();
        action.target_generation = reader.u64();
        action.call = read_call(reader, limits,version);
        if(version>=2){
            action.queue_uid=reader.u16();
            action.priority=reader.i16();
            const auto mode=reader.u8();
            if(mode>static_cast<std::uint8_t>(InteractionQueueMode::idle))
                throw FormatError("invalid interaction queue mode");
            action.mode=static_cast<InteractionQueueMode>(mode);
            action.flags=reader.u32();
            action.notify_idle=reader.boolean();
        }
        if(version>=3&&reader.boolean())action.check=read_key(reader,version);
        if(version>=4)action.check_permissions=reader.boolean();
        if(version>=8&&reader.boolean())action.icon_owner=InteractionIconOwnerSnapshot{reader.i16(),reader.u64()};
        if(version>=9&&reader.boolean()){
            const auto bits=reader.u32();action.interaction_number=static_cast<std::int32_t>(
                bits<=INT32_MAX?static_cast<std::int64_t>(bits):static_cast<std::int64_t>(bits)-4294967296LL);
        }
        if(version>=10){const auto mode=reader.u8();
            if(mode>static_cast<std::uint8_t>(InteractionArgumentMode::source_explicit))throw FormatError("invalid queued source argument mode");
            action.argument_mode=static_cast<InteractionArgumentMode>(mode);}
        if(version>=11){const auto context=reader.u8();
            if(context>static_cast<std::uint8_t>(InteractionCheckContext::autonomous))throw FormatError("invalid interaction eligibility context");
            action.check_context=static_cast<InteractionCheckContext>(context);}
        if(version>=14&&reader.boolean()){
            InteractionLabel label;
            label.origin.kind=static_cast<InteractionLabelKind>(reader.u8());label.origin.language=reader.u8();
            label.origin.table=reader.u16();label.origin.index=reader.u32();
            label.origin.resource=reader.string(4096);label.origin.asset=reader.string(4096);label.text=reader.string(2048);
            if(!valid_interaction_label(label))throw FormatError("invalid saved interaction label");
            action.label=std::move(label);
        }
    }
    if (reader.boolean()) state.active = InteractionActiveSnapshot{reader.u64(), reader.u64()};
    if(version>=5){
        const auto parents=reader.count(limits.max_queue_actions,16);
        state.suspended_active.resize(parents);
        for(auto& parent:state.suspended_active)parent={reader.u64(),reader.u64()};
    }
    if (reader.boolean()) state.wait = InteractionWaitSnapshot{reader.u64(), reader.u64()};
    if (reader.boolean()) state.completion = InteractionCompletion{reader.u64(), reader.u64(), reader.u64(), reader.boolean()};
    state.interrupted = reader.boolean();
    state.diagnostic = reader.string(limits.max_diagnostic_bytes);
    if(version>=2){
        const auto profile=reader.u8();
        if(profile>static_cast<std::uint8_t>(InteractionQueueProfile::ts1_priority))
            throw FormatError("invalid interaction queue profile");
        state.profile=static_cast<InteractionQueueProfile>(profile);
        state.next_queue_uid=reader.u16();
    }else state.profile=InteractionQueueProfile::legacy_fifo;
    return state;
}

void validate(const EntityThreadControllersSnapshot& snapshot,
              const EntityThreadControllersSnapshotCodecLimits& limits) {
    const auto& sleep = snapshot.sleep;
    const auto& interactions = snapshot.interactions;
    if (sleep.diagnostic.size() > limits.max_diagnostic_bytes ||
        interactions.diagnostic.size() > limits.max_diagnostic_bytes ||
        sleep.clock > maximum_clock ||
        (sleep.last_tick ? *sleep.last_tick != sleep.clock : sleep.clock != 0))
        throw FormatError("sleep snapshot exceeds limits or has inconsistent clock");
    if (!sleep.actor && (sleep.job || sleep.interrupted || sleep.notify_resume))
        throw FormatError("sleep state without an actor");
    if (sleep.job && (!sleep.actor || !sleep.job->ticket || sleep.notify_resume ||
        sleep.job->idle_start > sleep.clock || sleep.job->deadline <= sleep.clock ||
        sleep.job->deadline > maximum_clock || sleep.job->argument > 32767))
        throw FormatError("invalid sleep job state");
    if(sleep.job && sleep.job->next_evaluation &&
       (*sleep.job->next_evaluation<=sleep.clock || *sleep.job->next_evaluation>=sleep.job->deadline ||
        *sleep.job->next_evaluation!=sleep.job->idle_start+1))
        throw FormatError("invalid sleep scheduled evaluation");
    if (sleep.notify_resume && !sleep.actor)
        throw FormatError("sleep resume state without an actor");

    if (!interactions.capacity || interactions.capacity > limits.max_queue_capacity ||
        interactions.actions.size() > interactions.capacity ||
        interactions.actions.size() > limits.max_queue_actions ||
        interactions.clock > maximum_clock || interactions.idle_start > interactions.clock ||
        (interactions.last_tick ? *interactions.last_tick != interactions.clock : interactions.clock != 0))
        throw FormatError("interaction queue exceeds limits or has inconsistent clock");
    if(snapshot.schedule_idle_start&&(*snapshot.schedule_idle_start>maximum_clock||
       *snapshot.schedule_idle_start>sleep.clock||*snapshot.schedule_idle_start>interactions.clock||
       sleep.clock!=interactions.clock))
        throw FormatError("shared ScheduleIdleStart exceeds or disagrees with controller clocks");
    std::set<std::uint64_t> action_uids;
    for (const auto& action : interactions.actions) {
        if(!valid_context_references(action.call.context)||(action.call.action_tree_override&&!*action.call.action_tree_override)||
           (action.call.context.entity_references_bound&&(action.call.context.caller_generation!=interactions.actor_generation||
            action.call.context.callee_generation!=action.target_generation||action.call.context.stack_object_generation!=action.target_generation)))
            throw FormatError("queued action has invalid body references or ActionTree intent");
        if((action.label&&!valid_interaction_label(*action.label))||action.check_context>InteractionCheckContext::autonomous||
           (action.check_context==InteractionCheckContext::autonomous&&
            (action.argument_mode!=InteractionArgumentMode::source_explicit||!action.check_permissions||action.call.arguments.size()!=4))||
           action.argument_mode>InteractionArgumentMode::source_explicit||
           action.call.exact_arguments!=(action.argument_mode!=InteractionArgumentMode::legacy_metadata)||
           (action.argument_mode==InteractionArgumentMode::source_null&&
            (action.call.arguments.size()!=4||std::any_of(action.call.arguments.begin(),action.call.arguments.end(),[](auto value){return value!=0;})))||
           (action.icon_owner&&(action.icon_owner->id<=0||!action.icon_owner->generation)))
            throw FormatError("invalid interaction source argument extent or weak icon identity");
        if (!action.uid || !action_uids.insert(action.uid).second || action.uid > interactions.sequence ||
            !valid_key(action.call.key) || action.call.arguments.size() > limits.max_call_arguments ||
            action.call.context.caller != interactions.actor_id ||
            action.call.context.stack_object != action.call.context.callee ||
            action.call.return_mode != ((action.flags&4U)?VmCallReturnMode::immediate:
                                                        VmCallReturnMode::interaction_next_tick) ||
            action.mode > InteractionQueueMode::idle ||
            (action.check&&!valid_key(*action.check)))
            throw FormatError("invalid queued interaction action");
    }
    if(interactions.profile>InteractionQueueProfile::ts1_priority)
        throw FormatError("invalid interaction queue profile");
    const auto valid_ticket = [](std::uint64_t ticket) { return ticket != 0; };
    if(interactions.suspended_active.size()>interactions.actions.size()||
       (!interactions.suspended_active.empty()&&!interactions.active)||
       (interactions.profile==InteractionQueueProfile::legacy_fifo&&!interactions.suspended_active.empty()))
        throw FormatError("invalid nested interaction prefix");
    std::uint64_t previous_ticket=0;
    for(std::size_t i=0;i<interactions.suspended_active.size();++i){
        const auto& parent=interactions.suspended_active[i];
        if(!valid_ticket(parent.ticket)||parent.ticket<=previous_ticket||
           parent.uid!=interactions.actions[i].uid)
            throw FormatError("invalid suspended interaction parent prefix");
        previous_ticket=parent.ticket;
    }
    if (interactions.active && (interactions.actions.size()<=interactions.suspended_active.size()||
        interactions.active->uid != interactions.actions[interactions.suspended_active.size()].uid ||
        !valid_ticket(interactions.active->ticket)||interactions.active->ticket<=previous_ticket))
        throw FormatError("invalid active interaction state");
    if (interactions.wait && (interactions.resume_tick ||
        !valid_ticket(interactions.wait->ticket) || interactions.wait->deadline <= interactions.clock ||
        interactions.wait->deadline > maximum_clock))
        throw FormatError("invalid interaction wait state");
    if (interactions.completion && (!interactions.completion->uid ||
        interactions.completion->uid > interactions.sequence || !valid_ticket(interactions.completion->ticket) ||
        interactions.completion->tick > interactions.clock ||
        action_uids.count(interactions.completion->uid)))
        throw FormatError("invalid interaction completion state");
    if (interactions.resume_tick && (!interactions.completion || interactions.wait ||
        *interactions.resume_tick > maximum_clock || *interactions.resume_tick <= interactions.clock ||
        *interactions.resume_tick != interactions.completion->tick + 1 ||
        (!interactions.active&&!interactions.suspended_active.empty())||
        (interactions.active&&interactions.completion->ticket<=interactions.active->ticket)))
        throw FormatError("invalid interaction resume boundary");
}
bool has_v7_state(const EntityThreadControllersSnapshot& snapshot) {
    return snapshot.schedule_idle_start.has_value()||
        std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),[](const auto& action){
            return action.call.return_mode==VmCallReturnMode::immediate||(action.flags&4U)!=0;
        });
}
bool has_v13_state(const EntityThreadControllersSnapshot& snapshot){
    return std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),[](const auto& action){
        return action.call.key.scope>RoutineScope::semiglobal||(action.check&&action.check->scope>RoutineScope::semiglobal);
    });
}
bool has_v12_state(const EntityThreadControllersSnapshot& snapshot) {
    return std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.call.context.entity_references_bound||action.call.action_tree_override.has_value();});
}
bool has_v11_state(const EntityThreadControllersSnapshot& snapshot) {
    return std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.check_context!=InteractionCheckContext::default_zero;});
}
bool has_v10_state(const EntityThreadControllersSnapshot& snapshot) {
    return std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.argument_mode!=InteractionArgumentMode::legacy_metadata;});
}
bool has_v9_state(const EntityThreadControllersSnapshot& snapshot) {
    return std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.interaction_number.has_value();});
}
bool has_v8_state(const EntityThreadControllersSnapshot& snapshot) {
    return std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.icon_owner.has_value();});
}
}

std::vector<std::uint8_t> encode_entity_thread_controllers_snapshot(
        const EntityThreadControllersSnapshot& snapshot,
        EntityThreadControllersSnapshotCodecLimits limits) {
    if (limits.max_bytes < minimum_size)
        throw FormatError("controller snapshot byte limit is too small");
    validate(snapshot, limits);
    Writer writer(limits.max_bytes);
    const bool labels=std::any_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.label.has_value();});
    const auto version=static_cast<std::uint16_t>(labels?14:has_v13_state(snapshot)?13:has_v12_state(snapshot)?12:has_v11_state(snapshot)?11:has_v10_state(snapshot)?10:has_v9_state(snapshot)?9:has_v8_state(snapshot)?8:has_v7_state(snapshot)?7:
        snapshot.sleep.job&&snapshot.sleep.job->next_evaluation?6:5);
    writer.u16(version);
    write_sleep(writer, snapshot.sleep,version);
    write_interactions(writer, snapshot.interactions,version);
    writer.boolean(snapshot.thread_interrupt);
    if(version>=7) {
        writer.boolean(snapshot.schedule_idle_start.has_value());
        if(snapshot.schedule_idle_start)writer.u64(*snapshot.schedule_idle_start);
    }
    return writer.take();
}

EntityThreadControllersSnapshot decode_entity_thread_controllers_snapshot(
        const std::vector<std::uint8_t>& bytes,
        EntityThreadControllersSnapshotCodecLimits limits) {
    if (limits.max_bytes < minimum_size || bytes.size() > limits.max_bytes)
        throw FormatError("controller snapshot exceeds byte limit");
    if (bytes.size() < minimum_size) throw FormatError("controller snapshot header is truncated");
    Reader reader(bytes);
    const auto version=reader.u16();
    if(version<legacy_codec_version||version>codec_version)
        throw FormatError("unsupported controller snapshot version");
    EntityThreadControllersSnapshot snapshot;
    snapshot.sleep = read_sleep(reader, limits,version);
    snapshot.interactions = read_interactions(reader, limits,version);
    snapshot.thread_interrupt = reader.boolean();
    if(version>=7&&reader.boolean())snapshot.schedule_idle_start=reader.u64();
    if (!reader.done()) throw FormatError("controller snapshot has unclaimed bytes");
    validate(snapshot, limits);
    if(version==14&&std::none_of(snapshot.interactions.actions.begin(),snapshot.interactions.actions.end(),
        [](const auto& action){return action.label.has_value();}))throw FormatError("noncanonical controller snapshot v14 label extension");
    if(version==13&&!has_v13_state(snapshot))throw FormatError("noncanonical controller snapshot v13 named-key extension");
    if(version==12&&!has_v12_state(snapshot))throw FormatError("noncanonical controller snapshot v12 reference extension");
    if(version==11&&!has_v11_state(snapshot))throw FormatError("noncanonical controller snapshot v11 eligibility extension");
    if(version==10&&!has_v10_state(snapshot))throw FormatError("noncanonical controller snapshot v10 extension");
    if(version==9&&!has_v9_state(snapshot))throw FormatError("noncanonical controller snapshot v9 extension");
    if(version==8&&!has_v8_state(snapshot))throw FormatError("noncanonical controller snapshot v8 extension");
    if(version==7&&!has_v7_state(snapshot))
        throw FormatError("noncanonical controller snapshot v7 extension");
    return snapshot;
}

} // namespace odh
