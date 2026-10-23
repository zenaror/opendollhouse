#pragma once

#include "odh/vm.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace odh {

struct VmSnapshotCodecLimits {
    VmLimits vm{};
    std::size_t max_bytes = 64u * 1024u * 1024u;
    std::size_t max_diagnostic_bytes = 1024u * 1024u;
};

// Value-only VM state. The receiving host must rebuild the same BHAV programs,
// CodeOwners and equivalent memory/primitive callbacks before restore_snapshot.
std::vector<std::uint8_t> encode_vm_snapshot(
    const VmSnapshot& snapshot, VmSnapshotCodecLimits limits = {});
VmSnapshot decode_vm_snapshot(
    const std::vector<std::uint8_t>& bytes, VmSnapshotCodecLimits limits = {});

} // namespace odh
