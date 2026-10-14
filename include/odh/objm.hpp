#pragma once

#include "odh/formats.hpp"
#include <map>

namespace odh {

struct ObjmLimits {
    std::uint64_t payload_bytes = 16U * 1024U * 1024U;
    std::uint64_t owned_block_bytes = 8U * 1024U * 1024U;
    std::uint32_t object_count = 4096;
};
struct ObjmMapRecord {
    std::uint16_t object_id, type_id;
};
struct ObjmBlock {
    // Data starts at payload byte 12 + offset, AFTER its LE32 skip word.
    // Preserve parity for the instance field/string decoder's oddOffset.
    std::uint32_t offset;
    std::vector<std::uint8_t> bytes;
};
struct Objm {
    std::uint32_t reserved, version;
    std::uint8_t compression;
    std::vector<ObjmMapRecord> map_records; // Serialized order, not sorted IDs.
    std::map<std::uint16_t, std::uint16_t> id_to_objt;
    // Block order does not establish the associated object ID. That is encoded
    // inside each instance, which this container reader deliberately leaves raw.
    std::vector<ObjmBlock> blocks;
    std::uint64_t remaining_bytes;
};
// MjbO container, compression 1, direct signed field words cast to uint16_t.
// The container layout is independent of the version-specific instance layout;
// retain version without claiming that its entity state/positions were decoded.
// Duplicate IDs are rejected in this bounded profile (the reference overwrites).
// Owned blocks survive the source View/stream. Unknown tail retains its extent.
Objm read_objm(const View&, const ObjmLimits& = {});

} // namespace odh
