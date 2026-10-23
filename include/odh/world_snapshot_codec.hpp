#pragma once

#include "odh/world.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct WorldSnapshotCodecLimits {
    WorldLimits world{};
    std::size_t max_bytes = 64u * 1024u * 1024u;
};

// Explicit versioned representation for the owned World values only.
// Host callbacks, resources, VM programs and renderer state are not included.
std::vector<std::uint8_t> encode_world_snapshot(
    const WorldSnapshot& snapshot, WorldSnapshotCodecLimits limits = {});
WorldSnapshot decode_world_snapshot(
    const std::vector<std::uint8_t>& bytes, WorldSnapshotCodecLimits limits = {});

} // namespace odh
