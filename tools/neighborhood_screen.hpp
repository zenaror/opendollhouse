#pragma once

#include "linux_window.hpp"
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
    const std::filesystem::path& ui_graphics, unsigned mode);

// Show the real neighborhood map in an existing 800x600 window.
// Returns a house number on selection, or nullopt when the user cancels.
std::optional<std::uint16_t> show_neighborhood_picker(
    LinuxWindow& window, const PreparedNeighborhoodMap& map);

} // namespace odh_host
