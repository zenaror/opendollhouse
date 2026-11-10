#pragma once

#include "odh/session_resource_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace odh_host {

// Streaming SHA-256 for authenticated resource bytes. Uses the existing native
// OpenSSL backend when available, otherwise the portable odh::Sha256. No file
// cache or identity policy is introduced: callers retain their exact reads,
// limits and before/after checks. The ABI does not expose OpenSSL types.
class ResourceSha256 {
public:
    ResourceSha256();
    ~ResourceSha256();
    ResourceSha256(const ResourceSha256&) = delete;
    ResourceSha256& operator=(const ResourceSha256&) = delete;
    void update(const void*, std::size_t);
    std::array<std::uint8_t,32> finish();
private:
    struct State;
    std::unique_ptr<State> state_;
};


// Hashes a canonical, path-sorted manifest of the selected game files. Paths
// may be absolute or relative to game_root; the resolved files must stay
// inside that root. File contents are read in bounded-size blocks.
odh::SessionResourceIdentity identify_session_resources(
    const std::filesystem::path& game_root,
    const std::vector<std::filesystem::path>& resources,
    std::uint8_t language_code, std::uint8_t expansion_mask,
    std::uint64_t maximum_total_bytes = 16ull * 1024ull * 1024ull * 1024ull);

} // namespace odh_host
