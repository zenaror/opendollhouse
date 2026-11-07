#include "odh/vm.hpp"
#include "odh_expression.h"

#include <algorithm>
#include <set>
#include <exception>
#include <limits>
#include <type_traits>

namespace odh {
bool vm_context_equal(const VmContext&a,const VmContext&b) noexcept {
    return a.caller==b.caller&&a.callee==b.callee&&a.stack_object==b.stack_object&&a.code_owner==b.code_owner&&
        a.entity_references_bound==b.entity_references_bound&&a.caller_generation==b.caller_generation&&
        a.callee_generation==b.callee_generation&&a.stack_object_generation==b.stack_object_generation;
}
bool vm_context_references_valid(const VmContext& c) noexcept {
    if(!c.entity_references_bound)return !c.caller_generation&&!c.callee_generation&&!c.stack_object_generation;
    return (!c.caller_generation||c.caller>0)&&(!c.callee_generation||c.callee>0)&&(!c.stack_object_generation||c.stack_object>0);
}
WorldEntityReference vm_context_reference(const VmContext& c,VmEntityRole role) noexcept {
    switch(role){case VmEntityRole::caller:return {c.caller,c.entity_references_bound?c.caller_generation:0};
    case VmEntityRole::callee:return {c.callee,c.entity_references_bound?c.callee_generation:0};
    case VmEntityRole::stack_object:return {c.stack_object,c.entity_references_bound?c.stack_object_generation:0};}return {};
}
bool vm_context_set_reference(VmContext& c,VmEntityRole role,WorldEntityReference r) noexcept {
    if(!c.entity_references_bound||(r.generation&&r.id<=0))return false;
    switch(role){case VmEntityRole::caller:c.caller=r.id;c.caller_generation=r.generation;break;
    case VmEntityRole::callee:c.callee=r.id;c.callee_generation=r.generation;break;
    case VmEntityRole::stack_object:c.stack_object=r.id;c.stack_object_generation=r.generation;break;}return true;
}
bool vm_context_copy_reference(VmContext& dst,VmEntityRole to,const VmContext& src,VmEntityRole from) noexcept {
    return src.entity_references_bound&&vm_context_set_reference(dst,to,vm_context_reference(src,from));
}
bool vm_frame_equal(const VmFrame&a,const VmFrame&b) noexcept {
    return a.key.scope==b.key.scope&&a.key.id==b.key.id&&a.pc==b.pc&&vm_context_equal(a.context,b.context)&&
        a.arguments==b.arguments&&a.locals==b.locals&&a.return_mode==b.return_mode&&
        a.invocation_ticket==b.invocation_ticket&&a.exact_arguments==b.exact_arguments&&a.action_tree==b.action_tree;
}

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
SharedRoutineOwners share_owner_programs(RoutineOwners owners) {
    SharedRoutineOwners programs;
    for (auto& owner : owners)
        programs.emplace(owner.first, std::make_shared<const VmProgram>(std::move(owner.second)));
    return programs;
}
} // namespace

RoutineKey routine_key(std::uint16_t id) {
    return {id >= 8192 ? RoutineScope::semiglobal :
            id >= 4096 ? RoutineScope::private_object : RoutineScope::global, id};
}
bool named_routine_scope(RoutineScope scope) noexcept {
    return scope==RoutineScope::named_private||scope==RoutineScope::named_semiglobal;
}
RoutineKey named_routine_key(std::uint16_t id,bool linked){
    return {linked?RoutineScope::named_semiglobal:RoutineScope::named_private,id};
}
bool routine_key_valid(RoutineKey key) noexcept {
    return named_routine_scope(key.scope)||(key.scope>=RoutineScope::global&&key.scope<=RoutineScope::semiglobal&&key.scope==routine_key(key.id).scope);
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
    case VmStatus::next_tick: return "next_tick";
    }
    return "invalid_status";
}
Vm::Vm(RoutineSet routines, VmMemory memory, VmLimits limits, VmPrimitive primitive, RoutineOwners owners)
    : Vm(std::make_shared<const VmProgram>(std::move(routines)),ShareVmProgramTag{},std::move(memory),limits,
         std::move(primitive),std::move(owners)) {}
Vm::Vm(std::shared_ptr<const VmProgram> program, ShareVmProgramTag, VmMemory memory, VmLimits limits,
       VmPrimitive primitive, RoutineOwners owners)
    : Vm(std::move(program), ShareVmProgramTag{}, std::move(memory), limits, std::move(primitive),
         share_owner_programs(std::move(owners)), ShareVmOwnerProgramsTag{}) {}
Vm::Vm(std::shared_ptr<const VmProgram> program, ShareVmProgramTag, VmMemory memory, VmLimits limits,
       VmPrimitive primitive, SharedRoutineOwners owners, ShareVmOwnerProgramsTag)
    : program_(std::move(program)), owners_(std::move(owners)), memory_(std::move(memory)),
      limits_(limits), primitive_(std::move(primitive)) {
    if(!program_)throw std::invalid_argument("VM program is null");
    if(!limits_.code_owners || owners_.size()>=limits_.code_owners)
        throw std::invalid_argument("VM code owner limit");
    for(const auto& owner:owners_) {
        if(owner.first==0)throw std::invalid_argument("default code owner zero cannot be replaced");
        if(!owner.second)throw std::invalid_argument("VM owner program is null");
        for(const auto& entry:owner.second->routines_) {
            if(entry.first.scope==RoutineScope::global || !routine_key_valid(entry.first))
                throw std::invalid_argument("additional owner contains invalid routine scope");
        }
    }
    memory_=with_advertisement_memory(std::move(memory_),advertisement_changes_,
        [this](const VmFrame& frame,std::uint8_t type,std::uint16_t index)->std::optional<std::int16_t>{
            if((check_active_&&(!named_stack_depth_||!named_queue_inherits_parent_index_))||!advertisement_fallback_)return std::int16_t{0};
            return advertisement_fallback_(frame,type,index);
        });
}
bool Vm::install_owner(CodeOwner owner, RoutineSet routines) {
    reject_storage_proof_mutation();
    if (executing_ || owner == 0 || routines.empty() || owners_.find(owner) != owners_.end() ||
        owners_.size() >= limits_.code_owners - 1) return false;
    for (const auto& entry : routines) {
        if (entry.first.scope == RoutineScope::global || !routine_key_valid(entry.first))
            return false;
    }
    owners_.emplace(owner, std::make_shared<const VmProgram>(std::move(routines)));
    return true;
}
bool Vm::install_shared_owner(CodeOwner owner, std::shared_ptr<const VmProgram> program) {
    reject_storage_proof_mutation();
    if(executing_)return false;
    return admit_shared_owner(owner,std::move(program));
}
bool Vm::release_idle_owner(CodeOwner owner,const std::shared_ptr<const VmProgram>& registered_program) {
    reject_storage_proof_mutation();
    if(!owner||!registered_program||!owner_resolver_||executing_||check_active_||
       entity_thread_stack_||named_stack_depth_||preparing_primitive_call_)return false;
    const auto installed=owners_.find(owner);
    if(installed==owners_.end()||installed->second!=registered_program)return false;
    for(const auto& frame:stack_)if(frame.context.code_owner==owner)return false;
    if(pending_&&pending_->context.code_owner==owner)return false;
    for(const auto& pending:suspended_pending_)if(pending.context.code_owner==owner)return false;
    for(const auto& parent:immediate_continuations_)
        if(parent.parent_pending&&parent.parent_pending->context.code_owner==owner)return false;
    if((interaction_return_&&interaction_return_->code_owner==owner)||
       (external_frame_return_&&external_frame_return_->code_owner==owner)||
       (immediate_return_&&immediate_return_->code_owner==owner))return false;
    owners_.erase(installed);
    return true;
}
bool Vm::admit_shared_owner(CodeOwner owner,std::shared_ptr<const VmProgram> program) {
    if (owner == 0 || !program || program->routines_.empty() || owners_.find(owner) != owners_.end() ||
        owners_.size() >= limits_.code_owners - 1) return false;
    for (const auto& entry : program->routines_) {
        if (entry.first.scope == RoutineScope::global || !routine_key_valid(entry.first))
            return false;
    }
    owners_.emplace(owner, std::move(program));
    return true;
}
bool Vm::set_advertisement_fallback(AdvertisementFallback fallback){
    reject_storage_proof_mutation();
    if(executing_||check_active_||external_check_active_)return false;
    advertisement_fallback_=std::move(fallback);return true;
}
bool Vm::set_owner_resolver(OwnerResolver resolver) {
    reject_storage_proof_mutation();
    if(executing_||check_active_)return false;
    owner_resolver_=std::move(resolver);
    return true;
}
void Vm::inherit_disposable_resource_context(Vm& clone) const {
    clone.owner_resolver_=owner_resolver_;
}
void Vm::resolve_missing_owner(CodeOwner owner) {
    if(owner==0 || owners_.find(owner)!=owners_.end())return;
    if(!owner_resolver_)throw VmError("code owner not supplied: "+std::to_string(owner));
    if(owners_.size()>=limits_.code_owners-1)
        throw VmError("code owner resolver capacity exceeded: "+std::to_string(owner));
    std::shared_ptr<const VmProgram> program;
    try {
        // start() can push outside run(). Give resource callbacks the same
        // reentry protection as instruction dispatch, restoring its prior phase.
        struct Guard {
            bool& executing;bool previous;
            explicit Guard(bool& value):executing(value),previous(value){executing=true;}
            ~Guard(){executing=previous;}
        } guard(executing_);
        program=owner_resolver_(owner);
    } catch(const std::exception& error) {
        throw VmError("code owner resolver failed for "+std::to_string(owner)+": "+error.what());
    } catch(...) {
        throw VmError("code owner resolver failed for "+std::to_string(owner)+": non-standard exception");
    }
    try {
        if(!admit_shared_owner(owner,std::move(program)))
            throw VmError("code owner resolver supplied an invalid bank: "+std::to_string(owner));
    } catch(const VmError&) {throw;}
    catch(const std::exception& error) {
        throw VmError("code owner resolver admission failed for "+std::to_string(owner)+": "+error.what());
    }
}
const Bhav* Vm::resolve(RoutineKey key, CodeOwner owner) const {
    if(!routine_key_valid(key)||(named_routine_scope(key.scope)&&!owner))return nullptr;
    if(owner!=0 && owners_.find(owner)==owners_.end())return nullptr;
    const auto& bank=(key.scope==RoutineScope::global || owner==0)?program_->routines_:owners_.at(owner)->routines_;
    const auto found=bank.find(key);
    return found==bank.end()?nullptr:&found->second;
}
const Bhav& Vm::current_routine(const VmFrame& frame) const {
    const auto* routine=resolve(frame.key,frame.context.code_owner);
    if(!routine)throw VmError("frame code-owner routine unavailable");
    return *routine;
}
bool Vm::set_active_primitive_stack_object(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch,std::int16_t value){
    reject_storage_proof_mutation();
    if(instruction.opcode!=30||!matches_active_primitive(frame,instruction,dispatch))return false;
    return assign_stack_object(stack_.back().context,value);
}
bool Vm::matches_active_primitive(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch) const noexcept {
    return !dispatch.check_mode&&matches_active_query(frame,instruction,dispatch);
}
bool Vm::matches_active_query(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch) const noexcept {
    if(!executing_||!active_primitive_dispatch_||!active_primitive_instruction_||
       dispatch.origin!=this||active_primitive_dispatch_->origin!=this||
       dispatch.check_mode!=active_primitive_dispatch_->check_mode||!dispatch.ticket||
       dispatch.ticket!=active_primitive_dispatch_->ticket||stack_.empty())return false;
    const auto same_instruction=[](const Instruction& a,const Instruction& b){
        return a.opcode==b.opcode&&a.on_true==b.on_true&&a.on_false==b.on_false&&
            a.operands==b.operands;
    };
    const auto& top=stack_.back();
    if(!vm_context_equal(top.context,frame.context)||top.action_tree!=frame.action_tree||top.key.scope!=frame.key.scope||top.key.id!=frame.key.id||top.pc!=frame.pc||
       top.context.caller!=frame.context.caller||top.context.callee!=frame.context.callee||
       top.context.stack_object!=frame.context.stack_object||top.context.code_owner!=frame.context.code_owner||
       top.arguments!=frame.arguments||top.locals!=frame.locals||top.return_mode!=frame.return_mode||
       top.invocation_ticket!=frame.invocation_ticket||top.exact_arguments!=frame.exact_arguments||
       !same_instruction(instruction,*active_primitive_instruction_))return false;
    const auto* routine=resolve(top.key,top.context.code_owner);
    return routine&&top.pc<routine->instructions.size()&&same_instruction(instruction,routine->instructions[top.pc]);
}

