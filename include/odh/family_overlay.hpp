#pragma once

#include "odh/family.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace odh {

enum class FamilyOverlayAction : std::uint32_t { move_in = 0, move_out = 1 };

struct FamilyOverlayAssignment {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    std::uint16_t house_number = 0;
    std::int32_t purchase_price = 0;
    std::int32_t budget_after_purchase = 0;
    FamilyOverlayAction action = FamilyOverlayAction::move_in;
};

struct FamilyOverlay {
    std::array<std::uint8_t, 32> neighborhood_identity{};
    std::vector<FamilyOverlayAssignment> assignments;
};

// Identity covers the FAMI chunk ID, family number, original house, ordered
// member GUIDs, and original budget. It does not identify the whole IFF file.
std::array<std::uint8_t, 32> family_overlay_identity(const FamilyEntry&);

// Makes the stored result of a purchase for a family with no current house.
// The caller must obtain purchase_price from the selected lot's
// SIMI.PurchaseValue; this helper checks affordability but cannot identify the
// lot file on its own.
FamilyOverlayAssignment plan_family_move_in(
    const FamilyEntry&, std::uint16_t house_number, std::int32_t purchase_price);

// Appends one move-in to the current overlay after checking its latest
// effective family/house state against the immutable source snapshot.
FamilyOverlay plan_family_move_in_overlay(
    const std::vector<FamilyEntry>& source,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay,
    std::uint16_t family_chunk_id, std::uint16_t house_number,
    std::int32_t purchase_price);

// Appends a move-out for a family that was already housed in the source data.
// Simitone refunds the source FAMI.ValueInArch and clears it; move-outs after
// a player-created move-in remain unsupported until that value's update rule
// is verified.
FamilyOverlay plan_family_move_out_overlay(
    const std::vector<FamilyEntry>& source,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id);

// Bounded v2 operation-log codec; v1 purchase-only saves remain readable. The
// caller supplies SHA-256 of the exact source Neighborhood.iff so stale player
// data can fail closed.
std::vector<std::uint8_t> encode_family_overlay(const FamilyOverlay&);
FamilyOverlay decode_family_overlay(const std::vector<std::uint8_t>&);

// Replays assignments against owned copies. Source identity, family identity,
// destination occupancy and budget are checked before publishing a result.
// Commercial FAMI values are never modified.
std::vector<FamilyEntry> apply_family_overlay(
    const std::vector<FamilyEntry>& source,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay& overlay);

} // namespace odh
