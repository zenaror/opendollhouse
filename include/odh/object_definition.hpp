#pragma once

#include "odh/formats.hpp"

namespace odh {

struct ObjectDefinition {
    std::uint32_t version, guid;
    std::uint16_t object_type, master_id;
    std::int16_t sub_index;
    bool is_multitile;
    std::uint64_t header_extent, remaining_bytes;
};

// Explicit OBJD136/138 metadata profiles through GUID (32 bytes). Values are owned
// and raw, including unknown object types and the signed sub-index. The flag
// only means MasterID != 0 according to the pinned FreeSO metadata mapping.
// header_extent is the prefix read, not the complete OBJD header size.
// No complete-definition validation, instance decoding, or construction occurs.
ObjectDefinition read_object_definition(const View&);

} // namespace odh
