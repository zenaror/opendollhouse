#pragma once

#include "odh/bootstrap.hpp"
#include "odh/lot_context.hpp"
#include "odh/random.hpp"
#include "odh/session_bundle.hpp"
#include "odh/seated_session_continuation.hpp"
#include <algorithm>
#include <functional>
#include <memory>

namespace odh {

// An unpublished candidate. Prepare restores owned World/relationship/global
// values before the caller constructs VM primitives and controllers against
// them. The live session must remain intact until restore_execution succeeds.
// Legacy Go Here routes and verified generic resource-owned interactions share
// the same resident execution graphs.
struct SessionExecutionBinding {
    std::int16_t id = 0;
    Vm* vm = nullptr;
    EntityThreadControllers* controllers = nullptr;
    Playback* playback = nullptr;
    PlaybackRestoreResolver resolver;
};

class HouseholdSessionRestore {
public:
    using DynamicActionLoader = std::function<std::pair<RoutineSet,
        std::optional<SessionLinkedSemiglobal>>(const SessionDynamicActionOwner&)>;
    using ResourceOwnerLoader = std::function<std::pair<RoutineSet,
        std::optional<SessionLinkedSemiglobal>>(const SessionResourceCodeOwner&)>;
    static std::unique_ptr<HouseholdSessionRestore> prepare(
        const std::vector<std::uint8_t>& archive,
        const SessionResourceIdentity& expected_resources,
        const SessionHostState& expected_selection,
        const std::vector<SessionHouseholdMember>& expected_roster,
        const WorldSnapshot& original_lot,
        const LotContext& original_globals,
        const Ts1BootstrapGlobals& bootstrap,
        std::optional<std::int16_t> family_global,
        SessionBundleLimits limits = {},
        const SessionCreatedObjectResolver& created_objects = {});

    World& world() {
        if(!world_)throw FormatError("candidate World ownership was transferred to the host");
        return *world_;
    }
    // A host whose lot container owns World may transfer the candidate before
    // binding execution/rendering. Keep the returned World alive through restore.
    // world() must not be called after this one-time transfer.
    std::unique_ptr<World> take_world() {
        if(attempted_||!world_)throw FormatError("candidate World can only transfer once before execution restore");
        return std::move(world_);
    }
    RelationshipState& relationships() { return relationships_; }
    LotContext& lot() { return lot_; }
    Ts1BootstrapGlobals& bootstrap() { return bootstrap_; }
    std::optional<std::int16_t>& family_global() { return family_global_; }
    const std::optional<SessionSimulationState>& simulation() const {return bundle_.simulation;}
    const std::optional<std::int32_t>& family_budget() const {return bundle_.host.family_budget;}
    const SessionHostState& host() const { return bundle_.host; }
    const std::optional<SessionUiState>& ui_state() const { return bundle_.ui_state; }
    const std::vector<SessionObjectThread>& object_threads() const { return bundle_.object_threads; }
    bool generic_execution() const {return bundle_.generic_execution;}
    CodeOwner next_code_owner() const {return bundle_.next_code_owner;}
    bool code_owner_ids_exhausted() const {return bundle_.code_owner_ids_exhausted;}
    const std::vector<SessionCreatedObject>& created_objects() const {return bundle_.created_objects;}
    const std::optional<SessionObjectHistory>& object_history() const {return bundle_.object_history;}
    const std::vector<SessionDeferredRemoval>& deferred_removals() const {return bundle_.deferred_removals;}
    const SessionSeatedContinuation* seated_continuation(std::int16_t id) const {
        return find_seated_session_continuation(bundle_,id);
    }
    void install_seated_continuation_owners(std::int16_t id, Vm&, const DefinitionCandidate&,
        const std::vector<EntryPoint>&, const SessionActionRoutineLoader&);
    const std::vector<SessionResourceCodeOwner>& resource_owners(std::int16_t id) const;
    void install_resource_owners(std::int16_t id,Vm&,const ResourceOwnerLoader&);
    const std::vector<SessionDynamicActionOwner>& dynamic_action_owners(std::int16_t id) const;
    void install_dynamic_action_owners(std::int16_t id, Vm&, const DynamicActionLoader&);
    const LocalRouteSnapshot* local_route(std::int16_t id) const {
        const auto found=std::find_if(bundle_.runtime_entities.begin(),bundle_.runtime_entities.end(),
            [&](const auto& entity){return entity.id==id&&entity.route.has_value();});
        return found==bundle_.runtime_entities.end()?nullptr:&*found->route;
    }
    // Every binding is an unpublished fresh execution graph against this World.
    // Failure invalidates all candidate graphs; discard them without publication.
    // Tick/RNG are committed only after the entire roster and scheduler succeed.
    void restore_execution(const std::vector<SessionExecutionBinding>&,
        EntityVmScheduler&, RandomState&, std::uint64_t& tick);
private:
    HouseholdSessionRestore(SessionBundle, const LotContext&,
        const Ts1BootstrapGlobals&, std::optional<std::int16_t>, WorldLimits,
        RelationshipLimits);
    SessionBundle bundle_;
    std::unique_ptr<World> world_;
    RelationshipState relationships_;
    LotContext lot_;
    Ts1BootstrapGlobals bootstrap_;
    std::optional<std::int16_t> family_global_;
    bool attempted_ = false;
    std::optional<std::int16_t> dynamic_owner_resident_;
    const Vm* dynamic_owner_vm_ = nullptr;
    bool dynamic_owners_verified_ = false;
    std::map<std::int16_t,const Vm*> resource_owner_vms_;
    std::map<std::int16_t,const Vm*> seated_owner_vms_;
};

} // namespace odh
