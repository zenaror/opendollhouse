#pragma once

#include "odh/saved_world.hpp"
#include "odh/vm.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace odh {

enum class SimitoneSavedReturnState { unresolved, normal, retry, interaction };

struct SimitoneSavedObjectUse {
    std::int16_t target_id;
    std::int32_t stack_length;
};

// Diagnostic representation of the frame conversion used by the pinned
// Simitone TS1 activator. This is a preparation plan, not restored execution
// and does not claim equivalence to the original.
struct SimitoneSavedFramePlan {
    std::uint16_t routine_id;
    std::uint16_t instruction_pointer;
    std::int16_t stack_object;
    std::int32_t saved_primitive_state; // Preserved; Simitone's converter ignores it.
    std::uint16_t code_owner_objt_type;
    std::uint32_t code_owner_guid;
    std::int16_t caller;
    std::int16_t callee;
    SimitoneSavedReturnState return_state = SimitoneSavedReturnState::unresolved;
    bool action_tree = false;
    std::vector<std::int16_t> arguments; // Simitone pads to at least four words.
    std::vector<std::int16_t> locals;
};

struct SimitoneSavedThreadPlan {
    std::int16_t entity_id;
    std::array<std::int16_t, 20> temp_registers{}; // Saved eight; rest are zero-filled.
    std::vector<SimitoneSavedFramePlan> frames;
    std::vector<SimitoneSavedObjectUse> object_uses;
};

using SimitoneSavedOpcodeLookup =
    std::function<std::optional<std::uint16_t>(const SimitoneSavedFramePlan&)>;

// Reconstructs the return/action flags used by the pinned Simitone TS1 adapter.
// The callback reads the saved instruction of the calling frame; this does not
// establish that the original game uses the same mapping.
void apply_simitone_saved_return_mapping(SimitoneSavedThreadPlan&,
                                         const SimitoneSavedOpcodeLookup&);

// Load plans whose routines are already installed in the VM and whose return
// state is supported. The VM validates every routine and address; it remains
// dormant and does not execute Main. The caller supplies the intended resources.
void restore_simitone_saved_thread_plan(Vm&, const SimitoneSavedThreadPlan&);

// Owns a decoded saved World and the imported VM state of its saved avatars.
// Admission binds each dormant VM to the World entity generation. Only const
// VM views are exposed; this session offers no activation or scheduler path.
class SavedWorldSession {
public:
    explicit SavedWorldSession(std::unique_ptr<SavedWorld>, std::size_t thread_limit = 64);
    const SavedWorld& saved_world() const { return *saved_; }
    World& world() { return *saved_->world; }
    void retain_dormant_thread(std::int16_t entity_id, std::unique_ptr<Vm>);
    const Vm* dormant_thread(std::int16_t entity_id) const;
    bool all_dormant_bindings_valid() const;
    std::size_t thread_count() const { return threads_.size(); }
private:
    struct Thread { std::uint64_t entity_generation; std::unique_ptr<Vm> vm; };
    bool valid_binding(std::int16_t, const Thread&) const;
    std::unique_ptr<SavedWorld> saved_;
    std::size_t thread_limit_;
    std::map<std::int16_t, Thread> threads_;
};

// Resolve frame code-owner type indices through the serialized OBJT table,
// preserve the saved stack, and apply Simitone's ObjectUses callee mapping.
// Refuses unresolved owner indices/GUIDs and invalid ObjectUses stack lengths.
SimitoneSavedThreadPlan make_simitone_saved_thread_plan(
    const SavedWorldEntity&, const Objt&);

} // namespace odh
