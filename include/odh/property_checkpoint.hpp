#pragma once
#include "odh/family_overlay.hpp"
#include "odh/property_survivors.hpp"
#include "odh/session_bundle.hpp"

namespace odh {
class PreparedPropertyCheckpoint {
public:
    const World& world() const { return *survivors_.world; }
    const RelationshipState& relationships() const { return *survivors_.relationships; }
    const std::vector<SavedMultitileGroup>& groups() const { return survivors_.groups; }
    const std::vector<std::int16_t>& removed_ids() const { return survivors_.removed_ids; }
    const SessionFamilyLease& lease() const { return lease_; }
    const std::array<std::uint8_t,32>& checkpoint_sha256() const { return checkpoint_sha256_; }
    const SessionResourceIdentity& resources() const { return resources_; }
    std::uint16_t house_number() const { return house_number_; }
    std::int32_t budget() const { return budget_; }
    // Checks the WHOLE overlay, including changes to other families. This is a
    // pure comparison, not an atomic filesystem CAS or publication permission.
    void verify_overlay(const FamilyOverlay& observed) const;
private:
    PreparedPropertyCheckpoint() = default;
    PropertySurvivors survivors_;
    SessionFamilyLease lease_;
    SessionResourceIdentity resources_;
    std::array<std::uint8_t,32> checkpoint_sha256_{};
    std::vector<std::uint8_t> expected_overlay_;
    std::uint16_t house_number_ = 0;
    std::int32_t budget_ = 0;
    friend PreparedPropertyCheckpoint prepare_property_checkpoint(
        const NeighborhoodFamilySource&, const FamilyOverlay&, std::uint16_t,
        const std::vector<std::uint8_t>&, const SessionResourceIdentity&,
        const DefinitionIndex&, const std::vector<SavedMultitileGroup>&, bool,
        const SavedWorldLimits&, const SessionBundleLimits&);
};
// Authenticates exact CURRENT committed checkpoint bytes against a managed
// occupancy in the complete expected overlay. Derives departing actor approvals
// ONLY from the decoded roster after matching the effective family's GUIDs.
// Historical retained checkpoints are rejected. The audited disposition is
// evaluated for EVERY live part, not just group roots; deletion then expands
// whole groups and contained children. Caller still supplies complete group
// membership and baseline-derived reservations; those and DefinitionIndex must
// come from the pinned house/resource manifest.
// This does not prove provenance by trusting arbitrary caller-built objects.
// No artifact, activation, refund or overlay write is authorized by the result.
PreparedPropertyCheckpoint prepare_property_checkpoint(
    const NeighborhoodFamilySource&, const FamilyOverlay&, std::uint16_t family_chunk_id,
    const std::vector<std::uint8_t>& checkpoint_bytes, const SessionResourceIdentity& expected_resources,
    const DefinitionIndex&, const std::vector<SavedMultitileGroup>& complete_partition,
    bool complete_inventory,
    const SavedWorldLimits& = {}, const SessionBundleLimits& = {});
} // namespace odh
