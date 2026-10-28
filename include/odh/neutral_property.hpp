#pragma once
#include "odh/property_checkpoint.hpp"

namespace odh {
// Explicitly selected by the host after proving architecture is unchanged.
// There is no edited-architecture fallback to the installed house in v1.
enum class NeutralArchitectureProfile : std::uint8_t { installed_unchanged = 1 };
struct NeutralPropertyPins {
    NeutralArchitectureProfile architecture{}; // Zero is invalid.
    std::uint64_t expected_property_revision = 0;
    std::optional<std::array<std::uint8_t,32>> expected_property_head;
};
struct NeutralPropertyState {
    // Structural profile1 only: NEVER sufficient for fresh execution/activation
    // or a release/refund receipt. A future executable profile must be required.
    SessionResourceIdentity resources;
    SessionFamilyLease source_lease;
    std::uint16_t house_number = 0;
    std::int32_t source_budget = 0;
    std::array<std::uint8_t,32> checkpoint_sha256{}, installed_house_sha256{}, expected_overlay_sha256{};
    NeutralPropertyPins pins;
    WorldSnapshot world;
    RelationshipSnapshot relationships;
    std::vector<SavedMultitileGroup> groups;
    std::vector<std::int16_t> reserved_ids;
    std::vector<SessionCreatedObject> created_objects, retired_created_objects;
    std::vector<SessionRemovedOriginalObject> removed_original_objects;
    std::vector<PropertyRetiredEntity> retired_entities;
};
struct NeutralPropertyLimits {
    SessionArchiveLimits archive;
    WorldSnapshotCodecLimits world{{4096,131072,4096},64u*1024u*1024u};
    RelationshipSnapshotCodecLimits relationships;
};
// Required NPRP/META/WRLD/RELA/NOWN sections; ordinary session readers reject.
// Only checkpoint-authenticated structural preparation can be encoded. Pins
// bind caller observations but cannot establish property-head CAS authority.
std::vector<std::uint8_t> encode_neutral_property(const PreparedPropertyCheckpoint&,
    const NeutralPropertyPins&, const NeutralPropertyLimits& = {});
// Pure owned archival values. Caller checks immutable byte digest separately.
// Exact resource and installed-house identities are mandatory read inputs.
NeutralPropertyState decode_neutral_property(const std::vector<std::uint8_t>&,
    const SessionResourceIdentity&, const std::array<std::uint8_t,32>& installed_house_sha256,
    const NeutralPropertyLimits& = {});
} // namespace odh
