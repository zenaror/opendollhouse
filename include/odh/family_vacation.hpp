// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/family.hpp"
#include <array>
#include <optional>
#include <vector>
namespace odh {
using FamilyVacationDigest=std::array<std::uint8_t,32>;
struct FamilyOverlay;
// Keys are actual FAMI chunkID/Global9, NOT serialized FamilyNumber.
struct FamilyVacationRow {
    std::uint16_t family_chunk_id=0;
    FamilyVacationDigest family_identity{};
    std::int32_t serialized_family_number=0;
    std::uint32_t fami_version=0;
    bool active=true;
    std::optional<std::uint32_t> seed_word,current_word;
};
struct FamilyVacationSnapshot {
    std::uint32_t version=1;
    FamilyVacationDigest neighborhood_identity{};
    std::uint64_t revision=1;
    std::vector<FamilyVacationRow> families;
};
void validate_family_vacation_snapshot(const FamilyVacationSnapshot&);
std::vector<std::uint8_t> encode_family_vacation_snapshot(const FamilyVacationSnapshot&);
FamilyVacationSnapshot decode_family_vacation_snapshot(const std::vector<std::uint8_t>&);
FamilyVacationDigest family_vacation_digest(const FamilyVacationSnapshot&);
FamilyVacationRow family_vacation_seed(const FamilyEntry&,const FamilyVacationDigest& family_identity,bool active=true);
void validate_family_vacation_successor(const FamilyVacationSnapshot&,const FamilyVacationSnapshot&);
std::optional<std::int16_t> family_vacation_available_count(const FamilyVacationSnapshot&);
std::vector<FamilyEntry> project_family_vacation_families(const std::vector<FamilyEntry>&,const FamilyVacationSnapshot&);

enum class FamilyVacationPhaseKind : std::uint32_t {set_before_avatars=1,clear_residential_load=2};
// Stored ONLY in the overlay; never embedded in FVAC/self-hashing capsules.
// HOST authenticates actual sealed source/CallerPD61 or fresh residential phase.
struct FamilyVacationPhase {
    FamilyVacationPhaseKind kind=FamilyVacationPhaseKind::set_before_avatars;
    std::uint16_t family_chunk_id=0,destination=0;
    FamilyVacationDigest family_identity{},source_head{},before_bank{};
    std::uint32_t before_word=0,after_word=0;
};
struct FamilyVacationPrivateHead {
    std::uint16_t family_chunk_id=0,home_house=0;
    std::uint8_t home_mode=1;
    std::int32_t serialized_family_number=0;
    FamilyVacationDigest family_identity{},checkpoint_sha256{};
};
struct FamilyVacationOverlayState {
    FamilyVacationSnapshot state;
    std::vector<FamilyVacationPrivateHead> private_heads;
    std::vector<FamilyVacationPhase> phases;
};
void validate_family_vacation_overlay_state(const FamilyVacationOverlayState&);
std::vector<std::uint8_t> encode_family_vacation_overlay_state(const FamilyVacationOverlayState&);
FamilyVacationOverlayState decode_family_vacation_overlay_state(const std::vector<std::uint8_t>&);
const FamilyVacationPrivateHead* family_vacation_private_head(const FamilyVacationOverlayState&,std::uint16_t);
const FamilyVacationPhase* family_vacation_phase(const FamilyVacationOverlayState&,std::uint16_t);
void set_family_vacation_private_head(FamilyVacationOverlayState&,const FamilyVacationPrivateHead&);
void set_family_vacation_phase(FamilyVacationOverlayState&,const FamilyVacationPhase&);
void clear_family_vacation_phase(FamilyVacationOverlayState&,std::uint16_t);
bool family_vacation_phase_matches(const FamilyVacationOverlayState&,const FamilyVacationPhase&) noexcept;

class FamilyVacationState;
class PreparedFamilyVacationChange {
public:
    const FamilyVacationSnapshot& preview_snapshot() const;
    const FamilyVacationPhase& phase() const;
    bool changed() const noexcept {return changed_;}
private:
    const FamilyVacationState* owner_=nullptr;
    FamilyVacationDigest before_{};
    FamilyVacationSnapshot preview_;
    FamilyVacationPhase phase_;
    bool changed_=false,consumed_=false;
    friend class FamilyVacationState;
};
class FamilyVacationState {
public:
    FamilyVacationState(std::vector<FamilyVacationRow> seeds,FamilyVacationDigest neighborhood);
    const FamilyVacationSnapshot& snapshot() const noexcept {return state_;}
    void restore(const FamilyVacationSnapshot&);
    std::optional<std::int16_t> available_count() const;
    PreparedFamilyVacationChange prepare(const FamilyVacationPhase&) const;
    bool source_current(const PreparedFamilyVacationChange&) const;
    bool commit(PreparedFamilyVacationChange&);
private:
    FamilyVacationSnapshot seed_,state_;
};
// Derive complete admitted seeds/lifetimes from immutable source + existing
// creation/archive/deletion replay, retaining reserved historical identities.
FamilyVacationState make_family_vacation_state(const std::vector<FamilyEntry>& source,
    const FamilyVacationDigest& neighborhood,const FamilyOverlay* current);
FamilyOverlay plan_family_vacation_overlay(const std::vector<FamilyEntry>& source,
    const FamilyVacationDigest& neighborhood,const FamilyOverlay* current,const FamilyVacationOverlayState& replacement);
// Explicit family creation/removal consumer, preserving old tombstoned rows.
FamilyOverlay reconcile_family_vacation_overlay(const std::vector<FamilyEntry>& source,
    const FamilyVacationDigest& neighborhood,const FamilyOverlay& replacement);

enum class FamilyVacationHeadKind {managed,private_travel};
struct FamilyVacationRestoreHead {
    FamilyVacationHeadKind kind=FamilyVacationHeadKind::managed;
    std::uint16_t family_chunk_id=0;
    FamilyVacationDigest family_identity{},archive_sha256{};
};
// No generic stale override. Different/absent embedded FVAC needs exact current
// managed/private head and either unchanged own row or its typed overlay phase.
std::optional<FamilyVacationSnapshot> select_family_vacation_snapshot(
    const std::vector<FamilyEntry>& source,const FamilyVacationDigest& neighborhood,
    const std::optional<FamilyVacationSnapshot>& saved,const FamilyOverlay* current,
    const std::optional<FamilyVacationRestoreHead>& exact_head=std::nullopt);
} // namespace odh
