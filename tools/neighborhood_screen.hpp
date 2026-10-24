#pragma once

#include "linux_window.hpp"
#include "odh/session_entry.hpp"
#include "odh/neighborhood_picker.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace odh_host {

struct PreparedNeighborhoodMap {
    odh::RasterFrame frame;
    std::vector<odh::NeighborhoodMarker> markers;
    unsigned marker_scale = 1;
    std::vector<odh::FamilyEntry> source_families;
    std::vector<odh::FamilyEntry> effective_families;
};

struct MoveInFamilySelection {
    std::uint16_t family_chunk_id = 0;
    std::array<std::uint8_t, 32> family_identity{};
};

enum class OccupiedLotChoice { enter, move_out, cancel };

PreparedNeighborhoodMap prepare_neighborhood_map(
    const std::filesystem::path& user_data,
    const std::filesystem::path& ui_graphics, unsigned mode,
    const odh::FamilyOverlay* saved_overlay = nullptr);

// Build the exact 800x600 neighborhood frame without opening an X11 window.
odh::RasterFrame compose_neighborhood_picker_frame(
    const PreparedNeighborhoodMap& map,
    const std::string& localized_title = "Neighborhood",
    const std::string& localized_cancel = "Cancel",
    std::uint8_t language = 1);

// First-run choice for one of the 20 language codes present in The Sims 1 PC.
// Returns nullopt when the player cancels or closes the window.
std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
                                                  std::uint8_t initial_language);

// Show the real neighborhood map in an existing 800x600 window.
// Returns a house number on selection, or nullopt when the user cancels.
std::optional<std::uint16_t> show_neighborhood_picker(
    LinuxWindow& window, const PreparedNeighborhoodMap& map,
    const std::string& localized_title = "Neighborhood",
    const std::string& localized_cancel = "Cancel",
    std::uint8_t language = 1);

// Choose whether to enter an occupied lot or move its current family out.
// Move-out is disabled when the saved family history cannot calculate a
// verified refund. Escape cancels; E selects move-out; Enter/click enters.
OccupiedLotChoice show_occupied_lot_choice(LinuxWindow& window,
    const odh::FamilyEntry& family, std::uint16_t house_number,
    bool move_out_supported, std::uint8_t language);

// Select a currently unhoused family that can afford the selected vacant lot.
std::optional<MoveInFamilySelection> show_move_in_family_picker(
    LinuxWindow& window, const std::vector<odh::FamilyEntry>& families,
    std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
    const std::string& localized_action = "Move In Family",
    const std::string& localized_title = "SELECT A FAMILY",
    const std::string& localized_no_family_title = "Sorry",
    const std::string& localized_no_family_message = "You can't afford it.",
    const std::string& localized_cancel = "Cancel");

// Final confirmation shown before a player-state write; closing cancels it.
bool show_move_in_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
    std::uint16_t house_number, std::int32_t purchase_price, std::uint8_t language,
    const std::string& localized_title = "Purchase House?",
    const std::string& localized_question = "Are you sure you want to buy this house?",
    const std::string& localized_yes = "Yes", const std::string& localized_no = "No");

// Confirm a move-out and show the source ValueInArch refund before writing.
bool show_move_out_confirmation(LinuxWindow& window, const odh::FamilyEntry& family,
    std::uint16_t house_number, std::uint8_t language,
    const std::string& localized_title = "Evict Family?",
    const std::string& localized_yes = "Yes", const std::string& localized_no = "No");

// Resume the already-loaded lot from its neighborhood marker. Other lots remain
// unavailable while this session owns one live World.
std::optional<std::uint16_t> show_neighborhood_resume_picker(
    LinuxWindow& window, const PreparedNeighborhoodMap& map, std::uint16_t active_house);

// Select a family member in the existing window. Rows identify Sims by their
// neighborhood ID and character resource filename; this is not a name editor.
std::optional<std::size_t> show_resident_picker(
    LinuxWindow& window, const std::vector<odh::SessionEntryMember>& residents,
    std::uint8_t language);

} // namespace odh_host
