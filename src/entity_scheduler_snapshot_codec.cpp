#include "odh/entity_scheduler_snapshot_codec.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 1;
constexpr std::size_t header_size = 29;
constexpr std::size_t entity_size = 22;
static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t), "scheduler codec requires size_t no wider than 64 bits");

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
    void string(const std::string& value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max())
            throw FormatError("scheduler diagnostic exceeds file format");
        u32(static_cast<std::uint32_t>(value.size()));
        for (const unsigned char byte : value) push(byte);
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    void push(std::uint8_t value) {
        if (bytes_.size() >= limit_) throw FormatError("scheduler snapshot exceeds byte limit");
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
        if (value > 1) throw FormatError("invalid scheduler snapshot boolean");
        return value != 0;
    }
    std::uint16_t u16() {
        const auto low = static_cast<std::uint16_t>(u8());
        const auto high = static_cast<std::uint16_t>(u8());
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8));
    }
    std::int16_t i16() {
        const auto bits = u16();
        const auto value = bits <= static_cast<std::uint16_t>(INT16_MAX)
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
    std::uint32_t count(std::size_t maximum, std::size_t minimum_bytes) {
        const auto value = u32();
        if (value > maximum || value > remaining() / minimum_bytes)
            throw FormatError("scheduler entity count exceeds limit or remaining data");
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
        if (count > remaining()) throw FormatError("scheduler snapshot is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

void validate(const EntityVmSchedulerSnapshot& snapshot,
              const EntityVmSchedulerSnapshotCodecLimits& limits) {
    if (snapshot.entities.size() > limits.max_entities ||
        snapshot.entities.size() > snapshot.entity_limit ||
        snapshot.entity_limit > limits.max_entities ||
        snapshot.entities.size() > std::numeric_limits<std::uint32_t>::max() ||
        snapshot.diagnostic.size() > limits.max_diagnostic_bytes)
        throw FormatError("scheduler snapshot exceeds configured limits");
    std::optional<std::int16_t> previous;
    bool cursor_found = !snapshot.budget_cursor;
    for (const auto& entity : snapshot.entities) {
        if (previous && entity.id <= *previous)
            throw FormatError("scheduler entity IDs are not in strict ascending order");
        previous = entity.id;
        if (entity.status > VmStatus::next_tick)
            throw FormatError("invalid scheduler VM status");
        if (snapshot.budget_cursor && entity.id == *snapshot.budget_cursor) cursor_found = true;
    }
    if (!cursor_found) throw FormatError("scheduler budget cursor is not registered");
    if (snapshot.last_tick && (!*snapshot.last_tick ||
        *snapshot.last_tick > std::numeric_limits<std::uint32_t>::max()))
        throw FormatError("scheduler tick exceeds supported range");
    if (snapshot.instructions_last_tick && !snapshot.last_tick && !snapshot.faulted)
        throw FormatError("scheduler instruction count has no tick");
    if (snapshot.faulted && snapshot.diagnostic.empty())
        throw FormatError("faulted scheduler snapshot has no diagnostic");
}
}

std::vector<std::uint8_t> encode_entity_vm_scheduler_snapshot(
        const EntityVmSchedulerSnapshot& snapshot,
        EntityVmSchedulerSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size)
        throw FormatError("scheduler snapshot byte limit is too small");
    validate(snapshot, limits);
    Writer writer(limits.max_bytes);
    const auto version=std::any_of(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& entity){
        return entity.status==VmStatus::next_tick;})?std::uint16_t{2}:codec_version;
    writer.u16(version);
    writer.u64(static_cast<std::uint64_t>(snapshot.entity_limit));
    writer.u32(static_cast<std::uint32_t>(snapshot.entities.size()));
    for (const auto& entity : snapshot.entities) {
        writer.i16(entity.id);
        writer.u64(entity.run_generation);
        writer.u64(entity.steps);
        writer.u8(static_cast<std::uint8_t>(entity.status));
        writer.boolean(entity.before);
        writer.boolean(entity.after);
        writer.boolean(entity.interrupt);
    }
    writer.boolean(snapshot.budget_cursor.has_value());
    if (snapshot.budget_cursor) writer.i16(*snapshot.budget_cursor);
    writer.boolean(snapshot.last_tick.has_value());
    if (snapshot.last_tick) writer.u64(*snapshot.last_tick);
    writer.u64(snapshot.instructions_last_tick);
    writer.boolean(snapshot.faulted);
    writer.string(snapshot.diagnostic);
    return writer.take();
}

EntityVmSchedulerSnapshot decode_entity_vm_scheduler_snapshot(
        const std::vector<std::uint8_t>& bytes,
        EntityVmSchedulerSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size || bytes.size() > limits.max_bytes)
        throw FormatError("scheduler snapshot exceeds byte limit");
    if (bytes.size() < header_size) throw FormatError("scheduler snapshot header is truncated");
    Reader reader(bytes);
    const auto version=reader.u16();
    if (version!=1 && version!=2) throw FormatError("unsupported scheduler snapshot version");
    EntityVmSchedulerSnapshot snapshot;
    const auto entity_limit = reader.u64();
    if (entity_limit > std::numeric_limits<std::size_t>::max())
        throw FormatError("scheduler entity limit exceeds platform size");
    snapshot.entity_limit = static_cast<std::size_t>(entity_limit);
    const auto count = reader.count(limits.max_entities, entity_size);
    snapshot.entities.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        EntityVmSchedulerSnapshot::Entity entity;
        entity.id = reader.i16();
        entity.run_generation = reader.u64();
        entity.steps = reader.u64();
        const auto status = reader.u8();
        if (status > static_cast<std::uint8_t>(version>=2?VmStatus::next_tick:VmStatus::yielded))
            throw FormatError("invalid scheduler VM status");
        entity.status = static_cast<VmStatus>(status);
        entity.before = reader.boolean();
        entity.after = reader.boolean();
        entity.interrupt = reader.boolean();
        snapshot.entities.push_back(entity);
    }
    if (reader.boolean()) snapshot.budget_cursor = reader.i16();
    if (reader.boolean()) snapshot.last_tick = reader.u64();
    snapshot.instructions_last_tick = reader.u64();
    snapshot.faulted = reader.boolean();
    snapshot.diagnostic = reader.string(limits.max_diagnostic_bytes);
    if (!reader.done()) throw FormatError("scheduler snapshot has unclaimed bytes");
    validate(snapshot, limits);
    return snapshot;
}

} // namespace odh