bool Vm::matches_active_call_commit(const VmFrame& frame,const Instruction& instruction,
        const VmDispatch& dispatch,std::size_t depth) const noexcept {
    if(!executing_||!active_call_commit_dispatch_||!active_call_commit_instruction_||!depth||
       depth!=active_call_commit_parent_depth_||stack_.size()!=depth+1||dispatch.origin!=this||
       dispatch.check_mode||active_call_commit_dispatch_->check_mode||!dispatch.ticket||
       dispatch.ticket!=active_call_commit_dispatch_->ticket)return false;
    const auto& parent=stack_[depth-1];const auto& operation=*active_call_commit_instruction_;
    if(!vm_context_equal(parent.context,frame.context)||parent.action_tree!=frame.action_tree||parent.key.scope!=frame.key.scope||parent.key.id!=frame.key.id||parent.pc!=frame.pc||
       parent.context.caller!=frame.context.caller||parent.context.callee!=frame.context.callee||
       parent.context.stack_object!=frame.context.stack_object||parent.context.code_owner!=frame.context.code_owner||
       parent.arguments!=frame.arguments||parent.locals!=frame.locals||parent.return_mode!=frame.return_mode||
       parent.invocation_ticket!=frame.invocation_ticket||parent.exact_arguments!=frame.exact_arguments||
       operation.opcode!=instruction.opcode||operation.on_true!=instruction.on_true||
       operation.on_false!=instruction.on_false||operation.operands!=instruction.operands)return false;
    const auto* routine=resolve(parent.key,parent.context.code_owner);
    if(!routine||parent.pc>=routine->instructions.size())return false;
    const auto& authored=routine->instructions[parent.pc];
    return authored.opcode==operation.opcode&&authored.on_true==operation.on_true&&
        authored.on_false==operation.on_false&&authored.operands==operation.operands;
}

void Vm::push(RoutineKey key, VmContext context, const std::vector<std::int16_t>& args,
              VmCallReturnMode mode, std::uint64_t ticket, bool exact_arguments,std::optional<bool> action_tree_override) {
    if (mode != VmCallReturnMode::ordinary && mode != VmCallReturnMode::interaction_next_tick &&
        mode != VmCallReturnMode::external_frame_next_tick && mode != VmCallReturnMode::immediate)
        throw VmError("invalid child return mode");
    if(!bind_context_live(context))throw VmError("frame entity binding could not resolve the explicit numeric entry");
    resolve_missing_owner(context.code_owner);
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
    const auto arg_count = exact_arguments?args.size():std::max<std::size_t>(4, metadata->arguments);
    const auto local_count = std::max<std::size_t>(metadata->locals, metadata->arguments);
    if (arg_count > limits_.frame_variables || local_count > limits_.frame_variables || args.size() > arg_count)
        throw VmError("frame variable limit or too many arguments");
    VmFrame frame{key, 0, context, std::vector<std::int16_t>(arg_count),
                  std::vector<std::int16_t>(local_count), mode, ticket, exact_arguments};
    frame.action_tree=action_tree_override.value_or(!stack_.empty()&&stack_.back().action_tree);
    std::copy(args.begin(), args.end(), frame.arguments.begin());
    stack_.push_back(std::move(frame));
}
bool Vm::can_accept_call(const VmCall& call) const {
    if(call.return_mode!=VmCallReturnMode::ordinary&&
       call.return_mode!=VmCallReturnMode::interaction_next_tick&&
       call.return_mode!=VmCallReturnMode::immediate)return false;
    if(call.context.code_owner!=0&&owners_.find(call.context.code_owner)==owners_.end())return false;
    const auto* routine=resolve(call.key,call.context.code_owner);
    if(!routine)return false;
    const auto metadata=execution_metadata(*routine);
    if(!metadata||routine->instructions.empty()||stack_.size()>=limits_.stack_depth)return false;
    for(const auto& instruction:routine->instructions)
        for(const auto target:{instruction.on_true,instruction.on_false})
            if(target<0xFD&&target>=routine->instructions.size())return false;
    const auto arg_count=call.exact_arguments?call.arguments.size():std::max<std::size_t>(4,metadata->arguments);
    const auto local_count=std::max<std::size_t>(metadata->locals,metadata->arguments);
    return arg_count<=limits_.frame_variables&&local_count<=limits_.frame_variables&&
        call.arguments.size()<=arg_count;
}
bool Vm::prepare_primitive_owner(const VmFrame& frame,const Instruction& instruction,
        const VmDispatch& dispatch,CodeOwner owner) {
    reject_storage_proof_mutation();
    const auto authentic=[&]{return instruction.opcode==28?matches_active_query(frame,instruction,dispatch):matches_active_primitive(frame,instruction,dispatch);};
    if(preparing_primitive_call_||!authentic())return false;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(preparing_primitive_call_);
    resolve_missing_owner(owner);
    return authentic()&&
        (owner==0||owners_.find(owner)!=owners_.end());
}
bool Vm::prepare_primitive_call(const VmFrame& frame,const Instruction& instruction,
        const VmDispatch& dispatch,const VmCall& call) {
    if(preparing_primitive_call_||!matches_active_primitive(frame,instruction,dispatch))return false;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(preparing_primitive_call_);
    resolve_missing_owner(call.context.code_owner);
    return matches_active_primitive(frame,instruction,dispatch)&&can_accept_call(call);
}

