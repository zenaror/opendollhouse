#pragma once

#include "odh/world.hpp"
#include "odh/sound.hpp"
#include "odh/room_score.hpp"
#include "odh/random.hpp"
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace odh {

struct StagedLoadEntryPointResult {
    std::optional<WorldSnapshot> snapshot;
    std::vector<std::int16_t> completed_entities;
    std::vector<std::pair<std::int16_t, std::uint16_t>> missing_root_actions;
    std::uint64_t instructions = 0;
    std::int16_t failed_entity = 0;
    std::uint16_t failed_action = 0;
    std::uint16_t failed_routine = 0;
    std::uint16_t failed_pc = 0;
    std::uint16_t failed_opcode = 0;
    VmStatus vm_status = VmStatus::ready;
    std::string reason;
    // Resolved sound events remain pending until the full batch is accepted.
    std::vector<SoundRequest> deferred_sound_events;
    std::size_t unresolved_sound_events = 0;
    std::size_t discarded_sound_events = 0;
    std::size_t cancelled_sound_events = 0;
    // Room scores are staged with the candidate and published only on success.
    std::map<std::uint16_t, std::int16_t> room_scores;
    std::size_t discarded_room_score_refreshes = 0;
    // Present only on success when the caller supplied an explicit seed.
    std::optional<std::uint64_t> random_seed;
    // Architecture permission and notifications remain private until the whole
    // candidate succeeds. GhostImage effects are discarded with their sandbox.
    std::optional<bool> build_buy_enabled;
    std::vector<bool> deferred_build_buy_events;
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
    std::uint64_t instruction_budget, WorldLimits world_limits = {},
    VmMemory supplemental_memory = {}, VmPrimitive supplemental_primitive = {},
    SoundResolver sound_resolver = {}, const GeometricRoomMap* room_map = nullptr,
    std::optional<std::uint64_t> random_seed = {},
    bool initial_build_buy_enabled = true);

} // namespace odh
