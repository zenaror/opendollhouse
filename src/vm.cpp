#include "odh/vm.hpp"

#include <algorithm>
#include <limits>

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
std::int16_t* frame_output(VmFrame& frame, const VmFrameWrite& output) {
    std::vector<std::int16_t>* bank = nullptr;
    switch (output.bank) {
    case VmFrameBank::arguments: bank = &frame.arguments; break;
    case VmFrameBank::locals: bank = &frame.locals; break;
    default: return nullptr;
    }
    return output.index < bank->size() ? &(*bank)[output.index] : nullptr;
}
} // namespace

RoutineKey routine_key(std::uint16_t id) {
    return {id >= 8192 ? RoutineScope::semiglobal :
            id >= 4096 ? RoutineScope::private_object : RoutineScope::global, id};
}
const char* vm_status_name(VmStatus status) {
    switch (status) {
    case VmStatus::ready: return "ready";
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
    : routines_(std::move(routines)), owners_(std::move(owners)), memory_(std::move(memory)), limits_(limits), primitive_(std::move(primitive)) {
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
const Bhav* Vm::resolve(RoutineKey key, CodeOwner owner) const {
    if(owner!=0 && owners_.find(owner)==owners_.end())return nullptr;
    const auto& bank=(key.scope==RoutineScope::global || owner==0)?routines_:owners_.at(owner);
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
    if (mode != VmCallReturnMode::ordinary && mode != VmCallReturnMode::interaction_next_tick)
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
    if (routine.version != 0x8002 || !routine.metadata)
        throw VmError("execution requires BHAV 8002 metadata");
    if (routine.instructions.empty()) throw VmError("empty routine has no entry instruction");
    for (const auto& instruction : routine.instructions)
        for (const auto target : {instruction.on_true, instruction.on_false})
            if (target < 0xFD && target >= routine.instructions.size())
                throw VmError("branch outside routine");
    if (stack_.size() >= limits_.stack_depth) throw VmError("stack depth limit");
    const auto arg_count = std::max<std::size_t>(4, routine.metadata->arguments);
    const auto local_count = std::max<std::size_t>(routine.metadata->locals, routine.metadata->arguments);
    if (arg_count > limits_.frame_variables || local_count > limits_.frame_variables || args.size() > arg_count)
        throw VmError("frame variable limit or too many arguments");
    VmFrame frame{key, 0, context, std::vector<std::int16_t>(arg_count),
                  std::vector<std::int16_t>(local_count), mode, ticket};
    std::copy(args.begin(), args.end(), frame.arguments.begin());
    stack_.push_back(std::move(frame));
}
VmStatus Vm::start(RoutineKey key, VmContext context, const std::vector<std::int16_t>& args) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter start/run");
    if (run_generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::logic_error("VM run generation overflow");
    ++run_generation_;
    stack_.clear(); temps_.fill(0); diagnostic_.clear(); steps_ = 0;
    pending_.reset();
    interaction_return_.reset();
    status_ = VmStatus::running;
    try { push(key, context, args); }
    catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
    return status_;
}
std::optional<VmMemoryAddress> resolve_person_data_address(std::uint8_t scope,
    std::int16_t index,const std::array<std::int16_t,20>& temps) {
    if(scope!=30&&scope!=31)return VmMemoryAddress{scope,index};
    if(index<0||static_cast<std::size_t>(index)>=temps.size())return {};
    return VmMemoryAddress{static_cast<std::uint8_t>(scope==30?18:19),temps[static_cast<std::size_t>(index)]};
}
std::int16_t Vm::read(std::uint8_t scope, std::int16_t data) {
    auto& frame = stack_.back();
    switch (scope) {
    case 7: return data;
    case 8:
        if (data < 0 || static_cast<std::size_t>(data) >= temps_.size()) throw VmError("temp index outside registers");
        return temps_[static_cast<std::size_t>(data)];
    case 9: return indexed(frame.arguments, data);
    case 10: return frame.context.stack_object;
    case 25: return indexed(frame.locals, data);
    default:
        if (const auto address=resolve_person_data_address(scope,data,temps_);address&&memory_.read) {
            const auto value = memory_.read(frame, address->scope, address->index);
            if (value) return *value;
        }
        throw VmError("unsupported read scope " + std::to_string(scope) + " index " + std::to_string(data));
    }
}
void Vm::write(std::uint8_t scope, std::int16_t data, std::int32_t value) {
    auto& frame = stack_.back();
    const auto truncated = word(static_cast<std::uint32_t>(value));
    switch (scope) {
    case 8:
        if (data < 0 || static_cast<std::size_t>(data) >= temps_.size()) throw VmError("temp index outside registers");
        temps_[static_cast<std::size_t>(data)] = truncated; return;
    case 9: indexed(frame.arguments, data) = truncated; return;
    case 10: frame.context.stack_object = truncated; return;
    case 25: indexed(frame.locals, data) = truncated; return;
    default:
        if (const auto address=resolve_person_data_address(scope,data,temps_);address&&memory_.write&&
            memory_.write(frame,address->scope,address->index,truncated)) return;
        throw VmError("unsupported write scope " + std::to_string(scope) + " index " + std::to_string(data));
    }
}
bool Vm::expression(const Instruction& i) {
    const auto lhs_data = operand_word(i, 0), rhs_data = operand_word(i, 2);
    const auto op = i.operands[5], lhs_scope = i.operands[6], rhs_scope = i.operands[7];
    // Byte4 is preserved in traces. The reference TS1 handler does not use it.
    if (op > 19) throw VmError("unsupported expression operator " + std::to_string(op));
    if (op == 5) { write(lhs_scope, lhs_data, read(rhs_scope, rhs_data)); return true; }
    std::int32_t lhs = read(lhs_scope, lhs_data);
    if (op == 11 || op == 17) {
        lhs += op == 11 ? 1 : -1;
        write(lhs_scope, lhs_data, lhs);
        const auto rhs = read(rhs_scope, rhs_data); // Read after mutation, including aliases.
        return op == 11 ? lhs < rhs : lhs > rhs;
    }
    const std::int32_t rhs = read(rhs_scope, rhs_data);
    switch (op) {
    case 0: return lhs > rhs;
    case 1: return lhs < rhs;
    case 2: return lhs == rhs;
    case 3: lhs += rhs; break;
    case 4: lhs -= rhs; break;
    case 6: lhs *= rhs; break;
    case 7:
        if (rhs == 0) throw VmError("division by zero: original semantics unverified");
        lhs /= rhs; break;
    case 8: case 9: case 10: {
        if (rhs < 1 || rhs > 16) throw VmError("flag index outside supported 16-bit range");
        const auto mask = std::uint32_t{1} << (rhs - 1);
        const auto bits = static_cast<std::uint32_t>(lhs) & 65535;
        if (op == 8) return (bits & mask) != 0;
        lhs = word(op == 9 ? bits | mask : bits & ~mask); break;
    }
    case 12:
        if (rhs == 0) throw VmError("modulo by zero: original semantics unverified");
        lhs = (lhs % rhs + rhs) % rhs; break;
    case 13: lhs &= rhs; break;
    case 14: return lhs >= rhs;
    case 15: return lhs <= rhs;
    case 16: return lhs != rhs;
    case 18: lhs |= rhs; break; // TS1 OR, not the TSO list operation.
    case 19: lhs ^= rhs; break;
    default: throw VmError("unsupported expression operator");
    }
    write(lhs_scope, lhs_data, lhs);
    return true;
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
                if (!found || !found->metadata) {
                    push(key, frame.context, {}); // Produce the specific missing/metadata diagnostic.
                    continue;
                }
                const auto count = std::max<std::size_t>(4, found->metadata->arguments);
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
                    if(outcome.write && !frame_output(stack_.back(),*outcome.write))
                        throw VmError("primitive call output outside parent frame");
                    {
                    const auto parent_index=stack_.size()-1;
                    push(outcome.call->key,outcome.call->context,outcome.call->arguments,
                         outcome.call->return_mode, dispatch.ticket);
                    if(outcome.write)*frame_output(stack_[parent_index],*outcome.write)=outcome.write->value;
                    }
                    break;
                case PrimitiveDisposition::goto_true:
                case PrimitiveDisposition::goto_false:
                    // Validate all frame/temp/context outputs before publishing any.
                    if (outcome.write && !frame_output(stack_.back(), *outcome.write))
                        throw VmError("primitive frame output bank/index outside frame");
                    if (outcome.write) {
                        auto* destination = frame_output(stack_.back(), *outcome.write);
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
                        auto* destination = frame_output(stack_.back(), *outcome.write);
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
bool Vm::update_pending(std::uint64_t ticket, VmFrameWrite output) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter pending update");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    auto* destination = frame_output(stack_.back(), output);
    if (!destination) return false;
    *destination = output.value;
    return true;
}
bool Vm::complete(std::uint64_t ticket, bool result, std::optional<VmFrameWrite> output) {
    if (executing_) throw std::logic_error("VM callbacks must not reenter completion");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    if (output) {
        auto* destination = frame_output(stack_.back(), *output);
        if (!destination) return false;
        *destination = output->value;
    }
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
        auto* destination = frame_output(stack_.back(), *output);
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
} // namespace odh