VmSnapshot Vm::snapshot() const {
    if (executing_) throw std::logic_error("VM callbacks must not capture running state");
    return {stack_, register_temps(), register_xl(), status_, diagnostic_, steps_, pending_, suspended_pending_,
            interaction_return_, external_frame_return_, ticket_sequence_, run_generation_,
            immediate_continuations_,immediate_return_,action_tree_known_,advertisement_changes_};
}
bool Vm::restore_snapshot(const VmSnapshot& state,VmSavedRoutinePolicy policy) {
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter state restore");
    if(policy!=VmSavedRoutinePolicy::require_present && policy!=VmSavedRoutinePolicy::defer_missing_fault &&
        policy!=VmSavedRoutinePolicy::defer_missing_or_pc_fault)return false;
    if (status_ != VmStatus::ready || !stack_.empty() || steps_ || run_generation_ ||
        ticket_sequence_ || pending_ || !suspended_pending_.empty() || interaction_return_ ||
        external_frame_return_ || !immediate_continuations_.empty() || immediate_return_ || !diagnostic_.empty() ||
        std::any_of(register_temps().begin(), register_temps().end(), [](auto value) { return value != 0; })) return false;
    if (std::any_of(register_xl().begin(), register_xl().end(), [](auto value) { return value != 0; })) return false;
    if (state.stack.size() > limits_.stack_depth ||
        state.suspended_pending.size() > state.stack.size()||
        state.immediate_continuations.size()>limits_.stack_depth) return false;
    const auto valid_context=[](const VmContext& context){
        if(!context.entity_references_bound)return !context.caller_generation&&!context.callee_generation&&!context.stack_object_generation;
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
            const auto ref=vm_context_reference(context,role);if(ref.generation&&ref.id<=0)return false;}
        return true;
    };
    for(const auto& frame:state.stack)if(!valid_context(frame.context)||(!state.action_tree_known&&frame.action_tree))return false;
    if(state.pending&&!valid_context(state.pending->context))return false;
    for(const auto& pending:state.suspended_pending)if(!valid_context(pending.context))return false;
    for(const auto& continuation:state.immediate_continuations)if(continuation.parent_pending&&!valid_context(continuation.parent_pending->context))return false;
    const auto owner_valid = [&](CodeOwner owner) {
        return owner == 0 || owners_.find(owner) != owners_.end();
    };
    const auto ticket_valid = [&](std::uint64_t ticket) {
        return ticket != 0 && ticket <= state.ticket_sequence;
    };
    // External children deliberately share their ticket with the parent wait
    // retained in suspended_pending.
    std::vector<std::uint64_t> tickets;
    std::size_t external_count = 0,immediate_count=0;
    std::uint64_t last_ticket = 0;
    for (std::size_t depth = 0; depth < state.stack.size(); ++depth) {
        const auto& frame = state.stack[depth];
        if (!routine_key_valid(frame.key) || (named_routine_scope(frame.key.scope)&&!frame.context.code_owner) || !owner_valid(frame.context.code_owner)) return false;
        const auto* routine = resolve(frame.key, frame.context.code_owner);
        if(frame.arguments.size()>limits_.frame_variables || frame.locals.size()>limits_.frame_variables)return false;
        if(!routine) {
            if(policy==VmSavedRoutinePolicy::require_present || frame.key.id<=255 ||
                frame.return_mode!=VmCallReturnMode::ordinary || frame.invocation_ticket)return false;
        } else {
            const auto metadata=execution_metadata(*routine);
            if(!metadata || routine->instructions.empty() ||
                (frame.pc>=routine->instructions.size() && policy!=VmSavedRoutinePolicy::defer_missing_or_pc_fault))return false;
            for(const auto& instruction:routine->instructions)
                for(const auto target:{instruction.on_true,instruction.on_false})
                    if(target<0xFD && target>=routine->instructions.size())return false;
            if((!frame.exact_arguments&&frame.arguments.size()<std::max<std::size_t>(4,metadata->arguments)) ||
                frame.locals.size()<std::max<std::size_t>(metadata->locals,metadata->arguments))return false;
        }
        if (!depth && (frame.return_mode != VmCallReturnMode::ordinary || frame.invocation_ticket)) return false;
        switch (frame.return_mode) {
        case VmCallReturnMode::ordinary: break;
        case VmCallReturnMode::interaction_next_tick:
            if (!ticket_valid(frame.invocation_ticket)) return false;
            break;
        case VmCallReturnMode::external_frame_next_tick:
            if (!ticket_valid(frame.invocation_ticket) || external_count >= state.suspended_pending.size()) return false;
            if (state.suspended_pending[external_count].ticket != frame.invocation_ticket ||
                state.suspended_pending[external_count].instruction.depth != depth) return false;
            ++external_count;
            break;
        case VmCallReturnMode::immediate:
            if(immediate_count>=state.immediate_continuations.size()||
               state.immediate_continuations[immediate_count].ticket!=frame.invocation_ticket||
               state.immediate_continuations[immediate_count].parent_depth!=depth||
               !depth||frame.context.caller<=0||frame.context.caller!=state.stack[depth-1].context.caller)return false;
            ++immediate_count;
            break;
        default: return false;
        }
        if (frame.invocation_ticket) {
            if (!ticket_valid(frame.invocation_ticket) || (!state.steps&&frame.return_mode!=VmCallReturnMode::immediate) ||
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
        const auto* routine=resolve(frame.key,frame.context.code_owner);
        if(!routine || frame.pc>=routine->instructions.size())return false;
        const auto& instruction = routine->instructions[frame.pc];
        return trace.key.scope == frame.key.scope && trace.key.id == frame.key.id && trace.pc == frame.pc &&
            trace.code_owner == frame.context.code_owner &&
            vm_context_equal(wait.context,frame.context)&&wait.context.caller == frame.context.caller && wait.context.callee == frame.context.callee &&
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
    std::size_t previous_depth=0;std::uint64_t previous_immediate_ticket=0;
    for(const auto& continuation:state.immediate_continuations){
        if(!ticket_valid(continuation.ticket)||continuation.ticket<=previous_immediate_ticket||
           continuation.parent_depth<=previous_depth||continuation.parent_depth>state.stack.size()||
           (continuation.parent_status!=VmStatus::running&&continuation.parent_status!=VmStatus::budget_exhausted&&
            continuation.parent_status!=VmStatus::waiting)||
           (continuation.parent_status==VmStatus::waiting)!=continuation.parent_pending.has_value())return false;
        if(continuation.parent_pending){
            const auto& wait=*continuation.parent_pending;
            if(!valid_wait(wait)||wait.instruction.depth!=continuation.parent_depth||
               wait.ticket>=continuation.ticket||wait.ticket<=previous_immediate_ticket)return false;
            for(std::size_t depth=0;depth<continuation.parent_depth;++depth)
                if(state.stack[depth].invocation_ticket>=wait.ticket)return false;
            tickets.push_back(wait.ticket);
        }
        previous_depth=continuation.parent_depth;previous_immediate_ticket=continuation.ticket;
    }
    if(state.immediate_return){
        if(state.immediate_continuations.empty()||immediate_count+1!=state.immediate_continuations.size()||
           state.immediate_return->ticket!=state.immediate_continuations.back().ticket||
           state.stack.size()!=state.immediate_continuations.back().parent_depth||state.pending||
           state.interaction_return)return false;
    }else if(immediate_count!=state.immediate_continuations.size())return false;
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
    if(state.immediate_return){
        if(!valid_return(*state.immediate_return))return false;
        tickets.push_back(state.immediate_return->ticket);
    }
    if (state.interaction_return) {
        if (!valid_return(*state.interaction_return) || state.stack.empty()) return false;
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
    case VmStatus::next_tick:
        if (state.stack.empty()) return false;
        break;
    case VmStatus::waiting:
        if (!state.pending) return false;
        break;
    case VmStatus::yielded:
        if (unsigned(bool(state.interaction_return))+unsigned(bool(state.external_frame_return))+
            unsigned(bool(state.immediate_return))!=1) return false;
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
    if(state.immediate_return&&state.status!=VmStatus::yielded)return false;
    if(!state.immediate_continuations.empty()&&
       (state.status==VmStatus::ready||state.status==VmStatus::dormant||
        state.status==VmStatus::returned_true||state.status==VmStatus::returned_false))return false;
    if(!advertisement_changes_valid(state.advertisement_changes))return false;
    // Every allocation finishes before any observable destination value changes.
    auto candidate = state;
    static_assert(std::is_nothrow_swappable_v<decltype(pending_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(interaction_return_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(external_frame_return_)>);
    stack_.swap(candidate.stack);
    diagnostic_.swap(candidate.diagnostic);
    suspended_pending_.swap(candidate.suspended_pending);
    register_temps() = candidate.temps;
    register_xl() = candidate.temp_xl;
    status_ = candidate.status;
    action_tree_known_=candidate.action_tree_known;
    advertisement_changes_.swap(candidate.advertisement_changes);
    steps_ = candidate.steps;
    pending_.swap(candidate.pending);
    interaction_return_.swap(candidate.interaction_return);
    external_frame_return_.swap(candidate.external_frame_return);
    immediate_continuations_.swap(candidate.immediate_continuations);
    immediate_return_.swap(candidate.immediate_return);
    ticket_sequence_ = candidate.ticket_sequence;
    run_generation_ = candidate.run_generation;
    return true;
}

bool Vm::bind_context_live(VmContext& context) const {
    if(!vm_context_references_valid(context))return false;
    if(context.entity_references_bound||!memory_.resolve_live_entity)return true;
    auto bound=context;bound.entity_references_bound=true;
    try{for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
        auto reference=vm_context_reference(context,role);
        const auto live=reference.id>0?memory_.resolve_live_entity(reference.id):std::nullopt;
        if(live&&(live->id!=reference.id||!live->generation))return false;
        reference.generation=live?live->generation:0;
        if(!vm_context_set_reference(bound,role,reference))return false;
    }}catch(...){return false;}
    context=std::move(bound);return true;
}
bool Vm::assign_stack_object(VmContext& context,std::int16_t value){
    if(!memory_.resolve_live_entity){if(context.entity_references_bound)return false;context.stack_object=value;return true;}
    auto bound=context;if(!bind_context_live(bound))return false;
    const auto live=value>0?memory_.resolve_live_entity(value):std::nullopt;
    if(live&&(live->id!=value||!live->generation))return false;
    if(!vm_context_set_reference(bound,VmEntityRole::stack_object,{value,live?live->generation:0}))return false;
    context=std::move(bound);return true;
}
bool Vm::bind_legacy_entity_references(){
    reject_storage_proof_mutation();if(executing_||check_active_||!memory_.resolve_live_entity)return false;
    auto frames=stack_;auto pending=pending_;auto suspended=suspended_pending_;auto immediate=immediate_continuations_;
    const auto upgrade=[&](VmContext& context){
        if(context.entity_references_bound)return true;
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
            const auto reference=vm_context_reference(context,role);
            if(reference.id>0&&!memory_.resolve_live_entity(reference.id))return false;
        }
        return bind_context_live(context);
    };
    for(auto& frame:frames)if(!upgrade(frame.context))return false;
    if(pending&&!upgrade(pending->context))return false;
    for(auto& p:suspended)if(!upgrade(p.context))return false;
    for(auto& continuation:immediate)if(continuation.parent_pending&&!upgrade(continuation.parent_pending->context))return false;
    stack_.swap(frames);pending_=std::move(pending);suspended_pending_.swap(suspended);immediate_continuations_.swap(immediate);return true;
}
std::vector<WorldEntityReference> Vm::strong_entity_references() const {
    std::set<WorldEntityReference> references;
    const auto add=[&](const VmContext& context){if(!context.entity_references_bound)return;
        for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
            const auto ref=vm_context_reference(context,role);if(ref.id>0&&ref.generation)references.insert(ref);}};
    for(const auto& frame:stack_)add(frame.context);
    if(entity_thread_stack_)for(const auto& frame:*entity_thread_stack_)add(frame.context);
    if(pending_)add(pending_->context);
    for(const auto& pending:suspended_pending_)add(pending.context);
    for(const auto& continuation:immediate_continuations_)if(continuation.parent_pending)add(continuation.parent_pending->context);
    return {references.begin(),references.end()};
}
bool Vm::references_entity(WorldEntityReference reference) const {
    const auto references=strong_entity_references();return std::binary_search(references.begin(),references.end(),reference);
}
VmStatus Vm::start_after_reset(RoutineKey key,VmContext context,const std::vector<std::int16_t>& arguments,bool exact){
    const auto temps=register_temps();const auto xl=register_xl();const auto result=start(key,context,arguments,exact);
    register_temps()=temps;register_xl()=xl;return result;
}
bool Vm::reset_execution_state() {
    reject_storage_proof_mutation();
    if (executing_ || check_active_ || run_generation_ == std::numeric_limits<std::uint64_t>::max())
        return false;
    ++run_generation_;
    stack_.clear();
    pending_.reset();
    suspended_pending_.clear();
    interaction_return_.reset();
    external_frame_return_.reset();
    immediate_continuations_.clear();immediate_return_.reset();
    diagnostic_.clear();
    steps_ = 0;
    status_ = VmStatus::returned_false;
    return true;
}
bool Vm::source_recovery_pop_to_depth(std::size_t depth) {
    reject_storage_proof_mutation();
    if(executing_ || check_active_ || entity_thread_stack_ || depth>stack_.size() ||
       pending_ || !suspended_pending_.empty() || interaction_return_ || external_frame_return_ ||
       !immediate_continuations_.empty() || immediate_return_)return false;
    for(std::size_t i=depth;i<stack_.size();++i)
        if(stack_[i].return_mode==VmCallReturnMode::external_frame_next_tick ||
           stack_[i].return_mode==VmCallReturnMode::immediate)return false;
    stack_.resize(depth);
    if(stack_.empty())status_=VmStatus::returned_false;
    return true;
}

VmStatus Vm::start(RoutineKey key, VmContext context, const std::vector<std::int16_t>& args, bool exact_arguments) {
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter start/run");
    if (run_generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::logic_error("VM run generation overflow");
    ++run_generation_;
    action_tree_known_=true;
    stack_.clear(); register_temps().fill(0); register_xl().fill(0); diagnostic_.clear(); steps_ = 0;
    pending_.reset();
    suspended_pending_.clear();
    interaction_return_.reset();
    external_frame_return_.reset();
    immediate_continuations_.clear();immediate_return_.reset();
    status_ = VmStatus::running;
    try { push(key, context, args,VmCallReturnMode::ordinary,0,exact_arguments); }
    catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
    return status_;
}
std::vector<VmFrame> Vm::prepare_dormant_frames(std::vector<VmFrame> frames,VmSavedRoutinePolicy policy) const {
    if (executing_) throw std::logic_error("VM callbacks must not reenter state restore");
    if(policy!=VmSavedRoutinePolicy::require_present && policy!=VmSavedRoutinePolicy::defer_missing_fault &&
        policy!=VmSavedRoutinePolicy::defer_missing_or_pc_fault)
        throw std::invalid_argument("invalid saved routine admission policy");
    if (frames.empty() || frames.size() > limits_.stack_depth)
        throw std::invalid_argument("restored VM stack is empty or exceeds stack limit");
    if (run_generation_ == std::numeric_limits<std::uint64_t>::max())
        throw std::logic_error("VM run generation overflow");

    for (auto& frame : frames) {
        if (!routine_key_valid(frame.key)||(named_routine_scope(frame.key.scope)&&!frame.context.code_owner))
            throw std::invalid_argument("restored frame has invalid routine scope or named owner");
        if (frame.context.code_owner != 0 && owners_.find(frame.context.code_owner) == owners_.end())
            throw std::invalid_argument("restored frame code owner is unavailable");
        const auto* routine = resolve(frame.key, frame.context.code_owner);
        if(!routine) {
            if(policy==VmSavedRoutinePolicy::require_present || frame.key.id<=255)
                throw std::invalid_argument("restored frame routine is unavailable");
            if(frame.return_mode!=VmCallReturnMode::ordinary || frame.invocation_ticket)
                throw std::invalid_argument("missing restored frame has runtime-only return state");
            if(frame.arguments.size()>limits_.frame_variables || frame.locals.size()>limits_.frame_variables)
                throw std::invalid_argument("missing restored frame variables exceed limit");
            continue; // Preserve all unavailable-routine frame values without inventing metadata.
        }
        const auto metadata = execution_metadata(*routine);
        if (!metadata || routine->instructions.empty())
            throw std::invalid_argument("restored frame routine has unsupported metadata or no instructions");
        if (frame.pc >= routine->instructions.size() && policy!=VmSavedRoutinePolicy::defer_missing_or_pc_fault)
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
        if(!frame.exact_arguments)frame.arguments.resize(std::max(frame.arguments.size(), argument_count), 0);
        frame.locals.resize(std::max(frame.locals.size(), local_count), 0);
    }

    return frames;
}
void Vm::restore_dormant_state(std::vector<VmFrame> frames,
                               const std::array<std::int16_t, 20>& temps,VmSavedRoutinePolicy policy) {
    reject_storage_proof_mutation();
    frames=prepare_dormant_frames(std::move(frames),policy);
    ++run_generation_;
    stack_ = std::move(frames);
    action_tree_known_=false;
    register_temps() = temps;
    register_xl().fill(0); // Native saved-thread profiles do not supply TempXL values.
    steps_ = 0;
    diagnostic_.clear();
    pending_.reset();
    suspended_pending_.clear();
    interaction_return_.reset();
    external_frame_return_.reset();
    immediate_continuations_.clear();immediate_return_.reset();
    status_ = VmStatus::dormant;
}
bool Vm::activate_dormant_state() {
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter state activation");
    if (status_ != VmStatus::dormant || stack_.empty()) return false;
    if(memory_.resolve_live_entity&&!bind_legacy_entity_references())return false;
    status_ = VmStatus::running;
    return true;
}
std::optional<VmMemoryAddress> resolve_person_data_address(std::uint8_t scope,
    std::int16_t index,const std::array<std::int16_t,20>& temps) {
    if(scope!=30&&scope!=31)return VmMemoryAddress{scope,index};
    return resolve_ts1_variable_address(scope,index,VmFrame{},temps);
}
std::optional<VmMemoryAddress> resolve_ts1_variable_address(std::uint8_t scope,
    std::int16_t index,const VmFrame& frame,const Ts1Temps& temps) {
    const std::vector<std::int16_t>* arguments=nullptr;
    std::uint8_t target=scope;
    switch(scope) {
    case 17:target=15;break;
    case 22:target=1;arguments=&frame.arguments;break;
    case 40:target=25;break;
    case 41:target=1;break;
    case 30:target=18;break;
    case 31:target=19;break;
    default:return VmMemoryAddress{scope,index};
    }
    if(index<0)return {};
    const auto selector=static_cast<std::size_t>(index);
    if(arguments) {
        if(selector>=arguments->size())return {};
        index=(*arguments)[selector];
    } else {
        if(selector>=temps.size())return {};
        index=temps[selector];
    }
    // Attribute selectors cast to ushort in the reference. The native bounded
    // bank cannot represent/accept that high unsigned half; never alias it.
    if(index<0)return {};
    return VmMemoryAddress{target,index};
}
std::int32_t Vm::read(std::uint8_t scope, std::int16_t data) {
    auto& frame = stack_.back();
    const auto address=resolve_ts1_variable_address(scope,data,frame,register_temps());
    if(!address)throw VmError("indirect read selector outside bank");
    scope=address->scope;data=address->index;
    const auto temp = [this](std::int16_t index) -> std::int16_t& {
        if (index < 0 || static_cast<std::size_t>(index) >= register_temps().size())
            throw VmError("temp index outside registers");
        return register_temps()[static_cast<std::size_t>(index)];
    };
    switch (scope) {
    case 7: return data;
    case 8: return temp(data);
    case 9: return indexed(frame.arguments, data);
    case 10: return frame.context.stack_object;
    case 11: return temp(temp(data));
    case 25: return indexed(frame.locals, data);
    case 34: return 0; // Pinned NeighborhoodData has no mutable tutorial state.
    case 42:
        if (data < 0 || static_cast<std::size_t>(data) >= register_xl().size())
            throw VmError("TempXL index outside registers");
        return register_xl()[static_cast<std::size_t>(data)];
    default:
        if(scope==33&&memory_.job_data) {
            const auto value=memory_.job_data(frame,static_cast<std::uint16_t>(register_temps()[0]),
                register_temps()[1],data);
            if(value)return *value;
        }
        if (scope == 27 && memory_.dynamic_sprite_flag) {
            const auto flag=static_cast<std::uint16_t>(temp(data));
            if (const auto value=memory_.dynamic_sprite_flag(frame,flag)) return *value?1:0;
        }
        if (scope == 23 && memory_.room_by_temp0) {
            if (const auto value = memory_.room_by_temp0(frame, register_temps()[0], data)) return *value;
        }
        if (const auto address=resolve_person_data_address(scope,data,register_temps());scope!=33&&address&&memory_.read) {
            const auto value = memory_.read(frame, address->scope, address->index);
            if (value) return *value;
        }
        throw VmError("unsupported read scope " + std::to_string(scope) + " index " + std::to_string(data) +
            " (caller=" + std::to_string(frame.context.caller) + " callee=" +
            std::to_string(frame.context.callee) + " stack_object=" +
            std::to_string(frame.context.stack_object) + " owner=" +
            std::to_string(frame.context.code_owner) + " routine=" + std::to_string(frame.key.id) +
            " pc=" + std::to_string(frame.pc) + ")");
    }
}
bool Vm::write(std::uint8_t scope, std::int16_t data, std::int32_t value) {
    auto& frame = stack_.back();
    const auto address=resolve_ts1_variable_address(scope,data,frame,register_temps());
    if(!address)throw VmError("indirect write selector outside bank");
    scope=address->scope;data=address->index;
    const auto truncated = word(static_cast<std::uint32_t>(value));
    const auto temp = [this](std::int16_t index) -> std::int16_t& {
        if (index < 0 || static_cast<std::size_t>(index) >= register_temps().size())
            throw VmError("temp index outside registers");
        return register_temps()[static_cast<std::size_t>(index)];
    };
    switch (scope) {
    // The pinned SetVariable explicitly rejects these read-only scopes.
    case 34: throw VmError("NeighborhoodData writes are not implemented by the pinned reference");
    case 7: case 21: case 26: case 35: case 38: return false;
    case 8: temp(data) = truncated; return true;
    case 9: indexed(frame.arguments, data) = truncated; return true;
    case 10: return assign_stack_object(frame.context,truncated);
    case 11: temp(temp(data)) = truncated; return true;
    case 25: indexed(frame.locals, data) = truncated; return true;
    case 42:
        if (data < 0 || static_cast<std::size_t>(data) >= register_xl().size())
            throw VmError("TempXL index outside registers");
        register_xl()[static_cast<std::size_t>(data)] = value; return true;
    default:
        if (scope == 27 && memory_.set_dynamic_sprite_flag) {
            const auto flag=static_cast<std::uint16_t>(temp(data));
            if (memory_.set_dynamic_sprite_flag(frame,flag,truncated>0)) return true;
        }
        if (const auto address=resolve_person_data_address(scope,data,register_temps());address) {
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
        const auto& routine=current_routine(frame);
        if(frame.pc>=routine.instructions.size())throw VmError("instruction pointer outside routine");
        const auto& instruction = routine.instructions[frame.pc];
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
        if(frame.return_mode==VmCallReturnMode::immediate){
            if(immediate_continuations_.empty()||immediate_return_||
               immediate_continuations_.back().ticket!=frame.invocation_ticket||
               immediate_continuations_.back().parent_depth+1!=stack_.size())
                throw VmError("immediate frame lost its exact parent continuation");
            immediate_return_=VmImmediateReturn{frame.invocation_ticket,result,frame.context.code_owner,run_generation_};
            stack_.pop_back();
            status_=VmStatus::yielded;
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
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter start/run");
    if (status_ != VmStatus::running && status_ != VmStatus::budget_exhausted) return status_;
    const auto unbound=[](const VmContext& context){return !context.entity_references_bound;};
    bool legacy=std::any_of(stack_.begin(),stack_.end(),[&](const auto& frame){return unbound(frame.context);})||
        (pending_&&unbound(pending_->context))||std::any_of(suspended_pending_.begin(),suspended_pending_.end(),[&](const auto& pending){return unbound(pending.context);});
    for(const auto& continuation:immediate_continuations_)legacy=legacy||(continuation.parent_pending&&unbound(continuation.parent_pending->context));
    if(legacy&&memory_.resolve_live_entity&&!bind_legacy_entity_references()){
        diagnostic_="legacy frame entity identity cannot be bound before execution";status_=VmStatus::fault;return status_;
    }
    struct Guard {
        bool& value;
        explicit Guard(bool& flag) : value(flag) { value = true; }
        ~Guard() { value = false; }
    } guard(executing_);
    return run_internal(budget,trace,nullptr,false);
}

bool Vm::references_entity(std::int16_t entity) const {
    const auto matches=[entity](const VmContext& context){
        return context.caller==entity||context.callee==entity||context.stack_object==entity;
    };
    for(const auto& frame:stack_)if(matches(frame.context))return true;
    if(pending_&&matches(pending_->context))return true;
    for(const auto& pending:suspended_pending_)if(matches(pending.context))return true;
    return false;
}

void Vm::reject_storage_proof_mutation() const {
    if(storage_proof_active_){
        storage_proof_reentered_=true;
        throw std::logic_error("entity storage proof predicate must not mutate its VM");
    }
    if(external_check_active_)throw std::logic_error("disposable check must not mutate parent execution");
}

bool Vm::can_release_entity_storage(std::int16_t entity,
    const std::function<bool(const VmFrame&,const Instruction&)>& transparent_pending_predicate,
    std::size_t node_budget) const {
    if(storage_proof_active_){storage_proof_reentered_=true;return false;}
    if(entity<=0||executing_||check_active_||external_check_active_)return false;
    if(!immediate_continuations_.empty()||immediate_return_)return false;
    if(!references_entity(entity))return true;
    if(status_==VmStatus::fault||status_==VmStatus::dormant||
       !node_budget||node_budget>4096||stack_.empty()||!suspended_pending_.empty()||
       interaction_return_||external_frame_return_||!immediate_continuations_.empty()||immediate_return_)return false;
    bool matched_stack_object=false;
    for(const auto& frame:stack_)
    {
        matched_stack_object|=frame.context.stack_object==entity;
        if((matched_stack_object&&frame.context.stack_object!=entity)||
           frame.context.caller==entity||frame.context.callee==entity||
           frame.return_mode!=VmCallReturnMode::ordinary)return false;
    }
    if(pending_&&(pending_->context.caller==entity||pending_->context.callee==entity))return false;
    struct Guard {
        bool& active;bool& reentered;
        Guard(bool& a,bool& r):active(a),reentered(r){active=true;reentered=false;}
        ~Guard(){active=false;reentered=false;}
    } guard(storage_proof_active_,storage_proof_reentered_);
    try {
        if(pending_){
            const auto& frame=stack_.back();const auto* routine=resolve(frame.key,frame.context.code_owner);
            if(status_!=VmStatus::waiting||!routine||frame.pc>=routine->instructions.size()||
               pending_->instruction.depth!=stack_.size()||pending_->instruction.pc!=frame.pc||
               pending_->instruction.key.scope!=frame.key.scope||pending_->instruction.key.id!=frame.key.id||
               pending_->instruction.code_owner!=frame.context.code_owner||!pending_->ticket||
               pending_->ticket>ticket_sequence_||
               !vm_context_equal(pending_->context,frame.context)||pending_->context.caller!=frame.context.caller||pending_->context.callee!=frame.context.callee||
               pending_->context.stack_object!=frame.context.stack_object||
               pending_->context.code_owner!=frame.context.code_owner||!transparent_pending_predicate)
                return false;
            const auto& instruction=routine->instructions[frame.pc];
            const auto& saved=pending_->instruction.instruction;
            if(saved.opcode!=instruction.opcode||saved.operands!=instruction.operands||
               saved.on_true!=instruction.on_true||saved.on_false!=instruction.on_false||
               !transparent_pending_predicate(frame,instruction)||storage_proof_reentered_)return false;
        }else if(status_==VmStatus::waiting)return false;
        for(std::size_t depth=0;depth<stack_.size();++depth){
            const auto& frame=stack_[depth];
            if(frame.context.stack_object!=entity)continue;
            const auto* routine=resolve(frame.key,frame.context.code_owner);
            if(!routine||frame.pc>=routine->instructions.size())return false;
            const auto& current=routine->instructions[frame.pc];
            const bool child_active=depth+1<stack_.size();
            if(child_active){
                const auto& child=stack_[depth+1];
                if(current.opcode<256||child.key.scope!=routine_key(current.opcode).scope||
                   child.key.id!=current.opcode||child.context.code_owner!=frame.context.code_owner||
                   child.context.caller!=frame.context.caller||child.context.callee!=frame.context.callee||
                   child.context.entity_references_bound!=frame.context.entity_references_bound||
                   !(vm_context_reference(child.context,VmEntityRole::caller)==vm_context_reference(frame.context,VmEntityRole::caller))||
                   !(vm_context_reference(child.context,VmEntityRole::callee)==vm_context_reference(frame.context,VmEntityRole::callee))||
                   !resolve(child.key,child.context.code_owner))return false;
            }
            // Tri-color DFS rejects cycles, including otherwise harmless loops.
            // No variable is evaluated; both comparison/return outcomes must be safe.
            std::map<std::uint16_t,std::uint8_t> marks;
            std::function<bool(std::uint16_t)> visit;
            const auto branch=[&](const Instruction& instruction,bool result){
                auto target=result?instruction.on_true:instruction.on_false;
                if(target==0xFD){
                    target=instruction.on_true!=0xFD?instruction.on_true:instruction.on_false;
                    if(target==0xFD)return true; // Both error branches terminate execution.
                }
                return target>=0xFE||visit(target);
            };
            const auto value_scope=[&](std::uint8_t scope,std::int16_t index){
                switch(scope){
                case 7:case 10:return true; // Literal and raw StackObjectID are values, not data reads.
                case 8:return index>=0&&static_cast<std::size_t>(index)<register_temps().size();
                case 9:return index>=0&&static_cast<std::size_t>(index)<frame.arguments.size();
                case 25:return index>=0&&static_cast<std::size_t>(index)<frame.locals.size();
                default:return false;
                }
            };
            visit=[&](std::uint16_t pc){
                if(pc>=routine->instructions.size())return false;
                const auto marked=marks.find(pc);
                if(marked!=marks.end())return marked->second==2;
                if(!node_budget)return false;
                --node_budget;marks.emplace(pc,1);
                const auto& instruction=routine->instructions[pc];
                if(instruction.opcode!=2||instruction.operands[4]!=0)return false;
                const auto op=instruction.operands[5],lhs=instruction.operands[6],rhs=instruction.operands[7];
                if(!value_scope(rhs,operand_word(instruction,2)))return false;
                if(op==5){
                    if(lhs==10){marks[pc]=2;return true;} // Authored assignment releases the old binding.
                    if((lhs!=8&&lhs!=9&&lhs!=25)||!value_scope(lhs,operand_word(instruction,0))||
                       !branch(instruction,true))return false;
                }else if(op==2){
                    if(!value_scope(lhs,operand_word(instruction,0))||
                       !branch(instruction,true)||!branch(instruction,false))return false;
                }else return false;
                marks[pc]=2;return true;
            };
            if(child_active||(depth+1==stack_.size()&&pending_)){
                if(!node_budget)return false;
                --node_budget;
                if(!branch(current,true)||!branch(current,false))return false;
            }else if(!visit(frame.pc))return false;
        }
        return !storage_proof_reentered_;
    }catch(...){return false;}
}

bool Vm::can_start_synchronous_check(std::uint64_t budget) const {
    return !executing_&&!check_active_&&status_==VmStatus::running&&!steps_&&stack_.size()==1&&
       !stack_.front().pc&&stack_.front().return_mode==VmCallReturnMode::ordinary&&
       !pending_&&suspended_pending_.empty()&&!interaction_return_&&!external_frame_return_&&budget;
}

VmSubroutineResult Vm::run_synchronous_check_with_registers(Vm& owner,bool share,
        std::uint64_t budget,const VmPrimitive& primitive,const std::function<void(const VmTrace&)>& trace,
        bool allow_lifecycle_deletion) {
    reject_storage_proof_mutation();
    // The loan authorizes only register-bank aliases. A protected parent's
    // execution state remains guarded; a storage audit still forbids mutation.
    if(share&&owner.storage_proof_active_)owner.reject_storage_proof_mutation();
    if(&owner==this)return {false,status_,0,"disposable check cannot own its register seed"};
    if(!can_start_synchronous_check(budget))return run_synchronous_check(budget,primitive,trace,allow_lifecycle_deletion);
    if(!share){
        register_temps()=owner.register_temps();register_xl()=owner.register_xl();
        return run_synchronous_check(budget,primitive,trace,allow_lifecycle_deletion);
    }
    struct RegisterLoan {
        Ts1Temps& own;Ts1TempXL& own_xl;Ts1Temps*& active;Ts1TempXL*& active_xl;
        Ts1Temps* previous;Ts1TempXL* previous_xl;
        RegisterLoan(Ts1Temps& storage,Ts1TempXL& storage_xl,Ts1Temps*& slot,Ts1TempXL*& slot_xl,
            Ts1Temps& owner,Ts1TempXL& owner_xl):own(storage),own_xl(storage_xl),active(slot),active_xl(slot_xl),
            previous(slot),previous_xl(slot_xl){active=&owner;active_xl=&owner_xl;}
        ~RegisterLoan(){
            // Retain the disposable VM's final visible values after the loan,
            // without writing a stale copy over the real shared owner.
            own=*active;own_xl=*active_xl;active=previous;active_xl=previous_xl;
        }
    } loan{temps_,temp_xl_,shared_temps_,shared_xl_,owner.register_temps(),owner.register_xl()};
    return run_synchronous_check(budget,primitive,trace,allow_lifecycle_deletion);
}

VmSubroutineResult Vm::run_synchronous_check(std::uint64_t budget,const VmPrimitive& primitive,
    const std::function<void(const VmTrace&)>& trace,bool allow_lifecycle_deletion) {
    reject_storage_proof_mutation();
    VmSubroutineResult result;
    if(!can_start_synchronous_check(budget)){
        result.status=status_;
        result.diagnostic="synchronous check requires a freshly started disposable VM and a nonzero budget";
        return result;
    }
    struct Guard {
        bool& executing;bool& checking;
        Guard(bool& a,bool& b):executing(a),checking(b){executing=true;checking=true;}
        ~Guard(){executing=false;checking=false;}
    } guard(executing_,check_active_);
    result.started=true;
    advertisement_changes_.reset();
    try{
        result.status=run_internal(budget,trace,&primitive,true,&result.steps,
            allow_lifecycle_deletion?&result.lifecycle_deleted:nullptr,&result.source_error);
        result.diagnostic=diagnostic_;
        if(result.status==VmStatus::budget_exhausted)result.diagnostic="check instruction limit reached";
    }catch(const std::exception& error){
        result.source_error=false;
        status_=VmStatus::fault;result.status=status_;result.diagnostic=error.what();
    }catch(...){
        result.source_error=false;
        status_=VmStatus::fault;result.status=status_;result.diagnostic="synchronous check raised a non-standard exception";
    }
    return result;
}

VmStatus Vm::run_internal(std::uint64_t instruction_budget,
                          const std::function<void(const VmTrace&)>& trace,
                          const VmPrimitive* primitive_override,bool check_mode,
                          std::uint64_t* executed_steps,bool* lifecycle_deleted,bool* source_error) {
    if (status_ != VmStatus::running && status_ != VmStatus::budget_exhausted) return status_;
    status_ = VmStatus::running;
    std::uint64_t used=0;
    for (;used<instruction_budget && status_ == VmStatus::running;++used) {
        try {
            const auto& frame = stack_.back();
            const auto& routine = current_routine(frame);
            if (frame.pc >= routine.instructions.size()) throw VmError("instruction pointer outside routine");
            const auto instruction = routine.instructions[frame.pc];
            if (!check_mode&&steps_ == std::numeric_limits<std::uint64_t>::max())
                throw VmError("step counter overflow");
            if (trace) trace(VmTrace{steps_, stack_.size(), frame.key, frame.pc, instruction,frame.context.code_owner});
            if(!check_mode)++steps_;
            if (instruction.opcode >= 256) {
                const auto key = routine_key(instruction.opcode);
                const auto* found = resolve(key,frame.context.code_owner);
                const auto metadata = found ? execution_metadata(*found) : std::nullopt;
                if (!metadata) {
                    push(key, frame.context, {}); // Produce the specific missing/metadata diagnostic.
                    continue;
                }
                const auto count = std::max<std::size_t>(4, metadata->arguments);
                if (count > register_temps().size()) throw VmError("call argument count exceeds supported temp mapping");
                std::vector<std::int16_t> args(count);
                const bool substitute = operand_word(instruction, 2) != 0 ||
                    operand_word(instruction, 4) != 0 || operand_word(instruction, 6) != 0;
                for (std::size_t n = 0; n < count; ++n) {
                    auto value = n < 4 ? operand_word(instruction, n * 2) : static_cast<std::int16_t>(-1);
                    if (value == -1 && substitute) value = register_temps()[n];
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
                const auto& handler=primitive_override?*primitive_override:primitive_;
                const VmDispatch dispatch{this,++ticket_sequence_,check_mode};
                const auto outcome = [&] {
                    struct ActivePrimitive {
                        Vm& vm;const VmDispatch* previous_dispatch;const Instruction* previous_instruction;
                        ActivePrimitive(Vm& value,const VmDispatch& active,const Instruction& operation):
                            vm(value),previous_dispatch(value.active_primitive_dispatch_),
                            previous_instruction(value.active_primitive_instruction_){
                            vm.active_primitive_dispatch_=&active;vm.active_primitive_instruction_=&operation;
                        }
                        ~ActivePrimitive(){vm.active_primitive_dispatch_=previous_dispatch;
                            vm.active_primitive_instruction_=previous_instruction;}
                    } active(*this,dispatch,instruction);
                    return handler ? handler(frame,instruction,register_temps(),dispatch) : PrimitiveOutcome{};
                }();
                if(outcome.disposition==PrimitiveDisposition::source_error&&
                   (outcome.write||outcome.call||outcome.call_commit||outcome.temp_write||
                    outcome.second_temp_write||outcome.stack_object_write||outcome.variable_write))
                    throw VmError("source ERROR cannot carry primitive outputs");
                if(check_mode && (outcome.disposition==PrimitiveDisposition::goto_true_next_tick ||
                                  outcome.disposition==PrimitiveDisposition::continue_next_tick))
                    throw VmError("check BHAV attempted to yield until the next tick");
                if(outcome.variable_write){
                    if(outcome.disposition!=PrimitiveDisposition::goto_true&&
                       outcome.disposition!=PrimitiveDisposition::goto_false&&
                       outcome.disposition!=PrimitiveDisposition::goto_true_next_tick)
                        throw VmError("primitive variable output requires synchronous branch disposition");
                    if(outcome.write||outcome.temp_write||outcome.second_temp_write||
                       outcome.stack_object_write||outcome.call||outcome.call_commit)
                        throw VmError("primitive variable output cannot be combined with other outputs or calls");
                    if(outcome.variable_write->scope>255)
                        throw VmError("primitive variable output scope outside TS1 address domain");
                }
                if (outcome.stack_object_write &&
                    outcome.disposition != PrimitiveDisposition::goto_true &&
                    outcome.disposition != PrimitiveDisposition::goto_true_next_tick &&
                    outcome.disposition != PrimitiveDisposition::goto_false)
                    throw VmError("primitive stack-object output requires synchronous branch disposition");
                if (outcome.temp_write || outcome.second_temp_write) {
                    if (outcome.disposition != PrimitiveDisposition::goto_true &&
                        outcome.disposition != PrimitiveDisposition::goto_true_next_tick &&
                        outcome.disposition != PrimitiveDisposition::goto_false)
                        throw VmError("primitive temp output requires synchronous branch disposition");
                    if ((outcome.temp_write && outcome.temp_write->index >= register_temps().size()) ||
                        (outcome.second_temp_write && outcome.second_temp_write->index >= register_temps().size()))
                        throw VmError("primitive temp output index outside registers");
                }
                if(outcome.call && outcome.disposition!=PrimitiveDisposition::call)
                    throw VmError("primitive call payload requires call disposition");
                if(outcome.call_commit&&outcome.disposition!=PrimitiveDisposition::call)
                    throw VmError("primitive call commit requires call disposition");
                if (outcome.write && outcome.disposition == PrimitiveDisposition::unsupported)
                    throw VmError("unsupported primitive outcome must not carry frame output");
                switch (outcome.disposition) {
                case PrimitiveDisposition::lifecycle_deleted:
                    // The trusted host has authenticated removal of this exact
                    // lifecycle owner. Never expose this escape to ordinary checks
                    // or Main, and never turn deletion into a fictitious true return.
                    if(!check_mode || !lifecycle_deleted || instruction.opcode!=18 ||
                       outcome.write || outcome.call || outcome.call_commit ||
                       outcome.temp_write || outcome.second_temp_write || outcome.stack_object_write || outcome.variable_write)
                        throw VmError("lifecycle deletion requires an opted-in synchronous RemoveObjectInstance without outputs");
                    stack_.clear();
                    status_=VmStatus::ready;
                    *lifecycle_deleted=true;
                    break;
                case PrimitiveDisposition::call:
                    if(check_mode){
                        bool named_child=false;
                        if(instruction.opcode==28&&instruction.operands[5]==2&&outcome.call&&named_routine_scope(outcome.call->key.scope)&&
                           outcome.call->return_mode==VmCallReturnMode::ordinary&&!outcome.call->exact_arguments&&
                           !outcome.call->action_tree_override&&!outcome.call_commit&&!outcome.write&&
                           !outcome.temp_write&&!outcome.second_temp_write&&!outcome.stack_object_write&&!outcome.variable_write){
                            auto participants=outcome.call->context;participants.code_owner=frame.context.code_owner;
                            named_child=vm_context_equal(participants,frame.context);
                        }
                        if(!named_child)throw VmError("check BHAV requested an asynchronous child service");
                    }
                    if(!outcome.call)throw VmError("primitive call requires child");
                    if(outcome.call->return_mode==VmCallReturnMode::external_frame_next_tick)
                        throw VmError("external child requires its pending service scheduling API");
                    {
                    const auto parent_index=stack_.size()-1;
                    const bool immediate=outcome.call->return_mode==VmCallReturnMode::immediate;
                    std::optional<VmFrame> immediate_parent_output;
                    if(outcome.write){
                        if(immediate){
                            immediate_parent_output=stack_.back();
                            auto* output=frame_output(*immediate_parent_output,*outcome.write,limits_.frame_variables);
                            if(!output)throw VmError("primitive call output outside parent frame");
                            *output=outcome.write->value;
                        }else if(!frame_output(stack_.back(),*outcome.write,limits_.frame_variables))
                            throw VmError("primitive call output outside parent frame");
                    }
                    if(immediate){
                        if(!can_accept_call(*outcome.call)||pending_||!suspended_pending_.empty()||
                           interaction_return_||external_frame_return_||immediate_return_||
                           outcome.call->context.caller<=0||outcome.call->context.caller!=frame.context.caller||
                           std::any_of(stack_.begin(),stack_.end(),[](const auto& active){
                               return active.return_mode==VmCallReturnMode::external_frame_next_tick;}))
                            throw VmError("immediate primitive child has an unsafe parent continuation");
                        immediate_continuations_.reserve(immediate_continuations_.size()+1);
                        immediate_continuations_.push_back({dispatch.ticket,stack_.size(),VmStatus::running,{}});
                    }
                    try{push(outcome.call->key,outcome.call->context,outcome.call->arguments,
                             outcome.call->return_mode, dispatch.ticket,outcome.call->exact_arguments,outcome.call->action_tree_override);}
                    catch(...){if(immediate)immediate_continuations_.pop_back();throw;}
                    if(outcome.call_commit){
                        bool accepted=false;
                        try{
                            struct CommitGuard {
                                Vm& vm;
                                CommitGuard(Vm& value,const VmDispatch& token,const Instruction& operation,std::size_t depth):vm(value){
                                    vm.active_call_commit_dispatch_=&token;vm.active_call_commit_instruction_=&operation;
                                    vm.active_call_commit_parent_depth_=depth;
                                }
                                ~CommitGuard(){vm.active_call_commit_dispatch_=nullptr;vm.active_call_commit_instruction_=nullptr;
                                    vm.active_call_commit_parent_depth_=0;}
                            } commit(*this,dispatch,instruction,parent_index+1);
                            accepted=outcome.call_commit();
                        }
                        catch(...){stack_.pop_back();if(immediate)immediate_continuations_.pop_back();status_=VmStatus::fault;throw;}
                        if(!accepted){
                            stack_.pop_back();
                            if(immediate)immediate_continuations_.pop_back();
                            throw VmError("primitive call state commit rejected after frame preflight");
                        }
                    }
                    if(immediate_parent_output){
                        stack_[parent_index].arguments.swap(immediate_parent_output->arguments);
                        stack_[parent_index].locals.swap(immediate_parent_output->locals);
                    }else if(outcome.write)*frame_output(stack_[parent_index],*outcome.write,limits_.frame_variables)=outcome.write->value;
                    }
                    break;
                case PrimitiveDisposition::goto_true:
                case PrimitiveDisposition::goto_true_next_tick:
                case PrimitiveDisposition::goto_false:
                    // Validate all frame/temp/context outputs before publishing any.
                    if (outcome.write && !frame_output(stack_.back(), *outcome.write,limits_.frame_variables))
                        throw VmError("primitive frame output bank/index outside frame");
                    if(outcome.variable_write&&!write(static_cast<std::uint8_t>(outcome.variable_write->scope),
                        outcome.variable_write->index,outcome.variable_write->value))
                        throw VmError("primitive variable output destination rejected by TS1 setter");
                    if (outcome.write) {
                        auto* destination = frame_output(stack_.back(), *outcome.write,limits_.frame_variables);
                        *destination = outcome.write->value;
                    }
                    if (outcome.temp_write)
                        register_temps()[outcome.temp_write->index] = outcome.temp_write->value;
                    if (outcome.second_temp_write)
                        register_temps()[outcome.second_temp_write->index] = outcome.second_temp_write->value;
                    if (outcome.stack_object_write)
                        if(!assign_stack_object(stack_.back().context,outcome.stack_object_write->value))throw VmError("StackObject result binding failed");
                    branch(outcome.disposition != PrimitiveDisposition::goto_false);
                    if(outcome.disposition==PrimitiveDisposition::goto_true_next_tick && status_==VmStatus::running)
                        status_=VmStatus::next_tick;
                    break;
                case PrimitiveDisposition::continue_next_tick:
                    // Source CONTINUE_NEXT_TICK retries this exact instruction,
                    // without selecting a branch or creating an async ticket.
                    if(outcome.write)throw VmError("CONTINUE_NEXT_TICK cannot carry frame output");
                    status_=VmStatus::next_tick;
                    break;
                case PrimitiveDisposition::continue_execution:
                    // Source CONTINUE retains the parent PC. An empty Push
                    // therefore retries until the ordinary instruction budget.
                    if(outcome.write)throw VmError("CONTINUE cannot carry frame output");
                    break;
                case PrimitiveDisposition::wait:
                    if(check_mode)
                        throw VmError("check BHAV attempted to wait for an asynchronous service");
                    if (outcome.write) {
                        auto* destination = frame_output(stack_.back(), *outcome.write,limits_.frame_variables);
                        if (!destination) throw VmError("primitive frame output bank/index outside frame");
                        *destination = outcome.write->value;
                    }
                    pending_ = VmPending{dispatch.ticket,VmTrace{steps_-1,stack_.size(),frame.key,frame.pc,instruction,frame.context.code_owner},frame.context};
                    status_ = VmStatus::waiting; break;
                case PrimitiveDisposition::source_error:
                    // Source RunInMyStack may map an authored ERROR to false.
                    // Keep its provenance distinct from unsupported/host faults;
                    // do not persist it or turn an ordinary Main error into a return.
                    diagnostic_=outcome.diagnostic.empty()?"authored primitive ERROR":outcome.diagnostic;
                    status_=VmStatus::fault;
                    if(source_error)*source_error=true;
                    break;
                case PrimitiveDisposition::unsupported:
                    if(outcome.diagnostic.empty()){
                        std::string detail="unsupported primitive "+std::to_string(instruction.opcode)+" operands=[";
                        for(std::size_t n=0;n<instruction.operands.size();++n){
                            if(n)detail+=',';
                            detail+=std::to_string(instruction.operands[n]);
                        }
                        throw VmError(detail+"]");
                    }
                    throw VmError(outcome.diagnostic);
                default: throw VmError("invalid primitive outcome");
                }
            }
        } catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
        catch (const std::exception& e) {
            if(!check_mode)throw;
            diagnostic_=e.what();status_=VmStatus::fault;
        } catch (...) {
            if(!check_mode)throw;
            diagnostic_="Check BHAV primitive raised a non-standard exception";
            status_=VmStatus::fault;
        }
        if(executed_steps)*executed_steps=used+1;
    }
    if (status_ == VmStatus::running && used>=instruction_budget) status_ = VmStatus::budget_exhausted;
    if(executed_steps)*executed_steps=used;
    return status_;
}

VmSubroutineResult Vm::evaluate_check(RoutineKey key,VmContext context,
        const std::vector<std::int16_t>& arguments,std::uint64_t instruction_budget,
        const VmPrimitive& check_primitive,bool exact_arguments,const VmPrimitiveFactory& primitive_factory) {
    return evaluate_temporary_stack(key,context,arguments,instruction_budget,check_primitive,exact_arguments,primitive_factory,false);
}
VmSubroutineResult Vm::run_named_in_stack(RoutineKey key,VmContext context,const std::vector<std::int16_t>& arguments,
    std::uint64_t budget,const VmPrimitive& primitive,const VmPrimitiveFactory& factory){
    try{return evaluate_temporary_stack(key,context,arguments,budget,primitive,false,factory,true);}
    catch(const std::exception& error){return {false,VmStatus::fault,0,error.what()};}
}
VmSubroutineResult Vm::evaluate_temporary_stack(RoutineKey key,VmContext context,const std::vector<std::int16_t>& arguments,
    std::uint64_t instruction_budget,const VmPrimitive& check_primitive,bool exact_arguments,
    const VmPrimitiveFactory& primitive_factory,bool named){
    reject_storage_proof_mutation();
    VmSubroutineResult result;
    const bool live_main=!stack_.empty()&&status_==VmStatus::running;
    const bool primitive_boundary=active_primitive_dispatch_&&active_primitive_instruction_&&!stack_.empty()&&
        matches_active_query(stack_.back(),*active_primitive_instruction_,*active_primitive_dispatch_);
    if(!instruction_budget||(named?(named_stack_depth_>=limits_.stack_depth||(executing_&&!primitive_boundary)):
       (check_active_||!live_main||!executing_||!primitive_boundary||pending_||interaction_return_||external_frame_return_))) {
        result.status=status_;
        result.diagnostic=check_active_?"nested Check BHAV evaluation is unsupported":
            "Check BHAV evaluation requires an executing Main primitive safe point";
        return result;
    }
    const bool inherited_action_tree=named&&!stack_.empty()?stack_.back().action_tree:false;
    if(named){
        if(!context.code_owner||!routine_key_valid(key)||key.scope==RoutineScope::global)
            return {false,VmStatus::fault,0,"named execution requires its explicit resource owner and entry namespace"};
        resolve_missing_owner(context.code_owner);
        if(!shared_program_for_owner(context.code_owner))return {false,VmStatus::fault,0,"named resource owner unavailable"};
        const auto* routine=resolve(key,context.code_owner);
        if(!routine||routine->instructions.empty())return {true,VmStatus::returned_false,0,"known named routine absent or empty"};
        // Source RunInMyStack removes its temporary predecessor at index0.
        // With no old frame that removes the new child itself, returning false.
        if(stack_.empty())return {true,VmStatus::returned_false,0,"named executor has no predecessor frame"};
    }
    struct ExecutionScope {
        bool& executing;bool previous;std::size_t& depth;bool named;bool& inherited_index;bool previous_index;
        ExecutionScope(bool& value,std::size_t& count,bool n,bool& index,bool was_check):
            executing(value),previous(value),depth(count),named(n),inherited_index(index),previous_index(index){
            executing=true;if(named){if(!depth)inherited_index=!was_check;++depth;}}
        ~ExecutionScope(){executing=previous;if(named){--depth;inherited_index=previous_index;}}
    } execution{executing_,named_stack_depth_,named,named_queue_inherits_parent_index_,check_active_};
    struct RestoreAdvertisements {
        AdvertisementChanges& state;AdvertisementChanges parent;bool isolate;
        RestoreAdvertisements(AdvertisementChanges& value,bool enabled):state(value),isolate(enabled){if(isolate){parent=std::move(value);state.reset();}}
        ~RestoreAdvertisements(){if(isolate)state=std::move(parent);}
    } restore_advertisements{advertisement_changes_,!named};
    // Swapping the vector transfers ownership of the Main frames without moving
    // their storage. The outer run() primitive still holds a valid reference to
    // its frame while this temporary check stack uses stack_.
    std::vector<VmFrame> main_stack;
    main_stack.swap(stack_);
    struct EntityThreadView {
        const std::vector<VmFrame>*& current;const std::vector<VmFrame>* previous;
        EntityThreadView(const std::vector<VmFrame>*& view,const std::vector<VmFrame>& parked,bool fresh):
            current(view),previous(view){if(fresh&&!current)current=&parked;}
        ~EntityThreadView(){current=previous;}
    } entity_view{entity_thread_stack_,main_stack,!named};
    const auto main_status=status_;
    auto main_diagnostic=std::move(diagnostic_);
    auto main_pending=std::move(pending_);
    auto main_suspended_pending=std::move(suspended_pending_);
    auto main_interaction_return=std::move(interaction_return_);
    auto main_external_frame_return=std::move(external_frame_return_);
    auto main_immediate_continuations=std::move(immediate_continuations_);
    auto main_immediate_return=std::move(immediate_return_);

    struct RestoreMainState {
        std::vector<VmFrame>& stack;
        std::vector<VmFrame>& main_stack;
        VmStatus& status;
        VmStatus main_status;
        std::string& diagnostic;
        std::string main_diagnostic;
        std::optional<VmPending>& pending;
        std::optional<VmPending> main_pending;
        std::vector<VmPending>& suspended_pending;
        std::vector<VmPending> main_suspended_pending;
        std::optional<VmInteractionReturn>& interaction_return;
        std::optional<VmInteractionReturn> main_interaction_return;
        std::optional<VmExternalFrameReturn>& external_frame_return;
        std::optional<VmExternalFrameReturn> main_external_frame_return;
        std::vector<VmImmediateContinuation>& immediate_continuations;
        std::vector<VmImmediateContinuation> main_immediate_continuations;
        std::optional<VmImmediateReturn>& immediate_return;
        std::optional<VmImmediateReturn> main_immediate_return;
        bool& check_active;bool previous_check;
        RestoreMainState(std::vector<VmFrame>& stack_,std::vector<VmFrame>& main_stack_,
                VmStatus& status_,VmStatus main_status_,std::string& diagnostic_,
                std::string main_diagnostic_,std::optional<VmPending>& pending_,
                std::optional<VmPending> main_pending_,std::vector<VmPending>& suspended_pending_,
                std::vector<VmPending> main_suspended_pending_,
                std::optional<VmInteractionReturn>& interaction_return_,
                std::optional<VmInteractionReturn> main_interaction_return_,
                std::optional<VmExternalFrameReturn>& external_frame_return_,
                std::optional<VmExternalFrameReturn> main_external_frame_return_,
                std::vector<VmImmediateContinuation>& immediate_continuations_,
                std::vector<VmImmediateContinuation> main_immediate_continuations_,
                std::optional<VmImmediateReturn>& immediate_return_,
                std::optional<VmImmediateReturn> main_immediate_return_,bool& check_active_)
            noexcept
            :stack(stack_),main_stack(main_stack_),status(status_),main_status(main_status_),
             diagnostic(diagnostic_),main_diagnostic(std::move(main_diagnostic_)),
             pending(pending_),main_pending(std::move(main_pending_)),
             suspended_pending(suspended_pending_),
             main_suspended_pending(std::move(main_suspended_pending_)),
             interaction_return(interaction_return_),
             main_interaction_return(std::move(main_interaction_return_)),
             external_frame_return(external_frame_return_),
             main_external_frame_return(std::move(main_external_frame_return_)),
             immediate_continuations(immediate_continuations_),
             main_immediate_continuations(std::move(main_immediate_continuations_)),
             immediate_return(immediate_return_),main_immediate_return(std::move(main_immediate_return_)),
             check_active(check_active_),previous_check(check_active_) {check_active=true;}
        ~RestoreMainState() noexcept {
            std::vector<VmFrame> check_stack;
            check_stack.swap(stack);
            main_stack.swap(stack);
            status=main_status;
            diagnostic.swap(main_diagnostic);
            pending.swap(main_pending);
            suspended_pending.swap(main_suspended_pending);
            interaction_return.swap(main_interaction_return);
            external_frame_return.swap(main_external_frame_return);
            immediate_continuations.swap(main_immediate_continuations);
            immediate_return.swap(main_immediate_return);
            check_active=previous_check;
        }
    } restore{stack_,main_stack,status_,main_status,diagnostic_,std::move(main_diagnostic),
        pending_,std::move(main_pending),suspended_pending_,std::move(main_suspended_pending),
        interaction_return_,std::move(main_interaction_return),external_frame_return_,
        std::move(main_external_frame_return),immediate_continuations_,std::move(main_immediate_continuations),
        immediate_return_,std::move(main_immediate_return),check_active_};
    status_=VmStatus::running;
    diagnostic_.clear();
    pending_.reset();suspended_pending_.clear();
    interaction_return_.reset();external_frame_return_.reset();
    immediate_continuations_.clear();immediate_return_.reset();
    std::uint64_t check_steps=0;
    try {
        push(key,context,arguments,VmCallReturnMode::ordinary,0,exact_arguments,inherited_action_tree);
        result.started=true;
        const auto effective_primitive=primitive_factory?primitive_factory(memory_):check_primitive;
        result.status=run_internal(instruction_budget,{},&effective_primitive,true,&check_steps,nullptr,&result.source_error);
        result.diagnostic=diagnostic_;
    } catch(const std::exception& error) {
        result.source_error=false;
        result.started=!stack_.empty();
        result.status=VmStatus::fault;
        result.diagnostic=error.what();
    } catch(...) {
        result.source_error=false;
        result.started=!stack_.empty();
        result.status=VmStatus::fault;
        result.diagnostic="Check BHAV evaluation raised a non-standard exception";
    }
    if(result.status==VmStatus::budget_exhausted)
        result.diagnostic="check instruction limit reached";
    result.steps=check_steps;

    // The guard restores Main even if setting the result diagnostic allocates
    // and throws. Temp/TempXL, World effects, and consumed dispatch tickets stay.
    return result;
}
VmSubroutineResult Vm::evaluate_entry_point_check(RoutineKey key,VmContext context,
    const std::vector<std::int16_t>& arguments,std::uint64_t budget,bool share,
    const VmPrimitive& primitive,const std::function<void(const VmTrace&)>& trace,bool allow_lifecycle_deletion){
    reject_storage_proof_mutation();
    VmSubroutineResult result;result.status=status_;
    if(executing_||check_active_||external_check_active_||!budget){
        result.diagnostic="entry-point Check requires an idle real Thread and nonzero instruction budget";return result;
    }
    struct Guard{bool& active;explicit Guard(bool& value):active(value){active=true;}~Guard(){active=false;}}guard(external_check_active_);
    try{
        const auto limits=limits_;
        Vm check(program_,ShareVmProgramTag{},memory_,limits,{},owners_,ShareVmOwnerProgramsTag{});
        inherit_disposable_resource_context(check);
        check.resolve_missing_owner(context.code_owner);
        const auto* root=check.resolve(key,context.code_owner);
        if(!root||root->instructions.empty())
            return {true,VmStatus::returned_false,0,"known entry-point routine absent or empty"};
        if(check.start(key,context,arguments,true)!=VmStatus::running){
            result.status=check.status();result.diagnostic=check.diagnostic();return result;
        }
        result=check.run_synchronous_check_with_registers(*this,share,budget,primitive,trace,allow_lifecycle_deletion);
    }catch(const std::exception& error){result.status=VmStatus::fault;result.diagnostic=error.what();}
    catch(...){result.status=VmStatus::fault;result.diagnostic="entry-point Check raised a non-standard exception";}
    return result;
}
VmSubroutineResult Vm::evaluate_check_at_safe_point(RoutineKey key,VmContext context,
    const std::vector<std::int16_t>& arguments,std::uint64_t instruction_budget,
    const VmPrimitive& check_primitive,bool exact_arguments,const VmPrimitiveFactory& primitive_factory) {
    reject_storage_proof_mutation();
    VmSubroutineResult result;result.status=status_;
    if(executing_||check_active_||!instruction_budget){
        result.diagnostic="disposable Check requires a non-reentrant external execution boundary and a nonzero budget";return result;
    }
    struct Guard {bool& active;explicit Guard(bool& a):active(a){active=true;}~Guard(){active=false;}}
        guard(external_check_active_);
    try {
        Vm check(program_,ShareVmProgramTag{},memory_,limits_,{},owners_,ShareVmOwnerProgramsTag{});
        inherit_disposable_resource_context(check);
        if(check.start(key,context,arguments,exact_arguments)!=VmStatus::running){
            result.status=check.status();result.diagnostic=check.diagnostic();return result;
        }
        const auto effective_primitive=primitive_factory?primitive_factory(check.effective_memory()):check_primitive;
        result=check.run_synchronous_check_with_registers(*this,true,instruction_budget,effective_primitive);
        // Source EvaluateCheck shares thread registers even on false/fault.
        // No parent program counter, pending identity, or work is replaced.
    }catch(const std::exception& error){result.status=VmStatus::fault;result.diagnostic=error.what();}
    catch(...){result.status=VmStatus::fault;result.diagnostic="disposable Check raised a non-standard exception";}
    return result;
}
VmSubroutineResult Vm::run_subroutine_in_temporary_stack(
    RoutineKey key, VmContext context, const std::vector<std::int16_t>& arguments,
    std::uint64_t instruction_budget, const std::function<void(const VmTrace&)>& trace) {
    reject_storage_proof_mutation();
    VmSubroutineResult result;
    const bool suspended_sleep = status_ == VmStatus::waiting && pending_ &&
        pending_->instruction.instruction.opcode == 0;
    const bool live_main = !stack_.empty() &&
        (status_ == VmStatus::dormant || status_ == VmStatus::running ||
         status_ == VmStatus::budget_exhausted || suspended_sleep);
    if (executing_ || !live_main || (pending_ && !suspended_sleep) || !suspended_pending_.empty() ||
        interaction_return_ || external_frame_return_ || !immediate_continuations_.empty() || immediate_return_) {
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
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter pending update");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    auto* destination = frame_output(stack_.back(), output,limits_.frame_variables);
    if (!destination) return false;
    *destination = output.value;
    return true;
}
bool Vm::complete(std::uint64_t ticket, bool result, std::optional<VmFrameWrite> output,
                  std::optional<VmStackObjectWrite> stack_object_output) {
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter completion");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket) return false;
    std::int16_t* frame_destination = nullptr;
    if (output) {
        frame_destination = frame_output(stack_.back(), *output,limits_.frame_variables);
        if (!frame_destination) return false;
    }
    if (frame_destination) *frame_destination = output->value;
    if (stack_object_output&&!assign_stack_object(stack_.back().context,stack_object_output->value))return false;
    pending_.reset();
    status_ = VmStatus::running;
    try { branch(result); }
    catch (const VmError& e) { diagnostic_ = e.what(); status_ = VmStatus::fault; }
    return true;
}
bool Vm::complete_next_tick(std::uint64_t ticket,bool result,std::optional<VmFrameWrite> output,
                            std::optional<VmStackObjectWrite> stack_object_output) {
    if(!complete(ticket,result,output,stack_object_output))return false;
    if(status_==VmStatus::running)status_=VmStatus::next_tick;
    return true;
}
bool Vm::complete_call(std::uint64_t ticket,const VmCall& callback,std::optional<std::int16_t> temp0) {
    reject_storage_proof_mutation();
    if(executing_)throw std::logic_error("VM callbacks must not reenter callback completion");
    if(status_!=VmStatus::waiting||!pending_||!ticket||pending_->ticket!=ticket||
       ticket>ticket_sequence_||stack_.empty()||interaction_return_||external_frame_return_||immediate_return_||
       callback.return_mode!=VmCallReturnMode::ordinary||callback.exact_arguments||callback.arguments.size()!=4||
       !routine_key_valid(callback.key))return false;
    const auto& parent=stack_.back();const auto& trace=pending_->instruction;
    // Callback resources may have their own owner; the three entity pointers
    // still have to be exactly those of the protected source frame.
    auto callback_entities=callback.context;
    callback_entities.code_owner=parent.context.code_owner;
    if(trace.depth!=stack_.size()||trace.step>=steps_||trace.key.scope!=parent.key.scope||
       trace.key.id!=parent.key.id||trace.pc!=parent.pc||trace.code_owner!=parent.context.code_owner||
       !vm_context_equal(pending_->context,parent.context)||pending_->context.caller!=parent.context.caller||pending_->context.callee!=parent.context.callee||
       pending_->context.stack_object!=parent.context.stack_object||pending_->context.code_owner!=parent.context.code_owner||
       !vm_context_equal(callback_entities,parent.context)||callback.context.caller!=parent.context.caller||callback.context.callee!=parent.context.callee||
       callback.context.stack_object!=parent.context.stack_object)return false;
    const auto* parent_routine=resolve(parent.key,parent.context.code_owner);
    if(!parent_routine||parent.pc>=parent_routine->instructions.size())return false;
    const auto& original=parent_routine->instructions[parent.pc];const auto& saved=trace.instruction;
    if(original.opcode!=saved.opcode||original.on_true!=saved.on_true||original.on_false!=saved.on_false||
       original.operands!=saved.operands||!can_accept_call(callback))return false;
    // can_accept_call proves the owner is already installed. Consequently push
    // cannot invoke a resolver or publish an owner while validating completion.
    const auto* routine=resolve(callback.key,callback.context.code_owner);
    const auto metadata=routine?execution_metadata(*routine):std::optional<Bhav::Metadata>{};
    if(!metadata)return false;
    try{
        auto arguments=callback.arguments;
        arguments.resize(std::max<std::size_t>(4,metadata->arguments),-1);
        push(callback.key,callback.context,arguments,VmCallReturnMode::ordinary,ticket,false,callback.action_tree_override);
    }catch(...){return false;}
    if(temp0)register_temps()[0]=*temp0;
    pending_.reset();diagnostic_.clear();status_=VmStatus::running;
    return true;
}
bool Vm::retry_pending(std::uint64_t ticket, std::optional<VmFrameWrite> output) {
    reject_storage_proof_mutation();
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
bool Vm::resume_next_tick() {
    reject_storage_proof_mutation();
    if(executing_)throw std::logic_error("VM callbacks must not reenter next-tick resume");
    if(check_active_)throw std::logic_error("synchronous checks must not resume next-tick execution");
    if(status_!=VmStatus::next_tick || stack_.empty() || pending_ || interaction_return_ || external_frame_return_)
        return false;
    status_=VmStatus::running;
    return true;
}
bool Vm::resume_interaction(std::uint64_t ticket) {
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter interaction resume");
    if (status_ != VmStatus::yielded || !interaction_return_ ||
        interaction_return_->ticket != ticket ||
        interaction_return_->run_generation != run_generation_) return false;
    interaction_return_.reset();
    status_ = VmStatus::running;
    return true;
}
bool Vm::can_inject_immediate(const VmCall& call,const ImmediatePendingAudit& audit) const {
    if(storage_proof_active_){storage_proof_reentered_=true;return false;}
    if(executing_||check_active_||external_check_active_||stack_.empty()||!run_generation_||
       (status_!=VmStatus::running&&status_!=VmStatus::budget_exhausted&&status_!=VmStatus::waiting)||
       interaction_return_||external_frame_return_||immediate_return_||
       ticket_sequence_==std::numeric_limits<std::uint64_t>::max()||
       immediate_continuations_.size()>=limits_.stack_depth||call.return_mode!=VmCallReturnMode::ordinary||
       call.context.caller<=0||call.context.caller!=stack_.back().context.caller||
       (call.context.entity_references_bound&&stack_.back().context.entity_references_bound&&
        !(vm_context_reference(call.context,VmEntityRole::caller)==vm_context_reference(stack_.back().context,VmEntityRole::caller)))||!can_accept_call(call))return false;
    std::size_t external=0;
    for(std::size_t depth=0;depth<stack_.size();++depth){
        const auto& frame=stack_[depth];const auto* routine=resolve(frame.key,frame.context.code_owner);
        if(!routine||frame.pc>=routine->instructions.size())return false;
        if(frame.return_mode==VmCallReturnMode::external_frame_next_tick){
            if(external>=suspended_pending_.size())return false;
            const auto& wait=suspended_pending_[external++];
            if(wait.ticket!=frame.invocation_ticket||wait.instruction.depth!=depth||!depth||
               wait.context.caller!=frame.context.caller)return false;
        }
    }
    if(external!=suspended_pending_.size())return false;
    if(status_!=VmStatus::waiting)return !pending_;
    if(!pending_||!audit||pending_->instruction.depth!=stack_.size())return false;
    const auto& frame=stack_.back();const auto* routine=resolve(frame.key,frame.context.code_owner);
    const auto& instruction=routine->instructions[frame.pc];const auto& trace=pending_->instruction;
    if(!pending_->ticket||pending_->ticket>ticket_sequence_||trace.pc!=frame.pc||
       trace.key.scope!=frame.key.scope||trace.key.id!=frame.key.id||trace.code_owner!=frame.context.code_owner||
       !vm_context_equal(pending_->context,frame.context)||pending_->context.caller!=frame.context.caller||pending_->context.callee!=frame.context.callee||
       pending_->context.stack_object!=frame.context.stack_object||pending_->context.code_owner!=frame.context.code_owner||
       trace.instruction.opcode!=instruction.opcode||trace.instruction.operands!=instruction.operands||
       trace.instruction.on_true!=instruction.on_true||trace.instruction.on_false!=instruction.on_false)return false;
    struct Guard {
        bool& active;bool& reentered;
        Guard(bool& a,bool& r):active(a),reentered(r){active=true;reentered=false;}
        ~Guard(){active=false;reentered=false;}
    } guard(storage_proof_active_,storage_proof_reentered_);
    try{return audit(*pending_,frame)&&!storage_proof_reentered_;}catch(...){return false;}
}
std::optional<std::uint64_t> Vm::inject_immediate(const VmCall& call,const ImmediatePendingAudit& audit) {
    reject_storage_proof_mutation();
    if(!can_inject_immediate(call,audit))return {};
    const auto ticket=ticket_sequence_+1;
    VmImmediateContinuation continuation{ticket,stack_.size(),status_,pending_};
    static_assert(std::is_nothrow_move_constructible_v<VmImmediateContinuation>);
    // Complete all allocations before parking the live service or publishing
    // an invocation ticket. push() admits only the already-installed owner.
    try{
        immediate_continuations_.reserve(immediate_continuations_.size()+1);
        stack_.reserve(stack_.size()+1);
        push(call.key,call.context,call.arguments,VmCallReturnMode::immediate,ticket,call.exact_arguments,call.action_tree_override);
    }catch(...){return {};}
    immediate_continuations_.push_back(std::move(continuation));
    ticket_sequence_=ticket;
    pending_.reset();status_=VmStatus::running;
    return ticket;
}
bool Vm::resume_immediate(std::uint64_t ticket) {
    reject_storage_proof_mutation();
    if(executing_||check_active_||status_!=VmStatus::yielded||!immediate_return_||
       immediate_return_->ticket!=ticket||immediate_return_->run_generation!=run_generation_||
       immediate_continuations_.empty()||immediate_continuations_.back().ticket!=ticket||
       stack_.size()!=immediate_continuations_.back().parent_depth||pending_||
       interaction_return_||external_frame_return_)return false;
    auto& continuation=immediate_continuations_.back();
    status_=continuation.parent_status;
    pending_=std::move(continuation.parent_pending);
    immediate_continuations_.pop_back();immediate_return_.reset();
    return true;
}
bool Vm::call_pending(std::uint64_t ticket, const VmCall& call) {
    reject_storage_proof_mutation();
    if (executing_) throw std::logic_error("VM callbacks must not reenter pending child calls");
    if (status_ != VmStatus::waiting || !pending_ || pending_->ticket != ticket ||
        external_frame_return_ || immediate_return_ ||
        call.return_mode != VmCallReturnMode::ordinary) return false;
    suspended_pending_.push_back(*pending_);
    try {
        push(call.key, call.context, call.arguments,
             VmCallReturnMode::external_frame_next_tick, ticket,call.exact_arguments,call.action_tree_override);
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
    reject_storage_proof_mutation();
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
