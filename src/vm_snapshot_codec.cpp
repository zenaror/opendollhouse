#include "odh/vm_snapshot_codec.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace odh {
namespace {
constexpr std::uint16_t codec_version = 9;
constexpr std::uint16_t next_tick_codec_version = 3;
constexpr std::uint16_t legacy_codec_version = 1;
constexpr std::size_t header_size = 90;
constexpr std::size_t legacy_header_size = 82;
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
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
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
    std::int32_t i32() {
        const auto bits = u32();
        return bits <= static_cast<std::uint32_t>(INT32_MAX)
            ? static_cast<std::int32_t>(bits)
            : static_cast<std::int32_t>(static_cast<std::int64_t>(bits) - 0x100000000LL);
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
    return routine_key_valid(key);
}
void write_context(Writer& writer, const VmContext& context,std::uint16_t version) {
    writer.i16(context.caller);
    writer.i16(context.callee);
    writer.i16(context.stack_object);
    writer.u32(context.code_owner);
    if(version>=6){writer.boolean(context.entity_references_bound);if(context.entity_references_bound){
        writer.u64(context.caller_generation);writer.u64(context.callee_generation);writer.u64(context.stack_object_generation);}}
}
VmContext read_context(Reader& reader,std::uint16_t version) {
    VmContext context{reader.i16(),reader.i16(),reader.i16(),reader.u32()};
    if(version>=6){context.entity_references_bound=reader.boolean();if(context.entity_references_bound){
        context.caller_generation=reader.u64();context.callee_generation=reader.u64();context.stack_object_generation=reader.u64();}}
    return context;
}
void write_key(Writer& writer, RoutineKey key) {
    if (!valid_scope(key)) throw FormatError("invalid VM snapshot routine key");
    writer.u8(static_cast<std::uint8_t>(key.scope));
    writer.u16(key.id);
}
RoutineKey read_key(Reader& reader,std::uint16_t version) {
    const auto scope = reader.u8();
    const auto id = reader.u16();
    if (scope > static_cast<std::uint8_t>(version>=8?RoutineScope::named_semiglobal:RoutineScope::semiglobal))
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
VmTrace read_trace(Reader& reader, const VmSnapshotCodecLimits& limits,std::uint16_t version) {
    VmTrace trace{};
    trace.step = reader.u64();
    trace.depth = reader.size(limits.vm.stack_depth);
    trace.key = read_key(reader,version);
    trace.pc = reader.u16();
    trace.instruction = read_instruction(reader);
    trace.code_owner = reader.u32();
    return trace;
}
void write_pending(Writer& writer, const VmPending& pending, std::size_t max_depth,std::uint16_t version) {
    writer.u64(pending.ticket);
    write_trace(writer, pending.instruction, max_depth);
    write_context(writer, pending.context,version);
}
VmPending read_pending(Reader& reader, const VmSnapshotCodecLimits& limits,std::uint16_t version) {
    VmPending pending{};
    pending.ticket = reader.u64();
    pending.instruction = read_trace(reader, limits,version);
    pending.context = read_context(reader,version);
    return pending;
}
void write_frame(Writer& writer, const VmFrame& frame, std::uint16_t version) {
    write_key(writer, frame.key);
    writer.u16(frame.pc);
    write_context(writer, frame.context,version);
    writer.count(frame.arguments.size());
    for (const auto value : frame.arguments) writer.i16(value);
    writer.count(frame.locals.size());
    for (const auto value : frame.locals) writer.i16(value);
    if (frame.return_mode > (version >= 4 ? VmCallReturnMode::immediate : VmCallReturnMode::external_frame_next_tick))
        throw FormatError("invalid VM frame return mode");
    writer.u8(static_cast<std::uint8_t>(frame.return_mode));
    writer.u64(frame.invocation_ticket);
    if(version>=5)writer.boolean(frame.exact_arguments);
    if(version>=6)writer.boolean(frame.action_tree);
    if(version>=9){writer.boolean(frame.original_import.has_value());if(frame.original_import){
        writer.i32(frame.original_import->primitive_state);writer.u8(static_cast<std::uint8_t>(frame.original_import->result));
        writer.count(frame.original_import->uses.size());for(const auto& use:frame.original_import->uses){writer.i16(use.target.id);writer.u64(use.target.generation);writer.u8(use.flags);}}}

}
VmFrame read_frame(Reader& reader, const VmSnapshotCodecLimits& limits, std::uint16_t version) {
    VmFrame frame{};
    frame.key = read_key(reader,version);
    frame.pc = reader.u16();
    frame.context = read_context(reader,version);
    auto count = reader.count(limits.vm.frame_variables, 2);
    frame.arguments.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) frame.arguments.push_back(reader.i16());
    count = reader.count(limits.vm.frame_variables, 2);
    frame.locals.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) frame.locals.push_back(reader.i16());
    const auto mode = reader.u8();
    if (mode > static_cast<std::uint8_t>(version >= 4 ? VmCallReturnMode::immediate : VmCallReturnMode::external_frame_next_tick))
        throw FormatError("invalid VM frame return mode");
    frame.return_mode = static_cast<VmCallReturnMode>(mode);
    frame.invocation_ticket = reader.u64();
    if(version>=5)frame.exact_arguments=reader.boolean();
    if(version>=6)frame.action_tree=reader.boolean();
    if(version>=9&&reader.boolean()){
        const auto primitive=reader.i32();const auto kind=reader.u8();
        if(kind>static_cast<std::uint8_t>(VmOriginalReturn::interaction))throw FormatError("invalid original frame return annotation");
        frame.original_import=VmOriginalFrameState{primitive,static_cast<VmOriginalReturn>(kind)};
        const auto uses=reader.count(limits.vm.frame_variables,11);frame.original_import->uses.reserve(uses);
        for(std::uint32_t i=0;i<uses;++i){const auto id=reader.i16();const auto generation=reader.u64();const auto flags=reader.u8();
            frame.original_import->uses.push_back({{id,generation},flags});}
    }
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

void write_immediate_return(Writer& writer,const VmImmediateReturn& value) {
    writer.u64(value.ticket);writer.boolean(value.result);writer.u32(value.code_owner);
    writer.u64(value.run_generation);
}
VmImmediateReturn read_immediate_return(Reader& reader) {
    return {reader.u64(),reader.boolean(),reader.u32(),reader.u64()};
}

// Mixed source stacks carry three independent relationships: protected action
// frames, external service waits and immediate parent waits. Authenticate each
// edge against its exact frame; overlap alone is not a conflict. This is only
// the value proof: installed routines/owners remain Vm::restore_snapshot's job.
void validate_immediate_ancestry(const VmSnapshot& snapshot) {
    if(snapshot.immediate_continuations.empty())return;
    if(snapshot.stack.empty()||!snapshot.run_generation||
       snapshot.stack.front().return_mode!=VmCallReturnMode::ordinary||snapshot.stack.front().invocation_ticket)
        throw FormatError("VM immediate ancestry has no ordinary root");
    const auto same_context=vm_context_equal;
    const auto valid_ticket=[&](std::uint64_t ticket){return ticket&&ticket<=snapshot.ticket_sequence;};
    const auto valid_wait=[&](const VmPending& wait){
        const auto& trace=wait.instruction;
        if(!valid_ticket(wait.ticket)||!trace.depth||trace.depth>snapshot.stack.size()||trace.step>=snapshot.steps)return false;
        const auto& frame=snapshot.stack[trace.depth-1];
        return trace.key.scope==frame.key.scope&&trace.key.id==frame.key.id&&trace.pc==frame.pc&&
            trace.code_owner==frame.context.code_owner&&same_context(wait.context,frame.context)&&
            trace.instruction.opcode<256&&trace.instruction.opcode!=2;
    };
    std::vector<std::uint64_t> tickets;
    std::size_t external=0,immediate=0;std::uint64_t last_ticket=0;
    for(std::size_t depth=0;depth<snapshot.stack.size();++depth){
        const auto& frame=snapshot.stack[depth];
        if(frame.invocation_ticket){
            if(!valid_ticket(frame.invocation_ticket)||frame.invocation_ticket<=last_ticket)
                throw FormatError("VM protected frame tickets are not ordered");
            last_ticket=frame.invocation_ticket;tickets.push_back(frame.invocation_ticket);
        }
        if(frame.return_mode==VmCallReturnMode::external_frame_next_tick){
            if(external>=snapshot.suspended_pending.size())throw FormatError("VM external frame has no suspended wait");
            const auto& wait=snapshot.suspended_pending[external++];
            if(!depth||!valid_wait(wait)||wait.ticket!=frame.invocation_ticket||wait.instruction.depth!=depth)
                throw FormatError("VM external frame does not match its exact suspended parent");
        }else if(frame.return_mode==VmCallReturnMode::immediate){
            if(immediate>=snapshot.immediate_continuations.size()||!depth||
               snapshot.immediate_continuations[immediate].ticket!=frame.invocation_ticket||
               snapshot.immediate_continuations[immediate].parent_depth!=depth||
               frame.context.caller<=0||frame.context.caller!=snapshot.stack[depth-1].context.caller)
                throw FormatError("VM immediate frame has no exact caller continuation");
            ++immediate;
        }else if(frame.return_mode!=VmCallReturnMode::ordinary&&!frame.invocation_ticket)
            throw FormatError("VM protected action frame has no invocation ticket");
    }
    if(external!=snapshot.suspended_pending.size())throw FormatError("VM suspended wait has no external frame");
    for(std::size_t n=0;n<snapshot.suspended_pending.size();++n)
        if(n&&snapshot.suspended_pending[n].instruction.step<=snapshot.suspended_pending[n-1].instruction.step)
            throw FormatError("VM suspended wait source steps are not ordered");
    for(const auto& continuation:snapshot.immediate_continuations)if(continuation.parent_pending){
        const auto& wait=*continuation.parent_pending;
        if(!valid_wait(wait)||wait.instruction.depth!=continuation.parent_depth)
            throw FormatError("VM immediate parent wait has no exact source frame");
        for(std::size_t depth=0;depth<continuation.parent_depth;++depth)
            if(snapshot.stack[depth].invocation_ticket>=wait.ticket)
                throw FormatError("VM immediate parent wait predates its protected ancestor");
        tickets.push_back(wait.ticket);
    }
    if(immediate+static_cast<std::size_t>(snapshot.immediate_return.has_value())!=snapshot.immediate_continuations.size())
        throw FormatError("VM immediate frame and continuation rosters differ");
    if(snapshot.pending){
        if(!valid_wait(*snapshot.pending)||snapshot.pending->instruction.depth!=snapshot.stack.size()||
           snapshot.pending->ticket<=last_ticket||(!snapshot.suspended_pending.empty()&&
            snapshot.pending->instruction.step<=snapshot.suspended_pending.back().instruction.step))
            throw FormatError("VM immediate current wait has no exact top frame");
        tickets.push_back(snapshot.pending->ticket);
    }
    const auto valid_return=[&](const auto& result){return valid_ticket(result.ticket)&&snapshot.steps&&
        result.ticket>last_ticket&&result.run_generation==snapshot.run_generation;};
    if(snapshot.interaction_return){
        if(!valid_return(*snapshot.interaction_return))throw FormatError("VM interaction return has stale immediate ancestry");
        tickets.push_back(snapshot.interaction_return->ticket);
    }
    if(snapshot.external_frame_return&&(!valid_return(*snapshot.external_frame_return)||!snapshot.pending||
       snapshot.external_frame_return->ticket!=snapshot.pending->ticket))
        throw FormatError("VM external return has no exact restored parent wait");
    if(snapshot.immediate_return){
        if(!valid_return(*snapshot.immediate_return))throw FormatError("VM immediate return has stale protected ancestry");
        tickets.push_back(snapshot.immediate_return->ticket);
    }
    std::sort(tickets.begin(),tickets.end());
    if(std::adjacent_find(tickets.begin(),tickets.end())!=tickets.end())
        throw FormatError("VM immediate ancestry reuses an unrelated ticket");
    const auto returns=static_cast<unsigned>(snapshot.interaction_return.has_value())+
        static_cast<unsigned>(snapshot.external_frame_return.has_value())+static_cast<unsigned>(snapshot.immediate_return.has_value());
    if((snapshot.status==VmStatus::yielded?returns!=1:returns!=0)||
       (snapshot.status==VmStatus::waiting&&!snapshot.pending)||
       (snapshot.pending&&snapshot.status!=VmStatus::waiting&&
        !(snapshot.status==VmStatus::yielded&&snapshot.external_frame_return))||
       snapshot.status==VmStatus::ready||snapshot.status==VmStatus::dormant||
       snapshot.status==VmStatus::returned_true||snapshot.status==VmStatus::returned_false)
        throw FormatError("VM immediate return channels do not match execution status");
}
void validate_shape(const VmSnapshot& snapshot, const VmSnapshotCodecLimits& limits) {
    if(!advertisement_changes_valid(snapshot.advertisement_changes))throw FormatError("invalid or oversized VM advertisement dictionary");
    for(const auto& frame:snapshot.stack)if(named_routine_scope(frame.key.scope)&&!frame.context.code_owner)
        throw FormatError("named routine requires an explicit nonzero resource owner");
    const auto named_pending=[](const VmPending& pending){if(named_routine_scope(pending.instruction.key.scope)&&
        (!pending.context.code_owner||pending.context.code_owner!=pending.instruction.code_owner))throw FormatError("named pending resource owner unavailable");};
    if(snapshot.pending)named_pending(*snapshot.pending);
    for(const auto& pending:snapshot.suspended_pending)named_pending(pending);
    for(const auto& item:snapshot.immediate_continuations)if(item.parent_pending)named_pending(*item.parent_pending);

    const auto context_valid=[](const VmContext& context){
        if(!context.entity_references_bound)return !context.caller_generation&&!context.callee_generation&&!context.stack_object_generation;
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
            const auto ref=vm_context_reference(context,role);if(ref.generation&&ref.id<=0)return false;}
        return true;
    };
    for(const auto& frame:snapshot.stack)if(!context_valid(frame.context)||(!snapshot.action_tree_known&&frame.action_tree))throw FormatError("invalid VM entity reference or unknown ActionTree");
    // SessionBundle route values flatten a real frame (and optional replay
    // frame) plus pending/replay instruction into a non-executable envelope.
    // Executable source ancestry is still authenticated by Vm::restore_snapshot.
    const bool frame_envelope=snapshot.status==VmStatus::ready&&!snapshot.run_generation&&!snapshot.steps&&
        !snapshot.ticket_sequence&&snapshot.stack.size()<=2&&snapshot.suspended_pending.size()<=1&&
        !snapshot.interaction_return&&!snapshot.external_frame_return&&snapshot.immediate_continuations.empty()&&
        !snapshot.immediate_return&&!snapshot.advertisement_changes;
    for(std::size_t depth=0;depth<snapshot.stack.size();++depth){const auto& frame=snapshot.stack[depth];
        if(!frame.original_import)continue;
        const auto kind=frame.original_import->result;
        if(frame.original_import->uses.size()>limits.vm.frame_variables||
           std::any_of(frame.original_import->uses.begin(),frame.original_import->uses.end(),[](const auto& use){return use.target.id<=0||!use.target.generation;}))
            throw FormatError("invalid original use bank identity or size");
        if(!snapshot.action_tree_known||kind>VmOriginalReturn::interaction||
           (kind==VmOriginalReturn::retry&&((!depth&&!frame_envelope)||frame.return_mode!=VmCallReturnMode::ordinary||frame.invocation_ticket))||
           (kind==VmOriginalReturn::interaction&&((!depth&&!frame_envelope)||frame.return_mode!=VmCallReturnMode::interaction_next_tick||!frame.invocation_ticket||!frame.action_tree)))
            throw FormatError("invalid original import frame annotation");
    }

