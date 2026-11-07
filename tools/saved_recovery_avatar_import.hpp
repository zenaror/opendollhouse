// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/saved_thread_plan.hpp"
#include "odh/entity_thread_controllers.hpp"
#include <functional>

namespace odh_host {
struct SavedRecoveryAvatarOwner {
    odh::CodeOwner owner=0;
    std::shared_ptr<const odh::VmProgram> program;
};
struct SavedRecoveryAvatarActionSource {
    odh::RoutineKey key{};
    SavedRecoveryAvatarOwner code;
    std::optional<std::int32_t> interaction_number;
};
struct SavedRecoveryAvatarBinding {
    odh::WorldEntityReference person{};
    odh::Vm* thread=nullptr;
    odh::InteractionQueue* queue=nullptr;
    odh::EntityThreadControllers* controllers=nullptr;
    const odh::SavedWorldEntity* source=nullptr;
};
struct SavedRecoveryAvatarImportContext {
    odh::World* world=nullptr;
    const odh::Objt* types=nullptr;
    std::function<bool()> source_still_matches;
    std::function<std::optional<odh::WorldEntityReference>(std::int16_t)> original_reference;
    // Exact frozen owner already installed in THIS avatar VM. Includes complete
    // private/linked semiglobal banks; global calls retain that same context.
    std::function<std::optional<SavedRecoveryAvatarOwner>(odh::WorldEntityReference,std::uint32_t)> resolve_owner;
    // Read-only physical-resource/TTAB proof, preserving raw ActionTreeID. This
    // does not execute a check or infer a current action from a public label.
    std::function<std::optional<SavedRecoveryAvatarActionSource>(odh::WorldEntityReference,
        odh::WorldEntityReference,const odh::ObjmSavedInteraction&)> resolve_action;
    // REQUIRED positive proof from the complete class-recovery plan. Neither
    // opaque saved frames nor native active headers can be resumed by this
    // bridge: every imported avatar must finish full self recovery before tick.
    std::function<bool(odh::WorldEntityReference)> will_reset_before_dispatch;
    odh::VmLimits vm_limits{}; // actual host limits, not a promise of larger banks
    std::size_t avatars=4096,frames=65536,words=1024*1024,actions=65536;
};
struct SavedRecoveryAvatarImportResult {
    bool completed=false,discard_candidate=false;
    std::size_t avatars=0;
    std::string diagnostic;
};
// Whole-cohort read-only preparation, followed by publication into the SAME
// World/VM/controller graph. No second VM, BHAV execution or invented return
// ticket. Frames preserve raw IDs/PCs and ordinary opaque unwind ownership;
// they remain dormant, and only the proven reset may consume them.
class SavedRecoveryAvatarImport {
public:
    explicit SavedRecoveryAvatarImport(SavedRecoveryAvatarImportContext);
    bool prepare(const std::vector<SavedRecoveryAvatarBinding>&,std::string&);
    SavedRecoveryAvatarImportResult commit();
private:
    struct Prepared {
        SavedRecoveryAvatarBinding binding;
        odh::SimitoneSavedThreadPlan source_plan;
        odh::VmSnapshot before,execution;
        odh::EntityThreadControllersSnapshot controllers_before;
        odh::InteractionQueueSnapshot queue;
        std::vector<std::pair<odh::CodeOwner,std::shared_ptr<const odh::VmProgram>>> owners;
    };
    SavedRecoveryAvatarImportContext context_;
    std::vector<Prepared> prepared_;
    bool ready_=false,consumed_=false;
    bool current(const Prepared&,std::string&) const;
};
} // namespace odh_host
