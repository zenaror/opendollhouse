#include "odh/saved_thread_plan.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace odh {

SavedWorldSession::SavedWorldSession(std::unique_ptr<SavedWorld> saved,
                                     std::size_t thread_limit)
    : saved_(std::move(saved)), thread_limit_(thread_limit) {
    if (!saved_ || !saved_->world || !thread_limit_ || thread_limit_ > 64)
        throw FormatError("saved World session needs a World and a bounded thread capacity");
}

void SavedWorldSession::retain_dormant_thread(std::int16_t id, std::unique_ptr<Vm> vm) {
    if (id <= 0 || !vm || vm->status() != VmStatus::dormant || vm->steps() != 0 ||
        vm->stack().empty())
        throw FormatError("saved thread must be a nonempty dormant VM with no executed steps");
    if (threads_.count(id) || threads_.size() >= thread_limit_)
        throw FormatError("saved World session has a duplicate thread or reached its thread limit");
    const auto* entity = saved_->world->find(id);
    const auto saved_entity = std::find_if(saved_->records.begin(), saved_->records.end(),
        [id](const SavedWorldEntity& record) {
            return record.saved.object_id == static_cast<std::uint16_t>(id) &&
                !record.common.stack.empty();
        });
    if (!entity || saved_entity == saved_->records.end())
        throw FormatError("saved thread entity is absent or has no saved stack");
    if (saved_entity->common.stack.size() != vm->stack().size())
        throw FormatError("saved thread VM frame count differs from its saved entity");
    if (std::any_of(vm->stack().begin(), vm->stack().end(),
            [id](const VmFrame& frame) { return frame.context.caller != id; }))
        throw FormatError("saved thread frames do not belong to their saved entity");
    threads_.emplace(id, Thread{entity->generation, std::move(vm)});
}

bool SavedWorldSession::valid_binding(std::int16_t id, const Thread& thread) const {
    const auto* entity = saved_->world->find(id);
    return entity && entity->generation == thread.entity_generation && thread.vm &&
        thread.vm->status() == VmStatus::dormant && thread.vm->steps() == 0;
}

const Vm* SavedWorldSession::dormant_thread(std::int16_t id) const {
    const auto found = threads_.find(id);
    return found != threads_.end() && valid_binding(id, found->second) ?
        found->second.vm.get() : nullptr;
}

bool SavedWorldSession::all_dormant_bindings_valid() const {
    return std::all_of(threads_.begin(), threads_.end(),
        [this](const auto& item) { return valid_binding(item.first, item.second); });
}

namespace {
void restore_saved_thread_plan(Vm& vm, const SimitoneSavedThreadPlan& plan,
                              const SavedThreadCodeOwnerMapper* mapper,
                              VmSavedRoutinePolicy policy = VmSavedRoutinePolicy::require_present) {
    std::vector<VmFrame> frames;
    frames.reserve(plan.frames.size());
    std::map<std::uint32_t,CodeOwner> mapped_owners;
    std::vector<bool> requires_return_mapping(plan.frames.size(),false);
    for(const auto& use:plan.object_uses){
        if(use.stack_length<=0||static_cast<std::size_t>(use.stack_length)>plan.frames.size()||use.target_id<=0)
            throw FormatError("saved thread ObjectUses entry does not identify a frame and target");
        // A one-frame ObjectUses entry names the Main entity but has no caller
        // frame whose opcode could override Simitone's Normal default.
        if(use.stack_length>1)
            requires_return_mapping[static_cast<std::size_t>(use.stack_length-1)]=true;
    }
    for (std::size_t n=0;n<plan.frames.size();++n) {
        const auto& saved=plan.frames[n];
        // Simitone initializes every imported frame to Normal. The saved
        // ObjectUses records may override that state for person threads after
        // the caller opcode is audited. Object threads have no PersonData
        // ObjectUses bank, so their unannotated frames retain that default.
        // A frame referenced at depth > 1 must be audited before Normal can be
        // distinguished from Retry or Interaction.
        const auto return_state=saved.return_state==SimitoneSavedReturnState::unresolved&&
            !requires_return_mapping[n]?SimitoneSavedReturnState::normal:saved.return_state;
        if (return_state != SimitoneSavedReturnState::normal || saved.action_tree)
            throw FormatError("saved thread return/action state is not supported by VM import");
        auto owner=saved.code_owner_guid;
        if(mapper) {
            const auto found=mapped_owners.find(saved.code_owner_guid);
            if(found!=mapped_owners.end())owner=found->second;
            else {
                if(!saved.code_owner_guid || !(owner=(*mapper)(saved.code_owner_guid)))
                    throw FormatError("saved thread resource GUID has no host code-owner mapping");
                mapped_owners.emplace(saved.code_owner_guid,owner);
            }
        }
        frames.push_back({routine_key(saved.routine_id), saved.instruction_pointer,
            {saved.caller, saved.callee, saved.stack_object, owner},
            saved.arguments, saved.locals, VmCallReturnMode::ordinary, 0});
    }
    vm.restore_dormant_state(std::move(frames), plan.temp_registers,policy);
}
} // namespace

