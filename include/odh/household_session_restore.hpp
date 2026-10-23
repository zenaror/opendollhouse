#pragma once

#include "odh/bootstrap.hpp"
#include "odh/lot_context.hpp"
#include "odh/random.hpp"
#include "odh/session_bundle.hpp"
#include <memory>

namespace odh {

// An unpublished candidate. Prepare restores owned World/relationship/global
// values before the caller constructs VM primitives and controllers against
// them. The live session must remain intact until restore_execution succeeds.
// Routes, dynamic owners and seated continuation remain unsupported.
struct SessionExecutionBinding {
    std::int16_t id = 0;
    Vm* vm = nullptr;
    EntityThreadControllers* controllers = nullptr;
    Playback* playback = nullptr;
    PlaybackRestoreResolver resolver;
};

class HouseholdSessionRestore {
public:
    static std::unique_ptr<HouseholdSessionRestore> prepare(
        const std::vector<std::uint8_t>& archive,
        const SessionResourceIdentity& expected_resources,
        const SessionHostState& expected_selection,
        const std::vector<SessionHouseholdMember>& expected_roster,
        const WorldSnapshot& original_lot,
        const LotContext& original_globals,
        const Ts1BootstrapGlobals& bootstrap,
        std::optional<std::int16_t> family_global,
        SessionBundleLimits limits = {});

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
    const SessionHostState& host() const { return bundle_.host; }
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
};

} // namespace odh
