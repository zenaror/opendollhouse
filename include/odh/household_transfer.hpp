#pragma once
#include "odh/session_bundle.hpp"
#include <array>
#include <string>
#include <vector>

namespace odh {
struct HouseholdTransferTicket {
    SessionFamilyLease source_lease;
    std::uint16_t source_house = 0;
    std::int32_t family_number = 0;
    std::int32_t checkpoint_budget = 0;
    std::array<std::uint8_t, 32> archive_sha256{};
};
struct HouseholdTransferIdentity {
    std::uint32_t guid = 0;
    std::int16_t neighbor_id = 0;
    std::uint16_t object_definition_id = 0;
    std::string character_asset;
    std::array<std::uint8_t, 32> character_fingerprint{};
};
struct HouseholdTransferMember {
    HouseholdTransferIdentity identity;
    std::array<std::int16_t, 88> person_data{};
    std::array<std::int16_t, 16> motives{};
    std::vector<std::int16_t> attributes;
    std::vector<std::string> bound_appearances;
    Ts1MotiveSnapshot decay;
};
class PreparedHouseholdTransfer;
class AppliedHouseholdTransfer;
PreparedHouseholdTransfer prepare_household_transfer(const std::vector<std::uint8_t>& encoded,
    const HouseholdTransferTicket&, const std::vector<HouseholdTransferIdentity>& expected_roster);
AppliedHouseholdTransfer apply_household_transfer(const PreparedHouseholdTransfer&,
    const WorldSnapshot& destination_world, const RelationshipSnapshot& destination_relationships,
    const SessionFamilyLease& destination_lease,
    const std::vector<HouseholdTransferIdentity>& destination_roster, const Ts1ClockSnapshot& destination_clock);

// An authenticated, bounded projection, never a World/VM restore. Construction
// validates the full archive and the complete expected family resource roster.
class PreparedHouseholdTransfer {
public:
    const HouseholdTransferTicket& ticket() const { return ticket_; }
    const std::vector<HouseholdTransferMember>& members() const { return members_; }
    const RelationshipSnapshot& relationships() const { return relationships_; }
private:
    PreparedHouseholdTransfer() = default;
    HouseholdTransferTicket ticket_;
    std::vector<HouseholdTransferMember> members_;
    RelationshipSnapshot relationships_;
    friend PreparedHouseholdTransfer prepare_household_transfer(const std::vector<std::uint8_t>&,
        const HouseholdTransferTicket&, const std::vector<HouseholdTransferIdentity>&);
};

// Receipt and staged result are inseparable; all fields are privately owned.
// Inputs stay untouched on failure. Host publishes only after occupancy CAS
// validation; World snapshots never authorize persistent journal publication.
class AppliedHouseholdTransfer {
public:
    const HouseholdTransferTicket& ticket() const { return ticket_; }
    const SessionFamilyLease& destination_lease() const { return destination_lease_; }
    const std::vector<SessionHouseholdMember>& members() const { return members_; }
    const WorldSnapshot& world() const { return world_; }
    const RelationshipSnapshot& relationships() const { return relationships_; }
    const std::vector<SessionAvatarMotiveState>& decays() const { return decays_; }
private:
    AppliedHouseholdTransfer() = default;
    HouseholdTransferTicket ticket_;
    SessionFamilyLease destination_lease_;
    std::vector<SessionHouseholdMember> members_;
    WorldSnapshot world_;
    RelationshipSnapshot relationships_;
    std::vector<SessionAvatarMotiveState> decays_;
    friend AppliedHouseholdTransfer apply_household_transfer(const PreparedHouseholdTransfer&,
        const WorldSnapshot&, const RelationshipSnapshot&, const SessionFamilyLease&,
        const std::vector<HouseholdTransferIdentity>&, const Ts1ClockSnapshot&);
};
} // namespace odh
