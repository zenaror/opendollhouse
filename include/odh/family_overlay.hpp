#pragma once

#include "odh/family.hpp"
#include "odh/neighborhood.hpp"
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

// Persisted headless metadata only: this does not produce a private avatar IFF
// or an executable player. Raw names contain 1..256 bytes, households 1..8.
struct FamilyOverlayCreation {
    std::uint16_t family_chunk_id = 0; // Any free chunk ID, including zero.
    std::string family_name;
    std::vector<std::uint8_t> fami_payload; // Exact Simitone CreateFamily profile.
    // Exact AddNeighbor profile: v10/Unknown3=9/PersonMode=9, iffname,
    // MysteryZero=0/UnknownNegOne=-1, no relationships; 256 person words with
    // family chunk ID as signed16 in word 61 and zeros in words 88..255.
    std::vector<NeighborRecord> members; // Ordered exactly as the FAMI roster.
};

// Logical removal of an unhoused player-created family. Its creation record
// remains in the overlay to reserve IDs and GUIDs; private character files are
// retained so this operation never deletes unrelated or commercial files.
struct FamilyOverlayArchive {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
};

struct FamilyCreationMember {
    std::uint32_t guid;
    // The CAS supplies the 88 base words produced from appearance/personality.
    // The helper adds Simitone's family-ID word and zero extension to 256.
    std::vector<std::int16_t> person_data;
};

struct FamilyOverlay {
    std::array<std::uint8_t, 32> neighborhood_identity{};
    std::vector<FamilyOverlayAssignment> assignments;
    std::vector<FamilyOverlayCreation> creations{}; // Replayed before assignments.
    std::vector<FamilyOverlayArchive> archives{}; // Applied after assignments.
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

// Bounded v3 tagged/length-prefixed codec for created-family metadata.
// Assignment-only overlays retain v2 encoding; v1 saves remain readable. The
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

// Validates both immutable snapshots before appending metadata. Caller supplies
// a free uint16 FAMI ID and positive, free int16 neighbor IDs. No avatar resource
// or live runtime/session integration is supplied by this API.
// FamilyNumber must be Max(source and preceding creations)+1; empty source and
// signed32 overflow fail closed. FamilyNumber -1 remains the townie sentinel.
FamilyOverlay plan_family_creation_overlay(
    const std::vector<FamilyEntry>&, const Nbrs&,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, const FamilyOverlayCreation&);

// Hides an unhoused player-created family and its neighborhood records while
// retaining the creation payload and private characters for recovery.
FamilyOverlay plan_family_archive_overlay(
    const std::vector<FamilyEntry>&, const Nbrs&,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id);

// Builds the observed Simitone CreateFamily/AddNeighbor sidecar from ordered
// CAS members. IDs are allocated against the current neighborhood snapshots;
// this does not create avatar IFF files or modify Neighborhood.iff.
FamilyOverlayCreation make_simitone_family_creation(
    const std::vector<FamilyEntry>& source_families, const Nbrs& source_neighbors,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, const std::string& family_name,
    const std::vector<FamilyCreationMember>& members, std::int32_t initial_budget = 20000);

// Owned copy preserves the original header, serialized order, placeholders,
// fields and unknown tail extent, then appends the created records. Unknown
// source tail bytes remain unpreserved and consequently unencodable.
Nbrs apply_neighbor_overlay(const Nbrs&,
    const std::array<std::uint8_t, 32>& neighborhood_identity, const FamilyOverlay&);

} // namespace odh
