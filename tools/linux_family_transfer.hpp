#pragma once
#include "odh/household_transfer.hpp"
#include "odh/session_entry.hpp"

namespace odh_host {
std::vector<odh::HouseholdTransferIdentity> identify_transfer_household(
    const odh::SessionEntry&, const std::filesystem::path& game_root,
    const std::filesystem::path& player_character_root);
// Supports current and historical tickets, including an unhoused family.
// The caller pins the overlay and must CAS that exact view before changing it.
std::optional<odh::PreparedHouseholdTransfer> load_family_transfer(
    const odh::NeighborhoodFamilySource&, const odh::FamilyOverlay&,
    std::uint16_t family_chunk_id,
    const std::vector<odh::HouseholdTransferIdentity>&,
    const std::filesystem::path& neighborhood_iff);
}
