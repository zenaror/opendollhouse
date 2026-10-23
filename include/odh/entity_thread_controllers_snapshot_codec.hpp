#pragma once

#include "odh/entity_thread_controllers.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct EntityThreadControllersSnapshotCodecLimits {
    std::size_t max_bytes = 16u * 1024u * 1024u;
    std::size_t max_queue_actions = 4096;
    std::size_t max_queue_capacity = 4096;
    std::size_t max_call_arguments = 4096;
    std::size_t max_diagnostic_bytes = 1024u * 1024u;
};

// Values only. Restore still requires the matching World, VM, RNG and
// interrupt bindings to be rebuilt by the host.
std::vector<std::uint8_t> encode_entity_thread_controllers_snapshot(
    const EntityThreadControllersSnapshot& snapshot,
    EntityThreadControllersSnapshotCodecLimits limits = {});
EntityThreadControllersSnapshot decode_entity_thread_controllers_snapshot(
    const std::vector<std::uint8_t>& bytes,
    EntityThreadControllersSnapshotCodecLimits limits = {});

} // namespace odh
