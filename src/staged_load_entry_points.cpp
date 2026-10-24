#include "odh/staged_load_entry_points.hpp"
#include <algorithm>
#include <set>

namespace odh {

StagedLoadEntryPointResult stage_saved_load_entry_points(
    const WorldSnapshot& source, const std::vector<std::int16_t>& entity_order,
    const std::map<std::int16_t, CodeOwner>& code_owners,
    const std::map<std::int16_t, bool>& ghost_images,
    const std::map<std::int16_t, std::array<std::int16_t, 20>>& temporary_registers,
    const RoutineSet& globals, const RoutineOwners& owners,
    std::uint64_t instruction_budget, WorldLimits world_limits) {
    StagedLoadEntryPointResult result;
    auto reject = [&](std::string reason, std::int16_t entity = 0,
                      std::uint16_t action = 0, VmStatus status = VmStatus::ready) {
        result.snapshot.reset();
        result.failed_entity = entity;
        result.failed_action = action;
        result.vm_status = status;
        result.reason = std::move(reason);
        return result;
    };

    if (entity_order.size() != source.entities.size())
        return reject("entity order does not cover the complete World");
    if (ghost_images.size() != source.entities.size())
        return reject("GhostImage state is not known for every entity");
    if (temporary_registers.size() != source.entities.size())
        return reject("saved temporary registers are not known for every entity");
    std::set<std::int16_t> seen;
    for (const auto id : entity_order) {
        if (!seen.insert(id).second) return reject("entity order contains a duplicate ID", id);
        const auto found = std::find_if(source.entities.begin(), source.entities.end(),
            [id](const Entity& entity) { return entity.id == id; });
        if (found == source.entities.end()) return reject("entity order contains an unknown ID", id);
        if (!ghost_images.count(id)) return reject("GhostImage state is unknown for entity", id);
        if (!temporary_registers.count(id)) return reject("saved temporary registers are unknown for entity", id);
    }

    World candidate(world_limits, source.attribute_policy);
    if (!candidate.restore_snapshot(source)) return reject("source World exceeds candidate limits");
    Vm vm(globals, candidate.vm_memory(), {}, {}, owners);

    for (const auto id : entity_order) {
        const auto* entity = candidate.find(id);
        if (!entity || !entity->entry_points)
            return reject("entity has no resolved EntryPoint table", id);
        if (entity->entry_points->size() <= 2)
            return reject("entity EntryPoint table has no Load slot", id);
        const auto action = entity->entry_points->at(2).action;
        if (action <= 255) {
            result.completed_entities.push_back(id);
            continue;
        }
        const auto owner = code_owners.find(id);
        if (owner == code_owners.end() || owner->second == 0 || !owners.count(owner->second))
            return reject("executable Load action has no exact resource CodeOwner", id, action);
        if (result.instructions >= instruction_budget)
            return reject("aggregate instruction budget exhausted", id, action,
                          VmStatus::budget_exhausted);

        const auto remaining = instruction_budget - result.instructions;
        VmStatus status;
        std::string diagnostic;
        if (ghost_images.at(id)) {
            World sandbox(world_limits, source.attribute_policy);
            if (!sandbox.restore_snapshot(candidate.snapshot()))
                return reject("cannot create isolated GhostImage sandbox", id, action);
            Vm sandbox_vm(globals, sandbox.vm_memory(), {}, {}, owners);
            const VmContext context{id, id, 0, owner->second};
            const auto started = sandbox_vm.start(routine_key(action), context, {0, 0, 0, 0});
            if (started == VmStatus::running) sandbox_vm.temps() = temporary_registers.at(id);
            status = started == VmStatus::running ? sandbox_vm.run(remaining) : started;
            result.instructions += sandbox_vm.steps();
            diagnostic = sandbox_vm.diagnostic();
        } else {
            const VmContext context{id, id, 0, owner->second};
            const auto started = vm.start(routine_key(action), context, {0, 0, 0, 0});
            if (started == VmStatus::running) vm.temps() = temporary_registers.at(id);
            status = started == VmStatus::running ? vm.run(remaining) : started;
            result.instructions += vm.steps();
            diagnostic = vm.diagnostic();
        }
        if (status != VmStatus::returned_true && status != VmStatus::returned_false)
            return reject("Load action did not complete synchronously: " + diagnostic,
                          id, action, status);
        result.completed_entities.push_back(id);
    }

    result.snapshot = candidate.snapshot();
    result.reason = "all Load actions completed synchronously in the unpublished candidate";
    return result;
}

} // namespace odh
