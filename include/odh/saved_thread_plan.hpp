#pragma once

#include "odh/saved_world.hpp"

namespace odh {

// Read-only representation of the frame conversion used by the pinned
// Simitone TS1 activator. This is a preparation plan, not restored execution:
// it does not interpret return behavior or claim equivalence to the original.
struct SimitoneSavedFramePlan {
    std::uint16_t routine_id;
    std::uint16_t instruction_pointer;
    std::int16_t stack_object;
    std::int32_t saved_primitive_state; // Preserved; Simitone's converter ignores it.
    std::uint16_t code_owner_objt_type;
    std::uint32_t code_owner_guid;
    std::int16_t caller;
    std::int16_t callee;
    std::vector<std::int16_t> arguments; // Simitone pads to at least four words.
    std::vector<std::int16_t> locals;
};

struct SimitoneSavedThreadPlan {
    std::int16_t entity_id;
    std::array<std::int16_t, 20> temp_registers{}; // Saved eight; rest are zero-filled.
    std::vector<SimitoneSavedFramePlan> frames;
};

// Resolve frame code-owner type indices through the serialized OBJT table,
// preserve the saved stack, and apply Simitone's ObjectUses callee mapping.
// Refuses unresolved owner indices/GUIDs and invalid ObjectUses stack lengths.
SimitoneSavedThreadPlan make_simitone_saved_thread_plan(
    const SavedWorldEntity&, const Objt&);

} // namespace odh
