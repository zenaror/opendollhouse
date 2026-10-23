#pragma once

#include "linux_window.hpp"
#include "odh/session_entry.hpp"
#include "odh/neighborhood_picker.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>

namespace odh_host {

struct PreparedNeighborhoodMap {
    odh::RasterFrame frame;
    std::vector<odh::NeighborhoodMarker> markers;
    unsigned marker_scale = 1;
};

PreparedNeighborhoodMap prepare_neighborhood_map(
    const std::filesystem::path& user_data,
    const std::filesystem::path& ui_graphics, unsigned mode,
    const odh::FamilyOverlay* saved_overlay = nullptr);

// First-run choice for one of the 20 language codes present in The Sims 1 PC.
// Returns nullopt when the player cancels or closes the window.
std::optional<std::uint8_t> show_language_picker(LinuxWindow& window,
                                                  std::uint8_t initial_language);

// Show the real neighborhood map in an existing 800x600 window.
// Returns a house number on selection, or nullopt when the user cancels.
std::optional<std::uint16_t> show_neighborhood_picker(
    LinuxWindow& window, const PreparedNeighborhoodMap& map);

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
