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
// Version11 preserves exact native slot-zero backing; earlier versions leave it unknown.
// Version10 additionally preserves independent native recovery-position policy.
// Unknown new policy retains version9 (or older) bytes unchanged.
// Version9 preserves known/unknown referenced-reset policy for live and retained
// bodies. Earlier versions decode this policy as unknown; all-unknown states
// retain their previous minimal wire version and byte representation.
// Version12 retains proven native sentinel level independently from an absent
// gameplay position. Earlier archives leave it unknown, never infer floor1.
std::vector<std::uint8_t> encode_world_snapshot(
    const WorldSnapshot& snapshot, WorldSnapshotCodecLimits limits = {});
WorldSnapshot decode_world_snapshot(
    const std::vector<std::uint8_t>& bytes, WorldSnapshotCodecLimits limits = {});

} // namespace odh
