#pragma once
#include "odh/session_bundle.hpp"
#include "odh/neighbor_context.hpp"
#include <optional>
namespace odh {
class OriginalFrontendPersonalSource;
// Pure sealed-family1 proof; accepts existing equal selective writer/seed values.
void validate_original_family_departure_personal(const SessionBundle&,const OriginalFrontendPersonalSource&);
class PreparedTs1TravelReturn;
class SessionSourceCheckpoint;
// Constructed only by hashing and parsing the same owned, bounded IFF bytes.
// The host must still authenticate the checkpoint and visited resource manifest.
class Ts1TravelPersonInheritanceSource {
public:
    const std::array<std::uint8_t,32>& neighborhood_identity() const {return identity_;}
private:
    Ts1TravelPersonInheritanceSource(Nbrs neighbors,std::array<std::uint8_t,32> identity)
        :neighbors_(std::move(neighbors)),identity_(identity) {}
    std::optional<std::int16_t> inherited_value(const Ts1TravelMember&,std::size_t,Ts1TravelProtocol = Ts1TravelProtocol::legacy_pin) const;
    NeighborContext neighbors_;
    std::array<std::uint8_t,32> identity_{};
    friend Ts1TravelPersonInheritanceSource read_ts1_travel_person_inheritance(const View&);
    friend class PreparedTs1TravelReturn;
    friend PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionBundle&,SessionBundleLimits,const Ts1TravelPersonInheritanceSource*);
    friend PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionSourceCheckpoint&,SessionBundleLimits,const Ts1TravelPersonInheritanceSource*);
};
Ts1TravelPersonInheritanceSource read_ts1_travel_person_inheritance(const View&);
// The reference restores SavedLot and does not implement SaveSimPersistentData.
// Retain every observed personal-bank difference, including missing vs zero,
// rather than inventing a persistence field list or silently losing progress.
struct Ts1TravelPersonalDelta {
    Ts1TravelMember member;
    std::size_t index=0;
    std::optional<std::int16_t> home_value,visited_value;
};
class PreparedTs1TravelReturn {
public:
    const std::array<std::uint8_t,32>& source_checkpoint_sha256() const {return source_checkpoint_sha256_;}
    const std::array<std::uint8_t,32>& suspended_home_sha256() const {return suspended_home_sha256_;}
    const std::vector<Ts1TravelPersonalDelta>& unresolved_personal_deltas() const {return personal_deltas_;}
    // Proven lot/thread-local differences retained verbatim from HOME. These
    // remain visible for diagnostics but do not block personal persistence.
    const std::vector<Ts1TravelPersonalDelta>& retained_runtime_deltas() const {return runtime_deltas_;}
    // The destination retained its proven original inherited value. Restoring
    // HOME preserves prior progress; this does not merge a new visited value.
    const std::vector<Ts1TravelPersonalDelta>& unchanged_inherited_personal_deltas() const {return inherited_deltas_;}
    bool requires_original_personal_persistence_resolution() const {return !personal_deltas_.empty();}
    // Candidate for diagnostics/review. It has no publication authority.
    const SessionBundle& preview_home() const {return home_;}
    // The caller must additionally authenticate source_checkpoint_sha256 against
    // its current journal/private-file head and restore the candidate privately.
    // A personal delta blocks publication until original rules are established.
    const SessionBundle& home() const {
        if(requires_original_personal_persistence_resolution())throw FormatError("travel return has unresolved original personal persistence rules");
        return home_;
    }
private:
    SessionBundle home_;
    std::array<std::uint8_t,32> source_checkpoint_sha256_{},suspended_home_sha256_{};
    std::vector<Ts1TravelPersonalDelta> personal_deltas_,runtime_deltas_,inherited_deltas_;
    friend PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionBundle&,SessionBundleLimits,const Ts1TravelPersonInheritanceSource*);
    friend PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionSourceCheckpoint&,SessionBundleLimits,const Ts1TravelPersonInheritanceSource*);
};
// Pure unpublished projection from an already-durable return-pending envelope.
// Never imports visited World IDs, object relationships, VM/queue/controller
// state, clock, motive-decay state or property/occupancy data into the home.
// Already-accepted source identity comes from its exact authenticated bytes,
// including older supported payload versions. The host still verifies the head
// is current before consuming this pure unpublished projection.
PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionSourceCheckpoint&,SessionBundleLimits = {},
    const Ts1TravelPersonInheritanceSource* = nullptr);
// Synthetic/new canonical model convenience. Its receipt identifies the current
// writer's explicit encoding; never use it to authenticate an older raw head.
PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionBundle&,SessionBundleLimits = {},
    const Ts1TravelPersonInheritanceSource* = nullptr);
}
