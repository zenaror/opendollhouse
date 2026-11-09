#pragma once

#include "odh/family_overlay.hpp"
#include "linux_user_config.hpp"
#include "odh/session_bundle.hpp"
#include "odh/household_transfer.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace odh_host {

// Pins one managed occupancy and the complete expected neighborhood overlay.
// Archive publication precedes the overlay CAS commit. Conflicts never rebase;
// unreferenced immutable archives are retained for recovery.
class FamilySessionJournal {
public:
    FamilySessionJournal(std::vector<odh::FamilyEntry> source,
        std::array<std::uint8_t, 32> neighborhood_identity,
        std::optional<odh::FamilyOverlay> expected,
        std::uint16_t house_number, std::uint16_t family_chunk_id,
        std::optional<odh::FamilyOverlay> pending_move_in = std::nullopt, LinuxNeighborhoodStorage storage = {});
    // Adopt a currently running original commercial household without moving
    // it, charging a price, or touching its source files. Validates the full
    // native capture/roster and invokes the real host restore validator BEFORE
    // archive I/O. Publishes immutable bytes before the overlay CAS. A stale
    // expected overlay can leave an unreferenced archive, never a new head.
    // The callback must perform actual same-house native restore preparation;
    // it must not publish, write the live simulation, or substitute a boolean.
    static FamilySessionJournal adopt_original_session(std::vector<odh::FamilyEntry> source,
        std::array<std::uint8_t,32> neighborhood_identity,
        std::optional<odh::FamilyOverlay> expected,std::uint16_t house_number,
        std::uint16_t family_chunk_id,odh::SessionBundle capture,
        const std::function<void(const odh::SessionBundle&,const std::vector<std::uint8_t>&)>& validate_reopen, LinuxNeighborhoodStorage storage = {},
        std::optional<std::int32_t> property_value = std::nullopt);
    odh::SessionFamilyLease lease() const;
    bool pending_purchase() const { return pending_move_in_.has_value(); }
    const std::optional<odh::FamilyOverlay>& expected_overlay() const { return expected_; }
    const std::optional<odh::FamilyOverlay>& runtime_overlay() const {
        return pending_move_in_ ? pending_move_in_ : expected_;
    }
    const std::filesystem::path& archive_directory() const { return archive_directory_; }
    // True if the latest CAS reported an error but reread proved exact commit.
    // The logical checkpoint is consistent; directory durability was not confirmed.
    bool durability_uncertain() const { return durability_uncertain_; }
    // Historical tickets remain carryover sources, never full-resume candidates.
    std::optional<std::filesystem::path> checkpoint_path() const;
    // Includes historical checkpoints; metadata preserves the source occupancy
    // and checkpoint budget, never the destination's post-purchase finances.
    std::optional<odh::HouseholdTransferTicket> carry_ticket() const;
    std::optional<std::filesystem::path> retained_checkpoint_path() const;
    void accept_carry(const odh::AppliedHouseholdTransfer&);
    // Host calls only after complete runtime initialization and carry application.
    // Constructor/failed preparation never charge the purchase.
    void commit_move_in();
    void verify_current() const;
    void validate_restore(const std::vector<std::uint8_t>& encoded) const;
    // Any captured TATT bank joins the checkpoint-head update in the same
    // whole-overlay CAS. A stale publisher cannot overwrite another family's
    // newer shared state. No separate neighborhood state file/head is used.
    // Optional current property quote replaces only ValueInArch in the same
    // occupancy/head CAS. It never adds cash; absence preserves legacy cache.
    std::filesystem::path publish(odh::SessionBundle bundle,
        std::optional<std::int32_t> property_value = std::nullopt);
    void publish_vacation_phase(const odh::FamilyVacationPhase&,
        const odh::FamilyVacationSnapshot& preview);
private:
    FamilySessionJournal(std::vector<odh::FamilyEntry>,std::array<std::uint8_t,32>,
        std::optional<odh::FamilyOverlay>,std::uint16_t,std::uint16_t,
        std::optional<odh::FamilyOverlay>,bool verify_disk, LinuxNeighborhoodStorage storage);
    void validate_bundle(const odh::SessionBundle&,bool publishing=false) const;
    bool carry_accepted() const;
    void commit_overlay(const odh::FamilyOverlay&);
    std::vector<odh::FamilyEntry> source_;
    std::array<std::uint8_t, 32> identity_{};
    std::optional<odh::FamilyOverlay> expected_;
    std::optional<odh::FamilyOverlay> pending_move_in_;
    odh::FamilyOverlayFamilyState state_;
    std::int32_t family_number_ = 0;
    std::vector<std::uint32_t> member_guids_;
    std::optional<odh::HouseholdTransferTicket> accepted_carry_;
    std::vector<odh::SessionHouseholdMember> carry_members_;
    bool durability_uncertain_ = false;
    std::filesystem::path overlay_path_, archive_directory_;
};

} // namespace odh_host
