#pragma once

#include "odh/saved_world.hpp"
#include "odh/family_overlay.hpp"
#include "odh/session_bundle.hpp"
#include <functional>

namespace odh {
class InstalledOccupancyRetirement {
public:
    const std::array<std::uint8_t,32>& neighborhood_identity() const { return neighborhood_identity_; }
    const std::array<std::uint8_t,32>& house_identity() const { return house_identity_; }
    const std::array<std::uint8_t,32>& overlay_identity() const { return overlay_identity_; }
    const std::array<std::uint8_t,32>& plan_identity() const { return plan_identity_; }
    const SessionFamilyLease& destination_lease() const { return destination_lease_; }
    std::uint16_t house_number() const { return house_number_; }
    std::uint16_t source_family_chunk_id() const { return source_family_chunk_id_; }
    const std::vector<std::uint32_t>& expired_guids() const { return expired_guids_; }
private:
    InstalledOccupancyRetirement() = default;
    std::array<std::uint8_t,32> neighborhood_identity_{}, house_identity_{}, overlay_identity_{}, plan_identity_{};
    SessionFamilyLease destination_lease_;
    std::uint16_t house_number_ = 0, source_family_chunk_id_ = 0;
    std::vector<std::uint32_t> expired_guids_;
    friend std::optional<InstalledOccupancyRetirement> prepare_installed_occupancy_retirement(
        const NeighborhoodFamilySource&, const FamilyOverlay&, const SessionFamilyLease&,
        std::uint16_t, const std::array<std::uint8_t,32>&);
};

// Only the source FAMI occupancy (epoch one) authorizes removal from the
// immutable installed lot. Later family rosters never broaden this authority.
std::optional<InstalledOccupancyRetirement> prepare_installed_occupancy_retirement(
    const NeighborhoodFamilySource&, const FamilyOverlay& runtime_overlay,
    const SessionFamilyLease& destination_lease, std::uint16_t house_number,
    const std::array<std::uint8_t,32>& house_identity);

struct RetiredOccupant {
    std::uint32_t guid = 0;
    std::int16_t id = 0;
    std::uint64_t generation = 0;
};
// Called on a private candidate AFTER expired avatars have been detached and
// removed. The caller supplies a reference-backed placement/visibility rule
// for every surviving held child. Returning false discards the candidate.
using OccupancyDropHandler = std::function<bool(World&, const Entity&,
    const std::vector<std::int16_t>&)>;
struct OccupancyProjection {
    // Records form a derived activation view: cursor/offset provenance still
    // refers to the source bytes, not to a re-encoded OBJM stream.
    std::unique_ptr<SavedWorld> candidate;
    std::vector<RetiredOccupant> retired;
    std::vector<std::int16_t> retired_ids;
    // Exact source records are diagnostic provenance, NEVER activation input.
    std::vector<SavedWorldEntity> source_records;
    // Native safety policy, not a claim that TS1 resets every object stack.
    // Host must not reactivate any saved stack/queue/opaque execution suffix.
    bool invalidated_saved_execution = false;
};
OccupancyProjection project_installed_occupancy_retirement(
    const SavedWorld& validated_source, const InstalledOccupancyRetirement&,
    const std::array<std::uint8_t,32>& observed_house_identity,
    const OccupancyDropHandler& drop_held = {}, const SavedWorldLimits& = {});
} // namespace odh
