#include "odh/entity_thread_controllers_snapshot_codec.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 1;
constexpr std::size_t minimum_size = 84;
constexpr std::size_t minimum_action_size = 34;
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
    return key.scope <= RoutineScope::semiglobal && key.scope == routine_key(key.id).scope;
}
void write_key(Writer& writer, RoutineKey key) {
    if (!valid_key(key)) throw FormatError("invalid queued interaction routine key");
    writer.u8(static_cast<std::uint8_t>(key.scope));
    writer.u16(key.id);
}
RoutineKey read_key(Reader& reader) {
    const auto scope = reader.u8();
    const auto id = reader.u16();
    if (scope > static_cast<std::uint8_t>(RoutineScope::semiglobal))
        throw FormatError("invalid queued interaction routine scope");
    const RoutineKey key{static_cast<RoutineScope>(scope), id};
    if (!valid_key(key)) throw FormatError("queued interaction routine key does not match its ID");
    return key;
}
void write_context(Writer& writer, const VmContext& context) {
    writer.i16(context.caller);
    writer.i16(context.callee);
    writer.i16(context.stack_object);
    writer.u32(context.code_owner);
}
VmContext read_context(Reader& reader) {
    return {reader.i16(), reader.i16(), reader.i16(), reader.u32()};
}
void write_call(Writer& writer, const VmCall& call) {
    write_key(writer, call.key);
    write_context(writer, call.context);
    writer.count(call.arguments.size());
    for (const auto value : call.arguments) writer.i16(value);
    if (call.return_mode > VmCallReturnMode::external_frame_next_tick)
        throw FormatError("invalid queued interaction return mode");
    writer.u8(static_cast<std::uint8_t>(call.return_mode));
}
VmCall read_call(Reader& reader, const EntityThreadControllersSnapshotCodecLimits& limits) {
    VmCall call;
    call.key = read_key(reader);
    call.context = read_context(reader);
    const auto count = reader.count(limits.max_call_arguments, 2);
    call.arguments.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) call.arguments.push_back(reader.i16());
    const auto mode = reader.u8();
    if (mode > static_cast<std::uint8_t>(VmCallReturnMode::external_frame_next_tick))
        throw FormatError("invalid queued interaction return mode");
    call.return_mode = static_cast<VmCallReturnMode>(mode);
    return call;
}

void write_sleep(Writer& writer, const SleepSnapshot& state) {
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
    }
    writer.boolean(state.last_tick.has_value());
    if (state.last_tick) writer.u64(*state.last_tick);
    writer.u64(state.clock);
    writer.boolean(state.interrupted);
    writer.boolean(state.notify_resume);
    writer.string(state.diagnostic);
}
SleepSnapshot read_sleep(Reader& reader, const EntityThreadControllersSnapshotCodecLimits& limits) {
    SleepSnapshot state;
    if (reader.boolean()) state.actor = SleepActorSnapshot{reader.i16(), reader.u64(), reader.u64()};
    if (reader.boolean()) state.job = SleepJobSnapshot{reader.u64(), reader.u64(), reader.u64(),
                                                       reader.size(32767)};
    if (reader.boolean()) state.last_tick = reader.u64();
    state.clock = reader.u64();
    state.interrupted = reader.boolean();
    state.notify_resume = reader.boolean();
    state.diagnostic = reader.string(limits.max_diagnostic_bytes);
    return state;
}

