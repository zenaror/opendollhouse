#include "odh/vm_snapshot_codec.hpp"

#include <limits>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 1;
constexpr std::size_t header_size = 82;
constexpr std::size_t minimum_frame_size = 32;
constexpr std::size_t minimum_pending_size = 55;

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
            throw FormatError("VM snapshot count exceeds file format");
        u32(static_cast<std::uint32_t>(value));
    }
    void string(const std::string& value) {
        count(value.size());
        for (const unsigned char byte : value) push(byte);
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }
private:
    void push(std::uint8_t value) {
        if (bytes_.size() >= limit_) throw FormatError("VM snapshot exceeds byte limit");
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
        if (value > 1) throw FormatError("invalid VM snapshot boolean");
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
    std::size_t size(std::size_t maximum) {
        const auto value = u64();
        if (value > maximum || value > std::numeric_limits<std::size_t>::max())
            throw FormatError("VM snapshot size exceeds limit");
        return static_cast<std::size_t>(value);
    }
    std::uint32_t count(std::size_t maximum, std::size_t minimum_bytes) {
        const auto value = u32();
        if (value > maximum || (minimum_bytes && value > remaining() / minimum_bytes))
            throw FormatError("VM snapshot count exceeds limit or remaining data");
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
        if (count > remaining()) throw FormatError("VM snapshot is truncated");
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

bool valid_scope(RoutineKey key) {
    return key.scope <= RoutineScope::semiglobal && key.scope == routine_key(key.id).scope;
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
void write_key(Writer& writer, RoutineKey key) {
    if (!valid_scope(key)) throw FormatError("invalid VM snapshot routine key");
    writer.u8(static_cast<std::uint8_t>(key.scope));
    writer.u16(key.id);
}
RoutineKey read_key(Reader& reader) {
    const auto scope = reader.u8();
    const auto id = reader.u16();
    if (scope > static_cast<std::uint8_t>(RoutineScope::semiglobal))
        throw FormatError("invalid VM snapshot routine scope");
    const RoutineKey key{static_cast<RoutineScope>(scope), id};
    if (!valid_scope(key)) throw FormatError("VM snapshot routine key does not match its ID");
    return key;
}
void write_instruction(Writer& writer, const Instruction& instruction) {
    writer.u16(instruction.opcode);
    writer.u8(instruction.on_true);
    writer.u8(instruction.on_false);
    for (const auto operand : instruction.operands) writer.u8(operand);
}
Instruction read_instruction(Reader& reader) {
    Instruction instruction{};
    instruction.opcode = reader.u16();
    instruction.on_true = reader.u8();
    instruction.on_false = reader.u8();
    for (auto& operand : instruction.operands) operand = reader.u8();
    return instruction;
}
void write_trace(Writer& writer, const VmTrace& trace, std::size_t max_depth) {
    if (trace.depth > max_depth) throw FormatError("VM trace depth exceeds limit");
    writer.u64(trace.step);
    writer.u64(static_cast<std::uint64_t>(trace.depth));
    write_key(writer, trace.key);
    writer.u16(trace.pc);
    write_instruction(writer, trace.instruction);
    writer.u32(trace.code_owner);
}
VmTrace read_trace(Reader& reader, const VmSnapshotCodecLimits& limits) {
    VmTrace trace{};
    trace.step = reader.u64();
    trace.depth = reader.size(limits.vm.stack_depth);
    trace.key = read_key(reader);
    trace.pc = reader.u16();
    trace.instruction = read_instruction(reader);
    trace.code_owner = reader.u32();
    return trace;
}
void write_pending(Writer& writer, const VmPending& pending, std::size_t max_depth) {
    writer.u64(pending.ticket);
    write_trace(writer, pending.instruction, max_depth);
    write_context(writer, pending.context);
}
VmPending read_pending(Reader& reader, const VmSnapshotCodecLimits& limits) {
    VmPending pending{};
    pending.ticket = reader.u64();
    pending.instruction = read_trace(reader, limits);
    pending.context = read_context(reader);
    return pending;
}
void write_frame(Writer& writer, const VmFrame& frame) {
    write_key(writer, frame.key);
    writer.u16(frame.pc);
    write_context(writer, frame.context);
    writer.count(frame.arguments.size());
    for (const auto value : frame.arguments) writer.i16(value);
    writer.count(frame.locals.size());
    for (const auto value : frame.locals) writer.i16(value);
    if (frame.return_mode > VmCallReturnMode::external_frame_next_tick)
        throw FormatError("invalid VM frame return mode");
    writer.u8(static_cast<std::uint8_t>(frame.return_mode));
    writer.u64(frame.invocation_ticket);
}
VmFrame read_frame(Reader& reader, const VmSnapshotCodecLimits& limits) {
    VmFrame frame{};
    frame.key = read_key(reader);
    frame.pc = reader.u16();
    frame.context = read_context(reader);
    auto count = reader.count(limits.vm.frame_variables, 2);
    frame.arguments.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) frame.arguments.push_back(reader.i16());
    count = reader.count(limits.vm.frame_variables, 2);
    frame.locals.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) frame.locals.push_back(reader.i16());
    const auto mode = reader.u8();
    if (mode > static_cast<std::uint8_t>(VmCallReturnMode::external_frame_next_tick))
        throw FormatError("invalid VM frame return mode");
    frame.return_mode = static_cast<VmCallReturnMode>(mode);
    frame.invocation_ticket = reader.u64();
    return frame;
}
void write_return(Writer& writer, const VmInteractionReturn& value) {
    writer.u64(value.ticket);
    writer.boolean(value.result);
    writer.u32(value.code_owner);
    writer.u64(value.run_generation);
}
VmInteractionReturn read_interaction_return(Reader& reader) {
    return {reader.u64(), reader.boolean(), reader.u32(), reader.u64()};
}
VmExternalFrameReturn read_external_return(Reader& reader) {
    return {reader.u64(), reader.boolean(), reader.u32(), reader.u64()};
}
void write_return(Writer& writer, const VmExternalFrameReturn& value) {
    writer.u64(value.ticket);
    writer.boolean(value.result);
    writer.u32(value.code_owner);
    writer.u64(value.run_generation);
}

void validate_shape(const VmSnapshot& snapshot, const VmSnapshotCodecLimits& limits) {
    if (snapshot.stack.size() > limits.vm.stack_depth ||
        snapshot.suspended_pending.size() > limits.vm.stack_depth ||
        snapshot.diagnostic.size() > limits.max_diagnostic_bytes ||
        snapshot.status > VmStatus::yielded)
        throw FormatError("VM snapshot exceeds configured limits");
    for (const auto& frame : snapshot.stack) {
        if (!valid_scope(frame.key) || frame.arguments.size() > limits.vm.frame_variables ||
            frame.locals.size() > limits.vm.frame_variables ||
            frame.return_mode > VmCallReturnMode::external_frame_next_tick)
            throw FormatError("invalid VM snapshot frame");
    }
    const auto validate_wait = [&](const VmPending& pending) {
        if (pending.instruction.depth > limits.vm.stack_depth ||
            !valid_scope(pending.instruction.key))
            throw FormatError("invalid VM snapshot pending instruction");
    };
    if (snapshot.pending) validate_wait(*snapshot.pending);
    for (const auto& pending : snapshot.suspended_pending) validate_wait(pending);
}
}

