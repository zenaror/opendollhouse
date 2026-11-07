#pragma once

#include "odh/family.hpp"
#include "odh/neighborhood.hpp"
#include "odh/type_attributes.hpp"
#include "odh/native_personal_state.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <optional>

namespace odh {

inline constexpr std::size_t family_overlay_max_bytes = 4 * 1024 * 1024;

// Owned baseline data, independent of filesystem or platform entry points.
struct NeighborhoodFamilySource {
    std::array<std::uint8_t, 32> identity{};
    std::vector<FamilyEntry> families;
};

enum class FamilyOverlayAction : std::uint32_t { move_in = 0, move_out = 1, checkpoint = 2 };

// Explicit before-state for chronological native player-state operations.
struct FamilyOverlayOperationState {
    std::uint16_t expected_house = 0;
    std::uint64_t expected_occupancy_epoch = 0;
    std::int32_t budget_before = 0;
    std::int32_t value_in_arch_before = 0;
    std::int32_t value_in_arch_after = 0;
    std::optional<std::array<std::uint8_t, 32>> session_sha256;
};

struct FamilyOverlayAssignment {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    std::uint16_t house_number = 0;
    std::int32_t purchase_price = 0;
    std::int32_t budget_after_purchase = 0;
    FamilyOverlayAction action = FamilyOverlayAction::move_in;
    std::optional<FamilyOverlayOperationState> state;
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

// Exact final state of a household with an operation history. A terminal
// removal retains the immutable identity and complete history, but cannot be
// replayed over a different checkpoint, occupancy period or finances.
struct FamilyOverlayRemovalState {
    std::uint64_t occupancy_epoch = 0;
    std::int32_t budget = 0;
    std::int32_t value_in_arch = 0;
    std::optional<std::array<std::uint8_t, 32>> checkpoint_sha256;
};

// Logical removal of an unhoused player-created family. Its creation record
// remains in the overlay to reserve IDs and GUIDs; private character files are
// retained so this operation never deletes unrelated or commercial files.
struct FamilyOverlayArchive {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    std::optional<FamilyOverlayRemovalState> final_state; // v9 after any history.
};

struct FamilyOverlayDeletedMember {
    std::uint32_t guid = 0;
    std::int16_t neighbor_id = 0;
};

// Non-destructive deletion of an unhoused source family. FAMI/NBRS entries and
// links are hidden in the ODH sidecar; commercial neighborhood/character files
// remain untouched.
struct FamilyOverlayDeletion {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    std::vector<FamilyOverlayDeletedMember> members;
    std::optional<FamilyOverlayRemovalState> final_state; // v9 after any history.
};

struct FamilyCreationMember {
    std::uint32_t guid;
    // The CAS supplies the 88 base words produced from appearance/personality.
    // The helper adds Simitone's family-ID word and zero extension to 256.
    std::vector<std::int16_t> person_data;
};

// Display-only metadata. Identity remains tied to the immutable source/creation
// snapshot; changing a name must never invalidate occupancy/checkpoint leases.
struct FamilyOverlayName {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    std::string family_name; // 1..256 legacy-encoded bytes, like creation names.
    std::uint8_t language = 0; // Required TS1 encoding code 1..20, independent of UI.
};

// Neighborhood-wide shared state, committed with the family checkpoint in
// one private overlay CAS. Revision changes only when canonical bank bytes do.
struct NeighborhoodTypeAttributes {
    std::uint64_t revision=1;
    TypeAttributesSnapshot state;
};

// One immutable vacant-lot document committed by the exact managed move-out.
// Mode is blob provenance only: residential modes1/4 share the HOUSE key.
// Policy1 preserves original linked HEAD; policy2 uses pinned VM ID order.
struct FamilyPropertyRelease {
    std::uint32_t assignment_index=0;
    std::uint16_t house=0,family_chunk_id=0,policy=1;
    std::uint8_t neighborhood=1,mode=1;
    std::int32_t vacant_purchase_value=0; // Measured AFTER source filtering, never the refund.
    std::int32_t cash_after_cleanup=0; // Actual runtime cash after Delete callbacks (00427678).
    std::uint64_t occupancy_epoch=0,expected_revision=0;
    std::array<std::uint8_t,32> family_identity{},source_checkpoint_sha256{},installed_lot_sha256{},head{};
    std::optional<std::array<std::uint8_t,32>> expected_head;
};
struct FamilyPropertyHead {
    std::uint16_t house=0,policy=1;
    std::uint8_t neighborhood=1,mode=1;
    std::uint64_t revision=0;
    std::array<std::uint8_t,32> installed_lot_sha256{},head{};
    std::int32_t vacant_purchase_value=0;
};
struct FamilyOverlay {
    std::array<std::uint8_t, 32> neighborhood_identity{};
    std::vector<FamilyOverlayAssignment> assignments;
    std::vector<FamilyOverlayCreation> creations{}; // Replayed before assignments.
    std::vector<FamilyOverlayArchive> archives{}; // Applied after assignments.
    std::vector<FamilyOverlayDeletion> deletions{}; // Source families, applied last.
    std::vector<FamilyOverlayName> names{}; // One current override per stable family ID.
    std::optional<NeighborhoodTypeAttributes> type_attributes;
    std::vector<FamilyPropertyRelease> property_releases{}; // Chronological v10 records.
    std::optional<NativePersonalSnapshot> personal_state; // v11, shared neighborhood authority.

};

enum class NeighborhoodTypeAttributesSource { immutable_seed,session_snapshot,private_overlay };
struct NeighborhoodTypeAttributesSelection {
    TypeAttributesSnapshot state;
    std::uint64_t private_revision=0;
    NeighborhoodTypeAttributesSource source=NeighborhoodTypeAttributesSource::immutable_seed;
    bool superseded_session_state=false;
};
// An older house archive must never roll back the shared neighborhood bank.
// Legacy snapshot bootstrap is allowed only while no private authority exists.
NeighborhoodTypeAttributesSelection select_neighborhood_type_attributes(
    const std::array<std::uint8_t,32>& neighborhood_identity,
    const TypeAttributesSnapshot& immutable_seed,
    const std::optional<TypeAttributesSnapshot>& saved,const FamilyOverlay* current);
// Pure plan; host must CAS the complete expected overlay, never merge a stale
// simulation's bank into a concurrently advanced neighborhood head.
FamilyOverlay plan_neighborhood_type_attributes_overlay(
    const std::array<std::uint8_t,32>& neighborhood_identity,
    const FamilyOverlay* current,const TypeAttributesSnapshot& replacement);

// Whole-overlay CAS remains mandatory. Existing record revisions cannot go
// backward, disappear or change identity; new writer revisions may advance.
FamilyOverlay plan_neighborhood_personal_overlay(const std::array<std::uint8_t,32>&,
    const FamilyOverlay*,const NativePersonalSnapshot&);

// Epoch counts occupancy periods, including legacy move-ins. A retained ticket
// is a full-resume candidate only when its house and epoch match current occupancy;
// otherwise it is available solely for validated portable household extraction.
struct FamilyOverlayFamilyState {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
    std::uint16_t house_number = 0;
    std::uint64_t occupancy_epoch = 0;
    std::int32_t budget = 0;
    std::int32_t value_in_arch = 0;
    bool managed = false;
    std::optional<std::array<std::uint8_t, 32>> checkpoint_sha256;
    std::uint16_t checkpoint_house = 0;
    std::uint64_t checkpoint_epoch = 0;
    std::optional<std::int32_t> checkpoint_budget;
};
struct FamilyOverlayReplay {
    std::vector<FamilyEntry> families;
    std::vector<FamilyOverlayFamilyState> states;
    std::vector<FamilyPropertyHead> properties; // Keyed by house within this neighborhood identity.
};
FamilyOverlayReplay replay_family_overlay(const std::vector<FamilyEntry>&,
    const std::array<std::uint8_t, 32>& neighborhood_identity, const FamilyOverlay&);

// Explicit first managed checkpoint for a family occupying its ORIGINAL
// commercial house. No relocation, purchase/refund or new occupancy period.
// The caller supplies the actual validated live budget and published digest.
// Existing selected-family operation history/managed state cannot be adopted.
// Uses the existing v6 checkpoint wire profile; other histories retain bytes.
FamilyOverlay plan_family_original_adoption_overlay(const std::vector<FamilyEntry>&,
    const std::array<std::uint8_t,32>&,const FamilyOverlay*,
    std::uint16_t family_chunk_id,std::uint16_t original_house,
    std::int32_t live_budget,const std::array<std::uint8_t,32>& session_sha256,
    std::optional<std::int32_t> property_value = std::nullopt);

FamilyOverlay plan_family_managed_move_in_overlay(const std::vector<FamilyEntry>&,
    const std::array<std::uint8_t, 32>&, const FamilyOverlay*,
    std::uint16_t family_chunk_id, std::uint16_t house_number, std::int32_t purchase_price);
// Couples this source checkpoint, already quoted cache and immutable property
// document to the SAME managed move-out operation; never writes files itself.
FamilyOverlay plan_family_property_release_overlay(const std::vector<FamilyEntry>&,
    const std::array<std::uint8_t,32>& neighborhood_identity,const FamilyOverlay& current,
    std::uint16_t family_chunk_id,const FamilyPropertyRelease&);
FamilyOverlay plan_family_managed_move_out_overlay(const std::vector<FamilyEntry>&,
    const std::array<std::uint8_t, 32>&, const FamilyOverlay*, std::uint16_t family_chunk_id);
// A checkpoint retains ValueInArch unless its owner supplies a current property
// quote. Its digest names an immutable session; validate/publish before CAS.
FamilyOverlay plan_family_checkpoint_overlay(const std::vector<FamilyEntry>&,
    const std::array<std::uint8_t, 32>&, const FamilyOverlay*,
    std::uint16_t family_chunk_id, std::uint16_t expected_house,
    std::uint64_t expected_epoch, std::int32_t budget_after,
    const std::array<std::uint8_t, 32>& session_sha256,
    std::optional<std::int32_t> property_value = std::nullopt);

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

// Bounded tagged codec: terminal removal after history uses v9; shared type attributes use v8; name overrides use v7; managed chronological operations use v6; legacy
// assignment-only overlays retain v2 and metadata uses v3-v5. v1 remains readable. The
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

// Logically deletes an unhoused source family, retaining any operation history and hiding its
// FAMI/NBRS members and relationship links while preserving the source files.
FamilyOverlay plan_family_delete_overlay(
    const std::vector<FamilyEntry>& source, const Nbrs& neighbors,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id);

// Renames an existing visible household without changing its members, money,
// occupancy history or private characters. Replaces only its current name.
FamilyOverlay plan_family_name_overlay(
    const std::vector<FamilyEntry>& source,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, std::uint16_t family_chunk_id,
    const std::string& family_name, std::uint8_t language);

// Builds the observed Simitone CreateFamily/AddNeighbor sidecar from ordered
// CAS members. IDs are allocated against the current neighborhood snapshots;
// this does not create avatar IFF files or modify Neighborhood.iff.
FamilyOverlayCreation make_simitone_family_creation(
    const std::vector<FamilyEntry>& source_families, const Nbrs& source_neighbors,
    const std::array<std::uint8_t, 32>& neighborhood_identity,
    const FamilyOverlay* current_overlay, const std::string& family_name,
    const std::vector<FamilyCreationMember>& members, std::int32_t initial_budget = 20000);

// Stable retired identities, including members of archived created families.
// Validates retained creation/deletion metadata and rejects ambiguous retired
// GUIDs or neighbor IDs. Hosts use this for restore admission and saved/live
// relationship cleanup; active families are still authenticated by replay.
std::vector<FamilyOverlayDeletedMember> retired_family_members(const FamilyOverlay&);

// Owned copy preserves the original header, serialized order, placeholders,
// fields and unknown tail extent, then appends the created records. Unknown
// source tail bytes remain unpreserved and consequently unencodable.
Nbrs apply_neighbor_overlay(const Nbrs&,
    const std::array<std::uint8_t, 32>& neighborhood_identity, const FamilyOverlay&);

} // namespace odh
