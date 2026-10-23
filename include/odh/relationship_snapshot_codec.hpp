#pragma once

#include "odh/relationship.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct RelationshipSnapshotCodecLimits {
    RelationshipLimits relationships{};
    std::size_t max_bytes = 64u * 1024u * 1024u;
};

std::vector<std::uint8_t> encode_relationship_snapshot(
    const RelationshipSnapshot& snapshot, RelationshipSnapshotCodecLimits limits = {});
RelationshipSnapshot decode_relationship_snapshot(
    const std::vector<std::uint8_t>& bytes, RelationshipSnapshotCodecLimits limits = {});

} // namespace odh
