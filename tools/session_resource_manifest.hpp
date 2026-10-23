#pragma once

#include "odh/session_resource_identity.hpp"

#include <filesystem>
#include <vector>

namespace odh_host {

// Hashes a canonical, path-sorted manifest of the selected game files. Paths
// may be absolute or relative to game_root; the resolved files must stay
// inside that root. File contents are read in bounded-size blocks.
odh::SessionResourceIdentity identify_session_resources(
    const std::filesystem::path& game_root,
    const std::vector<std::filesystem::path>& resources,
    std::uint8_t language_code, std::uint8_t expansion_mask,
    std::uint64_t maximum_total_bytes = 16ull * 1024ull * 1024ull * 1024ull);

} // namespace odh_host