    if(snapshot.pending&&!context_valid(snapshot.pending->context))throw FormatError("invalid pending entity references");
    for(const auto& pending:snapshot.suspended_pending)if(!context_valid(pending.context))throw FormatError("invalid suspended entity references");
    for(const auto& continuation:snapshot.immediate_continuations)if(continuation.parent_pending&&!context_valid(continuation.parent_pending->context))throw FormatError("invalid immediate entity references");

    if (snapshot.stack.size() > limits.vm.stack_depth ||
        snapshot.suspended_pending.size() > limits.vm.stack_depth ||
        snapshot.immediate_continuations.size() > limits.vm.stack_depth ||
        snapshot.diagnostic.size() > limits.max_diagnostic_bytes ||
        snapshot.status > VmStatus::next_tick)
        throw FormatError("VM snapshot exceeds configured limits");
    if(snapshot.status==VmStatus::next_tick && (snapshot.stack.empty() || snapshot.pending ||
        snapshot.interaction_return || snapshot.external_frame_return))
        throw FormatError("invalid next-tick VM snapshot shape");
    for (const auto& frame : snapshot.stack) {
        if (!valid_scope(frame.key) || frame.arguments.size() > limits.vm.frame_variables ||
            frame.locals.size() > limits.vm.frame_variables ||
            frame.return_mode > VmCallReturnMode::immediate)
            throw FormatError("invalid VM snapshot frame");
    }
    const auto validate_wait = [&](const VmPending& pending) {
        if (pending.instruction.depth > limits.vm.stack_depth ||
            !valid_scope(pending.instruction.key))
            throw FormatError("invalid VM snapshot pending instruction");
    };
    if (snapshot.pending) validate_wait(*snapshot.pending);
    for (const auto& pending : snapshot.suspended_pending) validate_wait(pending);
    if(snapshot.immediate_return && snapshot.immediate_continuations.empty())
        throw FormatError("VM immediate return has no continuation");
    validate_immediate_ancestry(snapshot);
    std::size_t prior_depth=0;std::uint64_t prior_ticket=0;
    for(std::size_t index=0;index<snapshot.immediate_continuations.size();++index) {
        const auto& continuation=snapshot.immediate_continuations[index];
        for(std::size_t previous=0;previous<index;++previous)
            if(snapshot.immediate_continuations[previous].ticket==continuation.ticket||
               (snapshot.immediate_continuations[previous].parent_pending&&
                snapshot.immediate_continuations[previous].parent_pending->ticket==continuation.ticket))
                throw FormatError("duplicate VM immediate continuation ticket");
        if(!continuation.ticket || continuation.ticket>snapshot.ticket_sequence ||
           continuation.parent_depth<1 || continuation.parent_depth>snapshot.stack.size() ||
           continuation.parent_depth<=prior_depth || continuation.ticket<=prior_ticket ||
           (continuation.parent_status!=VmStatus::running &&
            continuation.parent_status!=VmStatus::budget_exhausted &&
            continuation.parent_status!=VmStatus::waiting) ||
           (continuation.parent_status==VmStatus::waiting)!=continuation.parent_pending.has_value())
            throw FormatError("invalid VM immediate continuation");
        if(continuation.parent_pending) {
            validate_wait(*continuation.parent_pending);
            if(!continuation.parent_pending->ticket||continuation.parent_pending->ticket>snapshot.ticket_sequence)
                throw FormatError("invalid VM immediate parent pending ticket");
            const auto& parent=snapshot.stack[continuation.parent_depth-1];
            const auto& trace=continuation.parent_pending->instruction;
            if(trace.depth!=continuation.parent_depth||trace.step>=snapshot.steps||trace.pc!=parent.pc||
               trace.key.scope!=parent.key.scope||trace.key.id!=parent.key.id||
               trace.code_owner!=parent.context.code_owner||
               continuation.parent_pending->context.caller!=parent.context.caller||
               continuation.parent_pending->context.callee!=parent.context.callee||
               continuation.parent_pending->context.stack_object!=parent.context.stack_object||
               continuation.parent_pending->context.code_owner!=parent.context.code_owner)
                throw FormatError("VM immediate parent pending does not match its frame");
            if(continuation.parent_pending->ticket<=prior_ticket||
               continuation.parent_pending->ticket>=continuation.ticket)
                throw FormatError("VM immediate continuation reuses parent pending ticket");
            for(std::size_t previous=0;previous<index;++previous)
                if(snapshot.immediate_continuations[previous].ticket==continuation.parent_pending->ticket||
                   (snapshot.immediate_continuations[previous].parent_pending&&
                    snapshot.immediate_continuations[previous].parent_pending->ticket==continuation.parent_pending->ticket))
                    throw FormatError("duplicate VM immediate pending ticket");
        }
        if(snapshot.stack.size()==continuation.parent_depth) {
            if(index+1!=snapshot.immediate_continuations.size()||!snapshot.immediate_return||
               snapshot.immediate_return->ticket!=continuation.ticket)
                throw FormatError("VM immediate continuation has no protected frame");
        } else {
            const auto& child=snapshot.stack[continuation.parent_depth];
            if(child.return_mode!=VmCallReturnMode::immediate||child.invocation_ticket!=continuation.ticket)
                throw FormatError("VM immediate continuation does not match its child frame");
        }
        prior_depth=continuation.parent_depth;prior_ticket=continuation.ticket;
    }
    if(snapshot.pending&&!snapshot.immediate_continuations.empty()) {
        const auto& top=snapshot.stack.back();const auto& wait=*snapshot.pending;
        const auto& trace=wait.instruction;
        if(!wait.ticket||wait.ticket>snapshot.ticket_sequence||wait.ticket<=top.invocation_ticket||
           trace.step>=snapshot.steps||trace.depth!=snapshot.stack.size()||trace.pc!=top.pc||
           trace.key.scope!=top.key.scope||trace.key.id!=top.key.id||trace.code_owner!=top.context.code_owner||
           wait.context.caller!=top.context.caller||wait.context.callee!=top.context.callee||
           wait.context.stack_object!=top.context.stack_object||wait.context.code_owner!=top.context.code_owner)
            throw FormatError("VM immediate current pending does not match its child frame");
        for(const auto& continuation:snapshot.immediate_continuations)
            if(wait.ticket==continuation.ticket||
               (continuation.parent_pending&&wait.ticket==continuation.parent_pending->ticket))
                throw FormatError("VM immediate state reuses current pending ticket");
    }
    if(snapshot.immediate_return && (!snapshot.immediate_return->ticket ||
       snapshot.immediate_return->ticket>snapshot.ticket_sequence ||
       !snapshot.immediate_return->run_generation || snapshot.immediate_continuations.empty() ||
       snapshot.immediate_return->ticket!=snapshot.immediate_continuations.back().ticket ||
       snapshot.immediate_return->run_generation!=snapshot.run_generation ||
       snapshot.status!=VmStatus::yielded || snapshot.pending ||
       snapshot.stack.size()!=snapshot.immediate_continuations.back().parent_depth))
        throw FormatError("invalid VM immediate return");
}
}

