#pragma once

#include "odh/entity_scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct EntityVmSchedulerSnapshotCodecLimits {
    std::size_t max_entities = 4096;
    std::size_t max_bytes = 1024u * 1024u;
    std::size_t max_diagnostic_bytes = 64u * 1024u;
};

// Serializes scheduler counters and entity identities only. The host must
// register equivalent VMs and hooks before restoring the decoded state.
std::vector<std::uint8_t> encode_entity_vm_scheduler_snapshot(
    const EntityVmSchedulerSnapshot& snapshot,
    EntityVmSchedulerSnapshotCodecLimits limits = {});
EntityVmSchedulerSnapshot decode_entity_vm_scheduler_snapshot(
    const std::vector<std::uint8_t>& bytes,
    EntityVmSchedulerSnapshotCodecLimits limits = {});

} // namespace odh
