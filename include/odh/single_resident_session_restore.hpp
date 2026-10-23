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
// This profile does not serialize routes, dynamic code owners or seated actions.
class SingleResidentSessionRestore {
public:
    static std::unique_ptr<SingleResidentSessionRestore> prepare(
        const std::vector<std::uint8_t>& archive,
        const SessionResourceIdentity& expected_resources,
        const SessionHostState& expected_selection,
        const WorldSnapshot& original_lot,
        const LotContext& original_globals,
        const Ts1BootstrapGlobals& bootstrap,
        std::optional<std::int16_t> family_global,
        SessionBundleLimits limits = {});

    World& world() { return world_; }
    RelationshipState& relationships() { return relationships_; }
    LotContext& lot() { return lot_; }
    Ts1BootstrapGlobals& bootstrap() { return bootstrap_; }
    std::optional<std::int16_t>& family_global() { return family_global_; }
    const SessionHostState& host() const { return bundle_.host; }

    // Bind fresh VM/controllers/Playback and register equivalent scheduler hooks
    // against this candidate first. Never start Main. On any exception discard
    // this candidate and every execution object: restoration is not retryable.
    // Tick/RNG are published only after all execution components accept state.
    void restore_execution(Vm&, EntityThreadControllers&, Playback&,
        EntityVmScheduler&, const PlaybackRestoreResolver&, RandomState&,
        std::uint64_t& tick);
private:
    SingleResidentSessionRestore(SessionBundle, const LotContext&,
        const Ts1BootstrapGlobals&, std::optional<std::int16_t>, WorldLimits,
        RelationshipLimits);
    SessionBundle bundle_;
    World world_;
    RelationshipState relationships_;
    LotContext lot_;
    Ts1BootstrapGlobals bootstrap_;
    std::optional<std::int16_t> family_global_;
    bool attempted_ = false;
};

} // namespace odh
