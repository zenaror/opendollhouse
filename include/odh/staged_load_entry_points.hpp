#pragma once

#include "odh/world.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace odh {

struct StagedLoadEntryPointResult {
    std::optional<WorldSnapshot> snapshot;
    std::vector<std::int16_t> completed_entities;
    std::uint64_t instructions = 0;
    std::int16_t failed_entity = 0;
    std::uint16_t failed_action = 0;
    VmStatus vm_status = VmStatus::ready;
    std::string reason;
};

// Runs Load (EntryPoint 2) in the supplied entity order against a private World
// copy. The caller supplies the complete lot order and exact resource CodeOwner
// for each entity with an executable action, GhostImage status, and 20 saved
// temporary registers for every entity. GhostImage actions run against an extra
// disposable World copy. No state is published to the input World; snapshot is
// present only if the entire batch completed synchronously.
// Conditions are retained in the entity table but, like Simitone's generic
// immediate EntryPoint dispatch, are not evaluated here.
StagedLoadEntryPointResult stage_saved_load_entry_points(
    const WorldSnapshot& source, const std::vector<std::int16_t>& entity_order,
    const std::map<std::int16_t, CodeOwner>& code_owners,
    const std::map<std::int16_t, bool>& ghost_images,
    const std::map<std::int16_t, std::array<std::int16_t, 20>>& temporary_registers,
    const RoutineSet& globals, const RoutineOwners& owners,
    std::uint64_t instruction_budget, WorldLimits world_limits = {});

} // namespace odh
