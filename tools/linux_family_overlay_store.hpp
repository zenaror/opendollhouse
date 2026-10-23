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

std::optional<odh::FamilyOverlay> read_linux_family_overlay(
    const std::filesystem::path& path);
void write_linux_family_overlay_atomic(
    const std::filesystem::path& path, const odh::FamilyOverlay& overlay);

} // namespace odh_host
