// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "saved_recovery_avatar_import.hpp"
#include "saved_recovery_person.hpp"
#include <memory>

namespace odh_host {
struct SavedRecoveryPersonHostContext {
    SavedRecoveryAvatarImportContext avatar_import;
    SavedRecoveryPersonContext person;
    // Must authenticate the actual runtime registry BEFORE borrowed controllers
    // or Threads are read. False after replacement/release, even at a reused ID.
    std::function<bool(odh::WorldEntityReference,const odh::Vm*,
        const odh::InteractionQueue*,const odh::EntityThreadControllers*)> binding_still_matches;
};
// One private original-load candidate. Resource banks must already be installed
// and prewarmed by the app. prepare is read-only; commit hydrates actual owners
// and admits PersonService against those exact frames/queues. Any commit error
// after hydration requires discarding that private candidate, never rollback.
class SavedRecoveryPersonHost {
public:
    explicit SavedRecoveryPersonHost(SavedRecoveryPersonHostContext);
    ~SavedRecoveryPersonHost();
    SavedRecoveryPersonHost(const SavedRecoveryPersonHost&)=delete;
    SavedRecoveryPersonHost& operator=(const SavedRecoveryPersonHost&)=delete;
    bool prepare(const std::vector<SavedRecoveryAvatarBinding>&,std::string&);
    SavedRecoveryAvatarImportResult commit();
    bool supports(const odh::SavedRecoveryCall&,std::string&) const;
    bool execute(const odh::SavedRecoveryCall&,std::string&);
    bool ready() const;
    // Hydrated opaque continuations remain forbidden to dispatch/save until
    // every plan-required self reset reaches person_actions, or is deleted.
    bool dispatch_ready() const;
private:
    struct State;std::unique_ptr<State> state_;
};
}
