#pragma once

#include "odh/formats.hpp"

namespace odh {

struct ObjectDefinition {
    std::uint32_t version, guid;
    std::uint16_t object_type, master_id;
    std::int16_t sub_index;
    bool is_multitile;
    std::uint64_t header_extent, remaining_bytes;
    std::uint16_t tree_table_id = 0;
    // VMOBJDVariable index 56, used by VMEntity.FloorChangeValid. Present when the
    // complete supported-profile word is in the payload.
    std::optional<std::uint16_t> level_offset;
    // Present only when the payload reaches the complete stable fields in the
    // supported 136/138 layouts. Values are raw OBJD collision parameters.
    std::optional<std::uint16_t> tile_width;
    std::optional<std::uint16_t> footprint_mask;
};

// Explicit OBJD136/138 metadata profiles through TreeTableID/GUID (32 bytes),
// plus optional collision fields when their complete payload extent is present.
// Values are owned and raw, including unknown object types and signed sub-index. The flag
// only means MasterID != 0 according to the pinned FreeSO metadata mapping.
// header_extent is the highest inspected prefix extent, not full validation.
// No complete-definition validation, instance decoding, or construction occurs.
ObjectDefinition read_object_definition(const View&);

} // namespace odh
