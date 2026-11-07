// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/saved_object_recovery.hpp"
#include "odh/saved_world.hpp"
#include "odh/entity_thread_controllers.hpp"
#include "odh/user_interaction_tracker.hpp"
#include <map>
#include <set>

namespace odh_host {
struct SavedRecoveryPersonBinding {
    odh::WorldEntityReference person{};
    odh::Vm* thread=nullptr;
    odh::InteractionQueue* queue=nullptr;
    odh::EntityThreadControllers* controllers=nullptr;
    const odh::SavedWorldEntity* source=nullptr; // copied by prepare; not retained
};
struct SavedRecoveryPersonContext {
    odh::World* world=nullptr;
    odh::UserInteractionTracker* tracker=nullptr;
    std::function<bool()> source_still_matches;
    // Bind from original decoded records/imported_generation BEFORE callbacks.
    // Numeric ID reuse must never authenticate a replacement as a saved pointer.
    std::function<std::optional<odh::WorldEntityReference>(std::int16_t)> original_reference;
    // Source internal target+114 bit12, NOT an ObjectData flag. Unknown rejects.
    std::function<std::optional<bool>(odh::WorldEntityReference)> suppress_referenced_reset;
    // Read-only proof of the actual lifecycle consumers below.
    std::function<bool(odh::WorldEntityReference,odh::SavedRecoveryPhase,std::string&)> supports_phase;
    // Actual external services, visual reset, placement, Main restart, human EP3.
    // person_unwind is called AFTER this service releases bundle waits, without
    // deleting queue headers or VM frames. restart_main only prepares Main;
    // this service then rebases its bundle before EP3. No constructor Init.
    std::function<bool(odh::WorldEntityReference,odh::SavedRecoveryPhase,std::string&)> execute_phase;
    // Mutate the actual queue before EP4. Active removal is distinct from cancel.
    std::function<bool(odh::WorldEntityReference,std::uint64_t,bool,std::string&)> remove_action;
    // Native active header UID may exist without a running queue.active ticket.
    std::function<bool(odh::WorldEntityReference,std::uint64_t,odh::WorldEntityReference,std::string&)> set_active_icon;
    // Pop actual obsolete frames without executing them; preserve TEMP/XL/code.
    std::function<bool(odh::WorldEntityReference,std::size_t,std::string&)> pop_frames;
    std::function<bool(odh::WorldEntityReference,odh::WorldEntityReference,std::string&)> supports_entry4;
    // Native45ee5d calls the target's EP4 with StackObject=person and NULL:
    // the execution owner copies target TEMP defaults using routine metadata.
    // Queued action arguments do not supply this event's parameters.
    std::function<bool(odh::WorldEntityReference,odh::WorldEntityReference,std::string&)> skipped_entry4;
    std::function<bool(odh::WorldEntityReference,std::string&)> update_footprint;
    std::size_t operations=65536;
};
// Owns native original-import use/action metadata while mutating ONLY the real
// World, thread and queue. Untracked use/frame changes are unsupported, not a
// reconstruction from arbitrary StackObject references. One instance per load.
class SavedRecoveryPersonService {
public:
    explicit SavedRecoveryPersonService(SavedRecoveryPersonContext);
    bool prepare(const std::vector<SavedRecoveryPersonBinding>&,std::string&);
    bool supports(const odh::SavedRecoveryCall&,std::string&) const;
    bool execute(const odh::SavedRecoveryCall&,std::string&);
private:
    struct Use {odh::WorldEntityReference target;std::size_t depth;std::uint8_t flags;};
    struct State {
        SavedRecoveryPersonBinding binding;
        std::vector<Use> uses;
        odh::WorldEntityReference active_target{},active_icon{},last_target{},last_icon{};
        std::optional<std::uint64_t> active_uid;
        std::vector<odh::VmFrame> frames;
        std::uint64_t run=0;
    };
    SavedRecoveryPersonContext context_;
    std::map<odh::WorldEntityReference,State> people_;
    std::set<odh::WorldEntityReference> resetting_;
    bool prepared_=false;
    std::size_t operations_=0;
    bool identity_current(const State&,std::string&) const;
    bool current(State&,std::string&) const;
    bool clear_active(State&,std::string&);
    bool unwind(State&,const odh::SavedRecoveryCall&,std::string&);
    bool discard_queued(State&,std::optional<odh::WorldEntityReference>,std::string&);
    bool notify(State&,const odh::SavedRecoveryCall&,std::string&);
    bool phase(State&,odh::SavedRecoveryPhase,std::string&);
    bool charge(std::string&);
};
} // namespace odh_host