std::vector<std::uint8_t> encode_vm_snapshot(
        const VmSnapshot& snapshot, VmSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size)
        throw FormatError("VM snapshot byte limit is too small");
    validate_shape(snapshot, limits);
    Writer writer(limits.max_bytes);
    writer.u16(codec_version);
    writer.u8(static_cast<std::uint8_t>(snapshot.status));
    writer.string(snapshot.diagnostic);
    writer.u64(snapshot.steps);
    writer.u64(snapshot.ticket_sequence);
    writer.u64(snapshot.run_generation);
    for (const auto value : snapshot.temps) writer.i16(value);
    writer.count(snapshot.stack.size());
    for (const auto& frame : snapshot.stack) write_frame(writer, frame);
    writer.boolean(snapshot.pending.has_value());
    if (snapshot.pending) write_pending(writer, *snapshot.pending, limits.vm.stack_depth);
    writer.count(snapshot.suspended_pending.size());
    for (const auto& pending : snapshot.suspended_pending)
        write_pending(writer, pending, limits.vm.stack_depth);
    writer.boolean(snapshot.interaction_return.has_value());
    if (snapshot.interaction_return) write_return(writer, *snapshot.interaction_return);
    writer.boolean(snapshot.external_frame_return.has_value());
    if (snapshot.external_frame_return) write_return(writer, *snapshot.external_frame_return);
    return writer.take();
}

VmSnapshot decode_vm_snapshot(
        const std::vector<std::uint8_t>& bytes, VmSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size || bytes.size() > limits.max_bytes)
        throw FormatError("VM snapshot exceeds byte limit");
    if (bytes.size() < header_size) throw FormatError("VM snapshot header is truncated");
    Reader reader(bytes);
    if (reader.u16() != codec_version) throw FormatError("unsupported VM snapshot version");
    VmSnapshot snapshot;
    const auto status = reader.u8();
    if (status > static_cast<std::uint8_t>(VmStatus::yielded))
        throw FormatError("invalid VM snapshot status");
    snapshot.status = static_cast<VmStatus>(status);
    snapshot.diagnostic = reader.string(limits.max_diagnostic_bytes);
    snapshot.steps = reader.u64();
    snapshot.ticket_sequence = reader.u64();
    snapshot.run_generation = reader.u64();
    for (auto& value : snapshot.temps) value = reader.i16();
    auto count = reader.count(limits.vm.stack_depth, minimum_frame_size);
    snapshot.stack.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        snapshot.stack.push_back(read_frame(reader, limits));
    if (reader.boolean()) snapshot.pending = read_pending(reader, limits);
    count = reader.count(limits.vm.stack_depth, minimum_pending_size);
    snapshot.suspended_pending.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        snapshot.suspended_pending.push_back(read_pending(reader, limits));
    if (reader.boolean()) snapshot.interaction_return = read_interaction_return(reader);
    if (reader.boolean()) snapshot.external_frame_return = read_external_return(reader);
    if (!reader.done()) throw FormatError("VM snapshot has unclaimed bytes");
    validate_shape(snapshot, limits);
    return snapshot;
}

} // namespace odh