namespace {
bool uses_named_state(const VmSnapshot& snapshot){
    for(const auto& frame:snapshot.stack)if(named_routine_scope(frame.key.scope))return true;
    const auto pending=[](const auto& p){return named_routine_scope(p.instruction.key.scope);};
    if(snapshot.pending&&pending(*snapshot.pending))return true;
    for(const auto& item:snapshot.suspended_pending)if(pending(item))return true;
    for(const auto& item:snapshot.immediate_continuations)if(item.parent_pending&&pending(*item.parent_pending))return true;
    return false;
}
bool uses_reference_state(const VmSnapshot& snapshot){
    if(snapshot.action_tree_known)return true;
    for(const auto& frame:snapshot.stack)if(frame.action_tree||frame.context.entity_references_bound)return true;
    if(snapshot.pending&&snapshot.pending->context.entity_references_bound)return true;
    for(const auto& pending:snapshot.suspended_pending)if(pending.context.entity_references_bound)return true;
    for(const auto& continuation:snapshot.immediate_continuations)if(continuation.parent_pending&&continuation.parent_pending->context.entity_references_bound)return true;
    return false;
}
}
std::vector<std::uint8_t> encode_vm_snapshot(
        const VmSnapshot& snapshot, VmSnapshotCodecLimits limits) {
    if (limits.max_bytes < header_size)
        throw FormatError("VM snapshot byte limit is too small");
    validate_shape(snapshot, limits);
    Writer writer(limits.max_bytes);
    const auto exact=std::any_of(snapshot.stack.begin(),snapshot.stack.end(),[](const auto& frame){return frame.exact_arguments;});
    const bool modern=uses_reference_state(snapshot);
    const std::uint16_t version=std::any_of(snapshot.stack.begin(),snapshot.stack.end(),[](const auto& frame){return frame.original_import.has_value();})?codec_version:uses_named_state(snapshot)?std::uint16_t{8}:snapshot.advertisement_changes?std::uint16_t{7}:modern?std::uint16_t{6}:exact?std::uint16_t{5}:(!snapshot.immediate_continuations.empty()||snapshot.immediate_return)
        ? std::uint16_t{4} : (snapshot.status==VmStatus::next_tick ? next_tick_codec_version : std::uint16_t{2});
    writer.u16(version);
    if(version>=6)writer.boolean(snapshot.action_tree_known);
    writer.u8(static_cast<std::uint8_t>(snapshot.status));
    writer.string(snapshot.diagnostic);
    writer.u64(snapshot.steps);
    writer.u64(snapshot.ticket_sequence);
    writer.u64(snapshot.run_generation);
    for (const auto value : snapshot.temps) writer.i16(value);
    for (const auto value : snapshot.temp_xl) writer.i32(value);
    writer.count(snapshot.stack.size());
    for (const auto& frame : snapshot.stack) write_frame(writer, frame,version);
    writer.boolean(snapshot.pending.has_value());
    if (snapshot.pending) write_pending(writer, *snapshot.pending, limits.vm.stack_depth,version);
    writer.count(snapshot.suspended_pending.size());
    for (const auto& pending : snapshot.suspended_pending)
        write_pending(writer, pending, limits.vm.stack_depth,version);
    writer.boolean(snapshot.interaction_return.has_value());
    if (snapshot.interaction_return) write_return(writer, *snapshot.interaction_return);
    writer.boolean(snapshot.external_frame_return.has_value());
    if (snapshot.external_frame_return) write_return(writer, *snapshot.external_frame_return);
    if(version>=4) {
        writer.count(snapshot.immediate_continuations.size());
        for(const auto& continuation:snapshot.immediate_continuations) {
            writer.u64(continuation.ticket);
            writer.u64(static_cast<std::uint64_t>(continuation.parent_depth));
            writer.u8(static_cast<std::uint8_t>(continuation.parent_status));
            writer.boolean(continuation.parent_pending.has_value());
            if(continuation.parent_pending)write_pending(writer,*continuation.parent_pending,limits.vm.stack_depth,version);
        }
        writer.boolean(snapshot.immediate_return.has_value());
        if(snapshot.immediate_return)write_immediate_return(writer,*snapshot.immediate_return);
    }
    if(version>=8)writer.boolean(snapshot.advertisement_changes.has_value());
    if(version==7||(version>=8&&snapshot.advertisement_changes)){
        writer.count(snapshot.advertisement_changes->size());
        for(const auto& entry:*snapshot.advertisement_changes){writer.u32(entry.first);writer.i16(entry.second);}
    }
    return writer.take();
}