void write_interactions(Writer& writer, const InteractionQueueSnapshot& state) {
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
        write_call(writer, action.call);
    }
    writer.boolean(state.active.has_value());
    if (state.active) { writer.u64(state.active->uid); writer.u64(state.active->ticket); }
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
}
InteractionQueueSnapshot read_interactions(Reader& reader,
        const EntityThreadControllersSnapshotCodecLimits& limits) {
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
    const auto count = reader.count(limits.max_queue_actions, minimum_action_size);
    state.actions.resize(count);
    for (auto& action : state.actions) {
        action.uid = reader.u64();
        action.target_generation = reader.u64();
        action.call = read_call(reader, limits);
    }
    if (reader.boolean()) state.active = InteractionActiveSnapshot{reader.u64(), reader.u64()};
    if (reader.boolean()) state.wait = InteractionWaitSnapshot{reader.u64(), reader.u64()};
    if (reader.boolean()) state.completion = InteractionCompletion{reader.u64(), reader.u64(), reader.u64(), reader.boolean()};
    state.interrupted = reader.boolean();
    state.diagnostic = reader.string(limits.max_diagnostic_bytes);
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
    if (sleep.notify_resume && !sleep.actor)
        throw FormatError("sleep resume state without an actor");

    if (!interactions.capacity || interactions.capacity > limits.max_queue_capacity ||
        interactions.actions.size() > interactions.capacity ||
        interactions.actions.size() > limits.max_queue_actions ||
        interactions.clock > maximum_clock || interactions.idle_start > interactions.clock ||
        (interactions.last_tick ? *interactions.last_tick != interactions.clock : interactions.clock != 0))
        throw FormatError("interaction queue exceeds limits or has inconsistent clock");
    std::set<std::uint64_t> action_uids;
    for (const auto& action : interactions.actions) {
        if (!action.uid || !action_uids.insert(action.uid).second || action.uid > interactions.sequence ||
            !valid_key(action.call.key) || action.call.arguments.size() > limits.max_call_arguments ||
            action.call.context.caller != interactions.actor_id ||
            action.call.context.stack_object != action.call.context.callee ||
            action.call.return_mode != VmCallReturnMode::interaction_next_tick)
            throw FormatError("invalid queued interaction action");
    }
    const auto valid_ticket = [](std::uint64_t ticket) { return ticket != 0; };
    if (interactions.active && (interactions.actions.empty() ||
        interactions.active->uid != interactions.actions.front().uid ||
        !valid_ticket(interactions.active->ticket) || interactions.resume_tick ||
        (!interactions.wait && interactions.idle_start)))
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
    if (interactions.resume_tick && (!interactions.completion || interactions.active || interactions.wait ||
        *interactions.resume_tick > maximum_clock || *interactions.resume_tick <= interactions.clock ||
        *interactions.resume_tick != interactions.completion->tick + 1))
        throw FormatError("invalid interaction resume boundary");
}
}

std::vector<std::uint8_t> encode_entity_thread_controllers_snapshot(
        const EntityThreadControllersSnapshot& snapshot,
        EntityThreadControllersSnapshotCodecLimits limits) {
    if (limits.max_bytes < minimum_size)
        throw FormatError("controller snapshot byte limit is too small");
    validate(snapshot, limits);
    Writer writer(limits.max_bytes);
    writer.u16(codec_version);
    write_sleep(writer, snapshot.sleep);
    write_interactions(writer, snapshot.interactions);
    writer.boolean(snapshot.thread_interrupt);
    return writer.take();
}

EntityThreadControllersSnapshot decode_entity_thread_controllers_snapshot(
        const std::vector<std::uint8_t>& bytes,
        EntityThreadControllersSnapshotCodecLimits limits) {
    if (limits.max_bytes < minimum_size || bytes.size() > limits.max_bytes)
        throw FormatError("controller snapshot exceeds byte limit");
    if (bytes.size() < minimum_size) throw FormatError("controller snapshot header is truncated");
    Reader reader(bytes);
    if (reader.u16() != codec_version) throw FormatError("unsupported controller snapshot version");
    EntityThreadControllersSnapshot snapshot;
    snapshot.sleep = read_sleep(reader, limits);
    snapshot.interactions = read_interactions(reader, limits);
    snapshot.thread_interrupt = reader.boolean();
    if (!reader.done()) throw FormatError("controller snapshot has unclaimed bytes");
    validate(snapshot, limits);
    return snapshot;
}

} // namespace odh
