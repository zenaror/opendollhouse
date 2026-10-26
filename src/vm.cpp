#include "odh/vm.hpp"
#include "odh_expression.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <type_traits>

namespace odh {
namespace {
class VmError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
std::int16_t word(std::uint32_t value) {
    const auto low = value & 65535;
    return static_cast<std::int16_t>(low < 32768 ? static_cast<std::int32_t>(low)
                                              : static_cast<std::int32_t>(low) - 65536);
}
std::int16_t operand_word(const Instruction& i, std::size_t offset) {
    return word(static_cast<std::uint32_t>(i.operands[offset]) |
                (static_cast<std::uint32_t>(i.operands[offset + 1]) << 8));
}
std::int16_t& indexed(std::vector<std::int16_t>& values, std::int16_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        throw VmError("variable index outside frame");
    return values[static_cast<std::size_t>(index)];
}
std::int16_t* frame_output(VmFrame& frame, const VmFrameWrite& output,std::size_t maximum) {
    std::vector<std::int16_t>* bank = nullptr;
    switch (output.bank) {
    case VmFrameBank::arguments: bank = &frame.arguments; break;
    case VmFrameBank::locals: bank = &frame.locals; break;
    default: return nullptr;
    }
    if(output.index<bank->size())return &(*bank)[output.index];
    if(output.bank!=VmFrameBank::locals||!output.allow_local_growth||output.index>=maximum)return nullptr;
    bank->resize(output.index+1,0);
    return &(*bank)[output.index];
}
std::optional<Bhav::Metadata> execution_metadata(const Bhav& routine) {
    if (routine.metadata) return routine.metadata;
    // The legacy SimAntics reader profile treats 8000/8001 as four arguments
    // and no locals; unlike 8002, these versions do not serialize that metadata.
    if (routine.version == 0x8000 || routine.version == 0x8001)
        return Bhav::Metadata{0, 4, 0, 0};
    return std::nullopt;
}
} // namespace

RoutineKey routine_key(std::uint16_t id) {
    return {id >= 8192 ? RoutineScope::semiglobal :
            id >= 4096 ? RoutineScope::private_object : RoutineScope::global, id};
}
const char* vm_status_name(VmStatus status) {
    switch (status) {
    case VmStatus::ready: return "ready";
    case VmStatus::dormant: return "dormant";
    case VmStatus::running: return "running";
    case VmStatus::budget_exhausted: return "budget_exhausted";
    case VmStatus::waiting: return "waiting";
    case VmStatus::returned_true: return "returned_true";
    case VmStatus::returned_false: return "returned_false";
    case VmStatus::fault: return "fault";
    case VmStatus::yielded: return "yielded";
    }
    return "invalid_status";
}
Vm::Vm(RoutineSet routines, VmMemory memory, VmLimits limits, VmPrimitive primitive, RoutineOwners owners)
    : Vm(std::make_shared<const VmProgram>(std::move(routines)),ShareVmProgramTag{},std::move(memory),limits,
         std::move(primitive),std::move(owners)) {}
Vm::Vm(std::shared_ptr<const VmProgram> program, ShareVmProgramTag, VmMemory memory, VmLimits limits,
       VmPrimitive primitive, RoutineOwners owners)
    : program_(std::move(program)), owners_(std::move(owners)), memory_(std::move(memory)),
      limits_(limits), primitive_(std::move(primitive)) {
    if(!program_)throw std::invalid_argument("VM program is null");
    if(!limits_.code_owners || owners_.size()>=limits_.code_owners)
        throw std::invalid_argument("VM code owner limit");
    for(const auto& owner:owners_) {
        if(owner.first==0)throw std::invalid_argument("default code owner zero cannot be replaced");
        for(const auto& entry:owner.second) {
            const auto expected=routine_key(entry.first.id);
            if(entry.first.scope==RoutineScope::global || entry.first.scope!=expected.scope)
                throw std::invalid_argument("additional owner contains invalid routine scope");
        }
    }
}
bool Vm::install_owner(CodeOwner owner, RoutineSet routines) {
    if (executing_ || owner == 0 || routines.empty() || owners_.find(owner) != owners_.end() ||
        owners_.size() >= limits_.code_owners - 1) return false;
    for (const auto& entry : routines) {
        const auto expected = routine_key(entry.first.id);
        if (entry.first.scope == RoutineScope::global || entry.first.scope != expected.scope)
            return false;
    }
    owners_.emplace(owner, std::move(routines));
    return true;
}
const Bhav* Vm::resolve(RoutineKey key, CodeOwner owner) const {
    if(owner!=0 && owners_.find(owner)==owners_.end())return nullptr;
    const auto& bank=(key.scope==RoutineScope::global || owner==0)?program_->routines_:owners_.at(owner);
    const auto found=bank.find(key);
    return found==bank.end()?nullptr:&found->second;
}
const Bhav& Vm::current_routine(const VmFrame& frame) const {
    const auto* routine=resolve(frame.key,frame.context.code_owner);
    if(!routine)throw VmError("frame code-owner routine unavailable");
    return *routine;
}

void Vm::push(RoutineKey key, VmContext context, const std::vector<std::int16_t>& args,
              VmCallReturnMode mode, std::uint64_t ticket) {
    if (mode != VmCallReturnMode::ordinary && mode != VmCallReturnMode::interaction_next_tick &&
        mode != VmCallReturnMode::external_frame_next_tick)
        throw VmError("invalid child return mode");
    if (mode == VmCallReturnMode::interaction_next_tick &&
        std::any_of(stack_.begin(), stack_.end(), [](const auto& frame) {
            return frame.return_mode == VmCallReturnMode::interaction_next_tick;
        })) throw VmError("nested interaction invocation unsupported");
    if(context.code_owner!=0 && owners_.find(context.code_owner)==owners_.end())
        throw VmError("code owner not supplied: " + std::to_string(context.code_owner));
    const auto* found = resolve(key,context.code_owner);
    if (!found) throw VmError("routine not supplied: " + std::to_string(key.id) + " code owner " + std::to_string(context.code_owner));
    const auto& routine = *found;
    const auto metadata = execution_metadata(routine);
    if (!metadata) throw VmError("execution requires supported BHAV metadata");
    if (routine.instructions.empty()) throw VmError("empty routine has no entry instruction");
    for (const auto& instruction : routine.instructions)
        for (const auto target : {instruction.on_true, instruction.on_false})
            if (target < 0xFD && target >= routine.instructions.size())
                throw VmError("branch outside routine");
    if (stack_.size() >= limits_.stack_depth) throw VmError("stack depth limit");
    const auto arg_count = std::max<std::size_t>(4, metadata->arguments);
    const auto local_count = std::max<std::size_t>(metadata->locals, metadata->arguments);
    if (arg_count > limits_.frame_variables || local_count > limits_.frame_variables || args.size() > arg_count)
        throw VmError("frame variable limit or too many arguments");
    VmFrame frame{key, 0, context, std::vector<std::int16_t>(arg_count),
                  std::vector<std::int16_t>(local_count), mode, ticket};
    std::copy(args.begin(), args.end(), frame.arguments.begin());
    stack_.push_back(std::move(frame));
}
VmSnapshot Vm::snapshot() const {
    if (executing_) throw std::logic_error("VM callbacks must not capture running state");
    return {stack_, temps_, temp_xl_, status_, diagnostic_, steps_, pending_, suspended_pending_,
            interaction_return_, external_frame_return_, ticket_sequence_, run_generation_};
}
bool Vm::restore_snapshot(const VmSnapshot& state) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter state restore");
    if (status_ != VmStatus::ready || !stack_.empty() || steps_ || run_generation_ ||
        ticket_sequence_ || pending_ || !suspended_pending_.empty() || interaction_return_ ||
        external_frame_return_ || !diagnostic_.empty() ||
        std::any_of(temps_.begin(), temps_.end(), [](auto value) { return value != 0; })) return false;
    if (std::any_of(temp_xl_.begin(), temp_xl_.end(), [](auto value) { return value != 0; })) return false;
    if (state.stack.size() > limits_.stack_depth ||
        state.suspended_pending.size() > state.stack.size()) return false;
    const auto owner_valid = [&](CodeOwner owner) {
        return owner == 0 || owners_.find(owner) != owners_.end();
    };
    const auto ticket_valid = [&](std::uint64_t ticket) {
        return ticket != 0 && ticket <= state.ticket_sequence;
    };
    // Count each live invocation once; external children deliberately share
    // their ticket with the parent wait retained in suspended_pending.
    std::vector<std::uint64_t> tickets;
    std::size_t external_count = 0, interaction_count = 0;
    std::uint64_t last_ticket = 0;
    for (std::size_t depth = 0; depth < state.stack.size(); ++depth) {
        const auto& frame = state.stack[depth];
        if (frame.key.scope != routine_key(frame.key.id).scope || !owner_valid(frame.context.code_owner)) return false;
        const auto* routine = resolve(frame.key, frame.context.code_owner);
        const auto metadata = routine ? execution_metadata(*routine) : std::nullopt;
        if (!metadata || routine->instructions.empty() || frame.pc >= routine->instructions.size()) return false;
        for (const auto& instruction : routine->instructions)
            for (const auto target : {instruction.on_true, instruction.on_false})
                if (target < 0xFD && target >= routine->instructions.size()) return false;
        if (frame.arguments.size() < std::max<std::size_t>(4, metadata->arguments) ||
            frame.locals.size() < std::max<std::size_t>(metadata->locals, metadata->arguments) ||
            frame.arguments.size() > limits_.frame_variables || frame.locals.size() > limits_.frame_variables) return false;
        if (!depth && (frame.return_mode != VmCallReturnMode::ordinary || frame.invocation_ticket)) return false;
        switch (frame.return_mode) {
        case VmCallReturnMode::ordinary: break;
        case VmCallReturnMode::interaction_next_tick:
            if (++interaction_count > 1 || !ticket_valid(frame.invocation_ticket)) return false;
            break;
        case VmCallReturnMode::external_frame_next_tick:
            if (!ticket_valid(frame.invocation_ticket) || external_count >= state.suspended_pending.size()) return false;
            if (state.suspended_pending[external_count].ticket != frame.invocation_ticket ||
                state.suspended_pending[external_count].instruction.depth != depth) return false;
            ++external_count;
            break;
        default: return false;
        }
        if (frame.invocation_ticket) {
            if (!ticket_valid(frame.invocation_ticket) || !state.steps ||
                frame.invocation_ticket <= last_ticket) return false;
            last_ticket = frame.invocation_ticket;
            tickets.push_back(frame.invocation_ticket);
        }
        if (state.status == VmStatus::dormant &&
            (frame.return_mode != VmCallReturnMode::ordinary || frame.invocation_ticket)) return false;
    }
    if (external_count != state.suspended_pending.size()) return false;
    const auto valid_wait = [&](const VmPending& wait) {
        const auto& trace = wait.instruction;
        if (!ticket_valid(wait.ticket) || !trace.depth || trace.depth > state.stack.size() || trace.step >= state.steps) return false;
        const auto& frame = state.stack[trace.depth - 1];
        const auto& instruction = current_routine(frame).instructions[frame.pc];
        return trace.key.scope == frame.key.scope && trace.key.id == frame.key.id && trace.pc == frame.pc &&
            trace.code_owner == frame.context.code_owner &&
            wait.context.caller == frame.context.caller && wait.context.callee == frame.context.callee &&
            wait.context.stack_object == frame.context.stack_object && wait.context.code_owner == frame.context.code_owner &&
            trace.instruction.opcode == instruction.opcode && trace.instruction.on_true == instruction.on_true &&
            trace.instruction.on_false == instruction.on_false && trace.instruction.operands == instruction.operands &&
            instruction.opcode < 256 && instruction.opcode != 2;
    };
    for (std::size_t index = 0; index < state.suspended_pending.size(); ++index) {
        const auto& wait = state.suspended_pending[index];
        if (!valid_wait(wait) || (index &&
            wait.instruction.step <= state.suspended_pending[index - 1].instruction.step)) return false;
    }
    if (state.pending) {
        if (!valid_wait(*state.pending) || state.pending->instruction.depth != state.stack.size() ||
            state.pending->ticket <= last_ticket || (!state.suspended_pending.empty() &&
            state.pending->instruction.step <= state.suspended_pending.back().instruction.step)) return false;
        tickets.push_back(state.pending->ticket);
    }
    const auto valid_return = [&](const auto& event) {
        return ticket_valid(event.ticket) && state.steps && event.ticket > last_ticket &&
            owner_valid(event.code_owner) && event.run_generation == state.run_generation;
    };
    if (state.interaction_return) {
        if (!valid_return(*state.interaction_return) || interaction_count || state.stack.empty()) return false;
        tickets.push_back(state.interaction_return->ticket);
    }
    if (state.external_frame_return && (!valid_return(*state.external_frame_return) ||
        !state.pending || state.external_frame_return->ticket != state.pending->ticket)) return false;
    std::sort(tickets.begin(), tickets.end());
    if (std::adjacent_find(tickets.begin(), tickets.end()) != tickets.end()) return false;
    switch (state.status) {
    case VmStatus::ready:
        if (!state.stack.empty() || state.steps || state.run_generation || state.ticket_sequence || !state.diagnostic.empty()) return false;
        break;
    case VmStatus::dormant:
    case VmStatus::running:
    case VmStatus::budget_exhausted:
        if (state.stack.empty()) return false;
        break;
    case VmStatus::waiting:
        if (!state.pending) return false;
        break;
    case VmStatus::yielded:
        if (bool(state.interaction_return) == bool(state.external_frame_return)) return false;
        break;
    case VmStatus::returned_true:
    case VmStatus::returned_false:
        if (!state.stack.empty()) return false;
        break;
    case VmStatus::fault: break;
    default: return false;
    }
    if (state.status != VmStatus::ready && !state.run_generation) return false;
    if (state.pending && state.status != VmStatus::waiting &&
        !(state.status == VmStatus::yielded && state.external_frame_return)) return false;
    if ((state.interaction_return || state.external_frame_return) && state.status != VmStatus::yielded) return false;
    // Every allocation finishes before any observable destination value changes.
    auto candidate = state;
    static_assert(std::is_nothrow_swappable_v<decltype(pending_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(interaction_return_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(external_frame_return_)>);
    stack_.swap(candidate.stack);
    diagnostic_.swap(candidate.diagnostic);
    suspended_pending_.swap(candidate.suspended_pending);
    temps_ = candidate.temps;
    temp_xl_ = candidate.temp_xl;
    status_ = candidate.status;
    steps_ = candidate.steps;
    pending_.swap(candidate.pending);
    interaction_return_.swap(candidate.interaction_return);
    external_frame_return_.swap(candidate.external_frame_return);
    ticket_sequence_ = candidate.ticket_sequence;
    run_generation_ = candidate.run_generation;
    return true;
}

VmStatus Vm::start(RoutineKey key, VmContext context, const std::vector<std::int16_t>& args) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter start/run");
    if (run_generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::logic_error("VM run generation overflow");
    ++run_generation_;
    stack_.clear(); temps_.fill(0); temp_xl_.fill(0); diagnostic_.clear(); steps_ = 0;
    pending_.reset();
    suspended_pending_.clear();
    interaction_return_.reset();
    external_frame_return_.reset();
    status_ = VmStatus::running;
    try { push(key, context, args); }
    catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
    return status_;
}
void Vm::restore_dormant_state(std::vector<VmFrame> frames,
                               const std::array<std::int16_t, 20>& temps) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter state restore");
    if (frames.empty() || frames.size() > limits_.stack_depth)
        throw std::invalid_argument("restored VM stack is empty or exceeds stack limit");
    if (run_generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::logic_error("VM run generation overflow");

    for (auto& frame : frames) {
        if (frame.key.scope != routine_key(frame.key.id).scope)
            throw std::invalid_argument("restored frame has invalid routine scope");
        if (frame.context.code_owner != 0 && owners_.find(frame.context.code_owner) == owners_.end())
            throw std::invalid_argument("restored frame code owner is unavailable");
        const auto* routine = resolve(frame.key, frame.context.code_owner);
        if (!routine) throw std::invalid_argument("restored frame routine is unavailable");
        const auto metadata = execution_metadata(*routine);
        if (!metadata || routine->instructions.empty())
            throw std::invalid_argument("restored frame routine has unsupported metadata or no instructions");
        if (frame.pc >= routine->instructions.size())
            throw std::invalid_argument("restored frame instruction address is outside its routine");
        for (const auto& instruction : routine->instructions)
            for (const auto target : {instruction.on_true, instruction.on_false})
                if (target < 0xFD && target >= routine->instructions.size())
                    throw std::invalid_argument("restored routine has a branch outside its instructions");

        if (frame.return_mode != VmCallReturnMode::ordinary || frame.invocation_ticket != 0)
            throw std::invalid_argument("restored frame contains runtime-only return state");
        const auto argument_count = std::max<std::size_t>(4, metadata->arguments);
        const auto local_count = std::max<std::size_t>(metadata->locals, metadata->arguments);
        if (argument_count > limits_.frame_variables || local_count > limits_.frame_variables)
            throw std::invalid_argument("restored frame metadata exceeds variable limit");
        if (frame.arguments.size() > limits_.frame_variables || frame.locals.size() > limits_.frame_variables)
            throw std::invalid_argument("restored frame variables exceed limit");
        frame.arguments.resize(std::max(frame.arguments.size(), argument_count), 0);
        frame.locals.resize(std::max(frame.locals.size(), local_count), 0);
    }

    ++run_generation_;
    stack_ = std::move(frames);
    temps_ = temps;
    temp_xl_.fill(0); // Native saved-thread profiles do not supply TempXL values.
    steps_ = 0;
    diagnostic_.clear();
    pending_.reset();
    suspended_pending_.clear();
    interaction_return_.reset();
    external_frame_return_.reset();
    status_ = VmStatus::dormant;
}
bool Vm::activate_dormant_state() {
    if (executing_) throw std::logic_error("VM callbacks must not reenter state activation");
    if (status_ != VmStatus::dormant || stack_.empty()) return false;
    status_ = VmStatus::running;
    return true;
}
std::optional<VmMemoryAddress> resolve_person_data_address(std::uint8_t scope,
    std::int16_t index,const std::array<std::int16_t,20>& temps) {
    if(scope!=30&&scope!=31)return VmMemoryAddress{scope,index};
    if(index<0||static_cast<std::size_t>(index)>=temps.size())return {};
    return VmMemoryAddress{static_cast<std::uint8_t>(scope==30?18:19),temps[static_cast<std::size_t>(index)]};
}
std::int32_t Vm::read(std::uint8_t scope, std::int16_t data) {
    auto& frame = stack_.back();
    const auto temp = [this](std::int16_t index) -> std::int16_t& {
        if (index < 0 || static_cast<std::size_t>(index) >= temps_.size())
            throw VmError("temp index outside registers");
        return temps_[static_cast<std::size_t>(index)];
    };
    switch (scope) {
    case 7: return data;
    case 8: return temp(data);
    case 9: return indexed(frame.arguments, data);
    case 10: return frame.context.stack_object;
    case 11: return temp(temp(data));
    case 25: return indexed(frame.locals, data);
    case 42:
        if (data < 0 || static_cast<std::size_t>(data) >= temp_xl_.size())
            throw VmError("TempXL index outside registers");
        return temp_xl_[static_cast<std::size_t>(data)];
    default:
        if (scope == 27 && memory_.dynamic_sprite_flag) {
            const auto flag=static_cast<std::uint16_t>(temp(data));
            if (const auto value=memory_.dynamic_sprite_flag(frame,flag)) return *value?1:0;
        }
        if (scope == 23 && memory_.room_by_temp0) {
            if (const auto value = memory_.room_by_temp0(frame, temps_[0], data)) return *value;
        }
        if (const auto address=resolve_person_data_address(scope,data,temps_);address&&memory_.read) {
            const auto value = memory_.read(frame, address->scope, address->index);
            if (value) return *value;
        }
        throw VmError("unsupported read scope " + std::to_string(scope) + " index " + std::to_string(data));
    }
}
bool Vm::write(std::uint8_t scope, std::int16_t data, std::int32_t value) {
    auto& frame = stack_.back();
    const auto truncated = word(static_cast<std::uint32_t>(value));
    const auto temp = [this](std::int16_t index) -> std::int16_t& {
        if (index < 0 || static_cast<std::size_t>(index) >= temps_.size())
            throw VmError("temp index outside registers");
        return temps_[static_cast<std::size_t>(index)];
    };
    switch (scope) {
    // The pinned SetVariable explicitly rejects these read-only scopes.
    case 7: case 21: case 26: case 35: case 38: return false;
    case 8: temp(data) = truncated; return true;
    case 9: indexed(frame.arguments, data) = truncated; return true;
    case 10: frame.context.stack_object = truncated; return true;
    case 11: temp(temp(data)) = truncated; return true;
    case 25: indexed(frame.locals, data) = truncated; return true;
    case 42:
        if (data < 0 || static_cast<std::size_t>(data) >= temp_xl_.size())
            throw VmError("TempXL index outside registers");
        temp_xl_[static_cast<std::size_t>(data)] = value; return true;
    default:
        if (scope == 27 && memory_.set_dynamic_sprite_flag) {
            const auto flag=static_cast<std::uint16_t>(temp(data));
            if (memory_.set_dynamic_sprite_flag(frame,flag,truncated>0)) return true;
        }
        if (const auto address=resolve_person_data_address(scope,data,temps_);address) {
            if (memory_.expression_write) {
                if (const auto result=memory_.expression_write(frame,address->scope,address->index,truncated))
                    return *result;
            }
            if (memory_.write && memory_.write(frame,address->scope,address->index,truncated)) return true;
        }
        throw VmError("unsupported write scope " + std::to_string(scope) + " index " + std::to_string(data));
    }
}
bool Vm::expression(const Instruction& i) {
    // Keep all operator evaluation and write ordering in the shared native TS1
    // executor. The callbacks borrow this existing entity thread and never let
    // a C++ exception cross the C ABI; rethrow it on this side after execution.
    struct Context { Vm* vm; std::exception_ptr error; } context{this,{}};
    const auto read_callback = [](void* raw, std::uint8_t scope, std::int16_t index,
                                  std::int32_t* value) noexcept -> std::int32_t {
        auto& state = *static_cast<Context*>(raw);
        try { *value=state.vm->read(scope,index); return 0; }
        catch (...) { state.error=std::current_exception(); return 1; }
    };
    const auto write_callback = [](void* raw, std::uint8_t scope, std::int16_t index,
                                   std::int32_t value, std::uint32_t* accepted) noexcept -> std::int32_t {
        auto& state = *static_cast<Context*>(raw);
        try { *accepted=state.vm->write(scope,index,value)?1U:0U; return 0; }
        catch (...) { state.error=std::current_exception(); return 1; }
    };
    const auto levels_callback = [](void* raw, std::int32_t* caller, std::int32_t* callee,
                                    std::int32_t* offset) noexcept -> std::int32_t {
        auto& state = *static_cast<Context*>(raw);
        try {
            const auto& vm = *state.vm;
            if (!vm.memory_.expression_levels) throw VmError("unsupported expression room levels");
            const auto levels=vm.memory_.expression_levels(vm.stack_.back());
            if (!levels) throw VmError("unsupported expression room levels");
            *caller=(*levels)[0]; *callee=(*levels)[1]; *offset=(*levels)[2]; return 0;
        } catch (...) { state.error=std::current_exception(); return 1; }
    };
    std::uint32_t result=0;
    const auto status=odh_expression_execute_ts1_v1(i.operands.data(),
        static_cast<std::uint32_t>(i.operands.size()), &context,
        read_callback,write_callback,levels_callback,&result);
    if (context.error) std::rethrow_exception(context.error);
    if (status!=ODH_EXPRESSION_OK)
        throw VmError("native expression error " + std::to_string(status) +
                      " operator " + std::to_string(i.operands[5]));
    return result!=0;
}
void Vm::branch(bool result) {
    // Iteratively unwind returning children; parent stays on its call instruction.
    while (!stack_.empty()) {
        auto& frame = stack_.back();
        const auto& instruction = current_routine(frame).instructions.at(frame.pc);
        auto target = result ? instruction.on_true : instruction.on_false;
        if (target == 0xFD) {
            target = instruction.on_true != 0xFD ? instruction.on_true : instruction.on_false;
            if (target == 0xFD) throw VmError("both branches are FD error destinations");
        }
        if (target < 0xFD) {
            if (target >= current_routine(frame).instructions.size()) throw VmError("branch outside routine");
            frame.pc = target; return;
        }
        result = target == 0xFE;
        if (frame.return_mode == VmCallReturnMode::interaction_next_tick) {
            interaction_return_ = VmInteractionReturn{frame.invocation_ticket, result,
                                                       frame.context.code_owner, run_generation_};
            stack_.pop_back();
            status_ = VmStatus::yielded;
            return;
        }
        if (frame.return_mode == VmCallReturnMode::external_frame_next_tick) {
            if (suspended_pending_.empty()) throw VmError("external frame returned without parent wait");
            external_frame_return_ = VmExternalFrameReturn{frame.invocation_ticket, result,
                                                           frame.context.code_owner, run_generation_};
            stack_.pop_back();
            pending_ = std::move(suspended_pending_.back());
            suspended_pending_.pop_back();
            status_ = VmStatus::yielded;
            return;
        }
        stack_.pop_back();
    }
    status_ = result ? VmStatus::returned_true : VmStatus::returned_false;
}
VmStatus Vm::run(std::uint64_t budget, const std::function<void(const VmTrace&)>& trace) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter start/run");
    if (status_ != VmStatus::running && status_ != VmStatus::budget_exhausted) return status_;
    struct Guard {
        bool& value;
        explicit Guard(bool& flag) : value(flag) { value = true; }
        ~Guard() { value = false; }
    } guard(executing_);
    status_ = VmStatus::running;
    for (std::uint64_t used = 0; used < budget && status_ == VmStatus::running; ++used) {
        try {
            const auto& frame = stack_.back();
            const auto& routine = current_routine(frame);
            if (frame.pc >= routine.instructions.size()) throw VmError("instruction pointer outside routine");
            const auto instruction = routine.instructions[frame.pc];
            if (steps_ == std::numeric_limits<std::uint64_t>::max()) throw VmError("step counter overflow");
            if (trace) trace(VmTrace{steps_, stack_.size(), frame.key, frame.pc, instruction,frame.context.code_owner});
            ++steps_;
            if (instruction.opcode >= 256) {
                const auto key = routine_key(instruction.opcode);
                const auto* found = resolve(key,frame.context.code_owner);
                const auto metadata = found ? execution_metadata(*found) : std::nullopt;
                if (!metadata) {
                    push(key, frame.context, {}); // Produce the specific missing/metadata diagnostic.
                    continue;
                }
                const auto count = std::max<std::size_t>(4, metadata->arguments);
                if (count > temps_.size()) throw VmError("call argument count exceeds supported temp mapping");
                std::vector<std::int16_t> args(count);
                const bool substitute = operand_word(instruction, 2) != 0 ||
                    operand_word(instruction, 4) != 0 || operand_word(instruction, 6) != 0;
                for (std::size_t n = 0; n < count; ++n) {
                    auto value = n < 4 ? operand_word(instruction, n * 2) : static_cast<std::int16_t>(-1);
                    if (value == -1 && substitute) value = temps_[n];
                    args[n] = value;
                }
                const auto context = frame.context;
                push(key, context, args);
            } else if (instruction.opcode == 2) {
                const auto result = expression(instruction);
                branch(result);
            } else {
                if (ticket_sequence_ == std::numeric_limits<std::uint64_t>::max())
                    throw VmError("service ticket overflow");
                const VmDispatch dispatch{this,++ticket_sequence_};
                const auto outcome = primitive_ ? primitive_(frame,instruction,temps_,dispatch) : PrimitiveOutcome{};
                if (outcome.stack_object_write &&
                    outcome.disposition != PrimitiveDisposition::goto_true &&
                    outcome.disposition != PrimitiveDisposition::goto_false)
                    throw VmError("primitive stack-object output requires synchronous branch disposition");
                if (outcome.temp_write || outcome.second_temp_write) {
                    if (outcome.disposition != PrimitiveDisposition::goto_true &&
                        outcome.disposition != PrimitiveDisposition::goto_false)
                        throw VmError("primitive temp output requires synchronous branch disposition");
                    if ((outcome.temp_write && outcome.temp_write->index >= temps_.size()) ||
                        (outcome.second_temp_write && outcome.second_temp_write->index >= temps_.size()))
                        throw VmError("primitive temp output index outside registers");
                }
                if(outcome.call && outcome.disposition!=PrimitiveDisposition::call)
                    throw VmError("primitive call payload requires call disposition");
                if (outcome.write && outcome.disposition == PrimitiveDisposition::unsupported)
                    throw VmError("unsupported primitive outcome must not carry frame output");
                switch (outcome.disposition) {
                case PrimitiveDisposition::call:
                    if(!outcome.call)throw VmError("primitive call requires child");
                    if(outcome.call->return_mode==VmCallReturnMode::external_frame_next_tick)
                        throw VmError("external frame child must be scheduled by its pending service");
                    if(outcome.write && !frame_output(stack_.back(),*outcome.write,limits_.frame_variables))
                        throw VmError("primitive call output outside parent frame");
                    {
                    const auto parent_index=stack_.size()-1;
                    push(outcome.call->key,outcome.call->context,outcome.call->arguments,
                         outcome.call->return_mode, dispatch.ticket);
                    if(outcome.write)*frame_output(stack_[parent_index],*outcome.write,limits_.frame_variables)=outcome.write->value;
                    }
                    break;
                case PrimitiveDisposition::goto_true:
                case PrimitiveDisposition::goto_false:
                    // Validate all frame/temp/context outputs before publishing any.
                    if (outcome.write && !frame_output(stack_.back(), *outcome.write,limits_.frame_variables))
                        throw VmError("primitive frame output bank/index outside frame");
                    if (outcome.write) {
                        auto* destination = frame_output(stack_.back(), *outcome.write,limits_.frame_variables);
                        *destination = outcome.write->value;
                    }
                    if (outcome.temp_write)
                        temps_[outcome.temp_write->index] = outcome.temp_write->value;
                    if (outcome.second_temp_write)
                        temps_[outcome.second_temp_write->index] = outcome.second_temp_write->value;
                    if (outcome.stack_object_write)
                        stack_.back().context.stack_object = outcome.stack_object_write->value;
                    branch(outcome.disposition == PrimitiveDisposition::goto_true); break;
                case PrimitiveDisposition::wait:
                    if (outcome.write) {
                        auto* destination = frame_output(stack_.back(), *outcome.write,limits_.frame_variables);
                        if (!destination) throw VmError("primitive frame output bank/index outside frame");
                        *destination = outcome.write->value;
                    }
                    pending_ = VmPending{dispatch.ticket,VmTrace{steps_-1,stack_.size(),frame.key,frame.pc,instruction,frame.context.code_owner},frame.context};
                    status_ = VmStatus::waiting; break;
                case PrimitiveDisposition::unsupported:
                    throw VmError(outcome.diagnostic.empty() ? "unsupported primitive " + std::to_string(instruction.opcode)
                                                             : outcome.diagnostic);
                default: throw VmError("invalid primitive outcome");
                }
            }
        } catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
    }
    if (status_ == VmStatus::running) status_ = VmStatus::budget_exhausted;
    return status_;
}
VmSubroutineResult Vm::run_subroutine_in_temporary_stack(
    RoutineKey key, VmContext context, const std::vector<std::int16_t>& arguments,
    std::uint64_t instruction_budget, const std::function<void(const VmTrace&)>& trace) {
    VmSubroutineResult result;
    const bool suspended_sleep = status_ == VmStatus::waiting && pending_ &&
        pending_->instruction.instruction.opcode == 0;
    const bool live_main = !stack_.empty() &&
        (status_ == VmStatus::dormant || status_ == VmStatus::running ||
         status_ == VmStatus::budget_exhausted || suspended_sleep);
    if (executing_ || !live_main || (pending_ && !suspended_sleep) || !suspended_pending_.empty() ||
        interaction_return_ || external_frame_return_) {
        result.status = status_;
        result.diagnostic = "temporary subroutine requires a live Main at a safe point";
        return result;
    }

    // Capture everything that identifies the suspended Main. Temps and TempXL
    // deliberately stay live: Simitone runs named trees on the entity's thread,
    // so their register changes belong to that thread after the call returns.
    auto main = snapshot();
    stack_.clear();
    status_ = VmStatus::running;
    pending_.reset();
    diagnostic_.clear();
    steps_ = 0;
    try {
        push(key, context, arguments);
        result.started = true;
        result.status = run(instruction_budget, trace);
        result.steps = steps_;
        result.diagnostic = diagnostic_;
    } catch (const std::exception& error) {
        result.started = !stack_.empty();
        result.status = VmStatus::fault;
        result.steps = steps_;
        result.diagnostic = error.what();
    } catch (...) {
        result.started = !stack_.empty();
        result.status = VmStatus::fault;
        result.steps = steps_;
        result.diagnostic = "temporary subroutine raised a non-standard exception";
    }

    // Keep the called tree's registers and consumed dispatch-ticket sequence,
    // but restore every field that describes the suspended Main.
    stack_.swap(main.stack);
    diagnostic_ = std::move(main.diagnostic);
    suspended_pending_.swap(main.suspended_pending);
    status_ = main.status;
    steps_ = main.steps;
    pending_.swap(main.pending);
    interaction_return_.swap(main.interaction_return);
    external_frame_return_.swap(main.external_frame_return);
    run_generation_ = main.run_generation;
    return result;
}
bool Vm::update_pending(std::uint64_t ticket, VmFrameWrite output) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter pending update");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    auto* destination = frame_output(stack_.back(), output,limits_.frame_variables);
    if (!destination) return false;
    *destination = output.value;
    return true;
}
bool Vm::complete(std::uint64_t ticket, bool result, std::optional<VmFrameWrite> output,
                  std::optional<VmStackObjectWrite> stack_object_output) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter completion");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    std::int16_t* frame_destination = nullptr;
    if (output) {
        frame_destination = frame_output(stack_.back(), *output,limits_.frame_variables);
        if (!frame_destination) return false;
    }
    if (frame_destination) *frame_destination = output->value;
    if (stack_object_output) stack_.back().context.stack_object = stack_object_output->value;
    pending_.reset();
    status_ = VmStatus::running;
    try { branch(result); }
    catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
    return true;
}
bool Vm::retry_pending(std::uint64_t ticket, std::optional<VmFrameWrite> output) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter pending retry");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    if (output) {
        auto* destination = frame_output(stack_.back(), *output,limits_.frame_variables);
        if (!destination) return false;
        *destination = output->value;
    }
    pending_.reset();
    status_ = VmStatus::running;
    return true;
}
bool Vm::resume_interaction(std::uint64_t ticket) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter interaction resume");
    if (status_ != VmStatus::yielded || !interaction_return_ ||
        interaction_return_->ticket != ticket ||
        interaction_return_->run_generation != run_generation_) return false;
    interaction_return_.reset();
    status_ = VmStatus::running;
    return true;
}
bool Vm::call_pending(std::uint64_t ticket, const VmCall& call) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter pending child calls");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket ||
        external_frame_return_ || call.return_mode != VmCallReturnMode::ordinary) return false;
    suspended_pending_.push_back(*pending_);
    try {
        push(call.key, call.context, call.arguments,
             VmCallReturnMode::external_frame_next_tick, ticket);
    } catch (const VmError& error) {
        suspended_pending_.pop_back();
        diagnostic_ = error.what();
        return false;
    } catch (...) {
        suspended_pending_.pop_back();
        throw;
    }
    pending_.reset();
    diagnostic_.clear();
    status_ = VmStatus::running;
    return true;
}
bool Vm::resume_external_frame(std::uint64_t ticket) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter external frame resume");
    if (status_ != VmStatus::yielded || !external_frame_return_ ||
        external_frame_return_->ticket != ticket ||
        external_frame_return_->run_generation != run_generation_ ||
        !pending_ || pending_->ticket != ticket) return false;
    external_frame_return_.reset();
    status_ = VmStatus::waiting;
    return true;
}
} // namespace odh