VmSnapshot decode_vm_snapshot(
        const std::vector<std::uint8_t>& bytes, VmSnapshotCodecLimits limits) {
    if (limits.max_bytes < legacy_header_size || bytes.size() > limits.max_bytes)
        throw FormatError("VM snapshot exceeds byte limit");
    if (bytes.size() < legacy_header_size) throw FormatError("VM snapshot header is truncated");
    Reader reader(bytes);
    const auto version = reader.u16();
    if (version < legacy_codec_version || version > codec_version)
        throw FormatError("unsupported VM snapshot version");
    VmSnapshot snapshot;
    if(version>=6)snapshot.action_tree_known=reader.boolean();
    const auto status = reader.u8();
    if (status > static_cast<std::uint8_t>(version>=next_tick_codec_version ? VmStatus::next_tick : VmStatus::yielded))
        throw FormatError("invalid VM snapshot status");
    snapshot.status = static_cast<VmStatus>(status);
    snapshot.diagnostic = reader.string(limits.max_diagnostic_bytes);
    snapshot.steps = reader.u64();
    snapshot.ticket_sequence = reader.u64();
    snapshot.run_generation = reader.u64();
    for (auto& value : snapshot.temps) value = reader.i16();
    if (version >= 2)
        for (auto& value : snapshot.temp_xl) value = reader.i32();
    auto count = reader.count(limits.vm.stack_depth, minimum_frame_size);
    snapshot.stack.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        snapshot.stack.push_back(read_frame(reader, limits,version));
    if (reader.boolean()) snapshot.pending = read_pending(reader, limits,version);
    count = reader.count(limits.vm.stack_depth, minimum_pending_size);
    snapshot.suspended_pending.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        snapshot.suspended_pending.push_back(read_pending(reader, limits,version));
    if (reader.boolean()) snapshot.interaction_return = read_interaction_return(reader);
    if (reader.boolean()) snapshot.external_frame_return = read_external_return(reader);
    if(version>=4) {
        count=reader.count(limits.vm.stack_depth,18);
        snapshot.immediate_continuations.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            VmImmediateContinuation continuation;
            continuation.ticket=reader.u64();
            continuation.parent_depth=reader.size(limits.vm.stack_depth);
            const auto parent_status=reader.u8();
            if(parent_status>static_cast<std::uint8_t>(VmStatus::next_tick))
                throw FormatError("invalid VM immediate parent status");
            continuation.parent_status=static_cast<VmStatus>(parent_status);
            if(reader.boolean())continuation.parent_pending=read_pending(reader,limits,version);
            snapshot.immediate_continuations.push_back(std::move(continuation));
        }
        if(reader.boolean())snapshot.immediate_return=read_immediate_return(reader);
        if(version==4&&snapshot.immediate_continuations.empty()&&!snapshot.immediate_return)
            throw FormatError("noncanonical empty VM snapshot v4 extension");
        if(version==5&&!std::any_of(snapshot.stack.begin(),snapshot.stack.end(),
                [](const auto& frame){return frame.exact_arguments;}))
            throw FormatError("noncanonical VM snapshot exact argument extension");
    }
    const bool advertisements_present=version==7||(version>=8&&reader.boolean());
    if(advertisements_present){
        const auto count=reader.count(advertisement_key_limit,6);snapshot.advertisement_changes.emplace();
        std::optional<std::uint32_t> previous;
        for(std::uint32_t n=0;n<count;++n){const auto key=reader.u32();const auto value=reader.i16();
            if((key>>16)>2||(previous&&key<=*previous))throw FormatError("invalid or unordered VM advertisement key");
            snapshot.advertisement_changes->emplace(key,value);previous=key;
        }
    }
    if (!reader.done()) throw FormatError("VM snapshot has unclaimed bytes");
    validate_shape(snapshot, limits);
    if(version==9&&!std::any_of(snapshot.stack.begin(),snapshot.stack.end(),[](const auto& frame){return frame.original_import.has_value();}))
        throw FormatError("empty VM original-import extension is noncanonical");
    if(version==8&&!uses_named_state(snapshot))throw FormatError("empty VM named resource extension is noncanonical");
    if(version==6&&!uses_reference_state(snapshot))throw FormatError("empty VM reference extension is noncanonical");
    return snapshot;
}

} // namespace odh