void restore_simitone_saved_thread_plan(Vm& vm,const SimitoneSavedThreadPlan& plan) {
    restore_saved_thread_plan(vm,plan,nullptr);
}

void restore_simitone_saved_thread_plan(Vm& vm,const SimitoneSavedThreadPlan& plan,
                                      const SavedThreadCodeOwnerMapper& mapper,VmSavedRoutinePolicy policy) {
    if(!mapper)throw FormatError("saved thread requires a resource code-owner resolver");
    restore_saved_thread_plan(vm,plan,&mapper,policy);
}

void apply_simitone_saved_return_mapping(
    SimitoneSavedThreadPlan& plan, const SimitoneSavedOpcodeLookup& opcode_for) {
    if (!opcode_for && std::any_of(plan.object_uses.begin(), plan.object_uses.end(),
            [](const auto& use) { return use.stack_length > 1; }))
        throw FormatError("saved thread return mapping needs a caller-opcode lookup");

    for (const auto& use : plan.object_uses) {
        if (use.stack_length <= 0 ||
            static_cast<std::size_t>(use.stack_length) > plan.frames.size() || use.target_id <= 0)
            throw FormatError("saved thread ObjectUses entry does not identify a frame and target");
        const auto first = static_cast<std::size_t>(use.stack_length - 1);
        if (use.stack_length == 1) continue;
        const auto calling_index = first - 1;
        const auto opcode = opcode_for(plan.frames[calling_index]);
        if (!opcode)
            throw FormatError("saved thread calling BHAV instruction is unavailable");
        auto& returned = plan.frames[first];
        if (*opcode == 27 || *opcode == 45) {
            returned.return_state = SimitoneSavedReturnState::retry;
        } else if (*opcode == 30 || *opcode == 17) {
            returned.return_state = SimitoneSavedReturnState::interaction;
            for (std::size_t i = first; i < plan.frames.size(); ++i)
                plan.frames[i].action_tree = true;
        } else {
            returned.return_state = SimitoneSavedReturnState::normal;
        }
    }
}

SimitoneSavedThreadPlan make_simitone_saved_thread_plan(
    const SavedWorldEntity& entity, const Objt& types) {
    if (entity.saved.object_id == 0 ||
        entity.saved.object_id > static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max()))
        throw FormatError("saved thread owner ID is outside the runtime identity range");
    if (entity.common.stack.size() > 1024)
        throw FormatError("saved thread frame count exceeds plan limit");

    SimitoneSavedThreadPlan result{};
    result.entity_id = static_cast<std::int16_t>(entity.saved.object_id);
    for (std::size_t i = 0; i < entity.saved.prefix.temps.size(); ++i)
        result.temp_registers[i] = entity.saved.prefix.temps[i];

    result.frames.reserve(entity.common.stack.size());
    for (const auto& saved : entity.common.stack) {
        if (saved.code_owner_obj_type <= 0)
            throw FormatError("saved thread frame has invalid OBJT code-owner type");
        const auto owner_type = static_cast<std::uint16_t>(saved.code_owner_obj_type);
        const auto* owner = types.entry_for_type(owner_type);
        if (!owner || !owner->guid)
            throw FormatError("saved thread frame code-owner GUID is unavailable");

        SimitoneSavedFramePlan frame{};
        frame.routine_id = static_cast<std::uint16_t>(saved.tree_id);
        frame.instruction_pointer = static_cast<std::uint16_t>(saved.node_id);
        frame.stack_object = saved.stack_object_id;
        frame.saved_primitive_state = saved.primitive_state;
        frame.code_owner_objt_type = owner_type;
        frame.code_owner_guid = owner->guid;
        frame.caller = result.entity_id;
        frame.callee = result.entity_id;
        frame.arguments = saved.parameters;
        if (frame.arguments.size() < 4) frame.arguments.resize(4, 0);
        frame.locals = saved.locals;
        result.frames.push_back(std::move(frame));
    }

    if (entity.tail.person) {
        result.object_uses.reserve(entity.tail.person->object_uses.size());
        for (const auto& use : entity.tail.person->object_uses) {
            if (use.stack_length <= 0 ||
                static_cast<std::size_t>(use.stack_length) > result.frames.size() ||
                use.target_id <= 0)
                throw FormatError("saved thread ObjectUses entry does not identify a frame and target");
            const auto first = static_cast<std::size_t>(use.stack_length - 1);
            for (std::size_t i = first; i < result.frames.size(); ++i)
                result.frames[i].callee = use.target_id;
            result.object_uses.push_back({use.target_id, use.stack_length});
        }
    }
    return result;
}

} // namespace odh
