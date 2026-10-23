#pragma once

#include "odh/family_overlay.hpp"

#include <array>
#include <filesystem>
#include <optional>

namespace odh_host {

// Linux player-state location, kept outside the installed game data. The raw
// Neighborhood.iff identity gives each unchanged neighborhood its own file.
std::filesystem::path linux_family_overlay_path(
    const std::array<std::uint8_t, 32>& neighborhood_identity);

// Finds the player's saved overlay using the exact bytes of Neighborhood.iff.
// A missing overlay is normal; an unreadable source or invalid saved file is not.
std::optional<odh::FamilyOverlay> read_linux_family_overlay_for_neighborhood(
    const std::filesystem::path& neighborhood_iff);

std::optional<odh::FamilyOverlay> read_linux_family_overlay(
    const std::filesystem::path& path);
void write_linux_family_overlay_atomic(
    const std::filesystem::path& path, const odh::FamilyOverlay& overlay);

} // namespace odh_host
