#pragma once

#include "odh/family_overlay.hpp"
#include "linux_user_config.hpp"

#include <array>
#include <filesystem>
#include <optional>

namespace odh_host {

// Linux player-state location, kept outside the installed game data. The raw
// Neighborhood.iff identity authenticates data within the selected neighborhood.
std::filesystem::path linux_family_overlay_path(
    const std::array<std::uint8_t, 32>& neighborhood_identity, LinuxNeighborhoodStorage storage = {});

std::array<std::uint8_t, 32> linux_neighborhood_identity(
    const std::filesystem::path& neighborhood_iff);

// Finds the player's saved overlay using the exact bytes of Neighborhood.iff.
// A missing overlay is normal; an unreadable source or invalid saved file is not.
std::optional<odh::FamilyOverlay> read_linux_family_overlay_for_neighborhood(
    const std::filesystem::path& neighborhood_iff, LinuxNeighborhoodStorage storage = {});

std::optional<odh::FamilyOverlay> read_linux_family_overlay(
    const std::filesystem::path& path);
void write_linux_family_overlay_atomic(
    const std::filesystem::path& path, const odh::FamilyOverlay& overlay);

// Cross-process move-in transaction: hold a stable sibling lock, verify the
// overlay still equals expected, then replace it atomically or fail unchanged.
void compare_exchange_linux_family_overlay_atomic(
    const std::filesystem::path& path,
    const std::optional<odh::FamilyOverlay>& expected,
    const odh::FamilyOverlay& replacement);

} // namespace odh_host
