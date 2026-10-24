#include "odh/object_definition.hpp"

namespace odh {

ObjectDefinition read_object_definition(const View& payload) {
    const auto version = payload.le32(0);
    // Both profiles share this prefix in OBJD.cs at pinned FreeSO revision
    // 4c6b3e8f5835b228723caea3c9f683c62f244f73. Their later field counts differ.
    if (version != 136 && version != 138)
        throw FormatError("unsupported OBJD metadata version (requires 136 or 138)");
    constexpr std::uint64_t extent = 32;
    const auto prefix = payload.subview(0, extent);
    const auto sub = prefix.le16(22);
    const auto signed_sub = static_cast<std::int16_t>(
        sub < 32768 ? static_cast<std::int32_t>(sub)
                    : static_cast<std::int32_t>(sub) - 65536);
    const auto master = prefix.le16(20);
    ObjectDefinition result{version, prefix.le32(28), prefix.le16(18), master, signed_sub,
            master != 0, extent, payload.size() - extent, prefix.le16(14), {}, {}, {}, {}, {}};
    if (payload.size() >= 42) {
        result.slot_table_id = payload.le16(40);
        result.header_extent = 42;
        result.remaining_bytes = payload.size() - result.header_extent;
    }
    // Simitone's v136/v138 profiles share the first 80 OBJD fields. The
    // VMOBJDVariable index includes the two 16-bit version words; LevelOffset
    // is index 56, at byte offset 112 from the payload start.
    constexpr std::uint64_t level_offset_end = 114;
    if (payload.size() >= level_offset_end) {
        result.level_offset = payload.le16(112);
        result.header_extent = level_offset_end;
        result.remaining_bytes = payload.size() - level_offset_end;
    }
    // VMOBJDVariable.NumAttributes is field58 at byte offset116 in the
    // pinned Simitone OBJD profiles. Shorter metadata views remain valid.
    constexpr std::uint64_t attributes_extent = 118;
    if (payload.size() >= attributes_extent) {
        result.num_attributes = payload.le16(116);
        result.header_extent = attributes_extent;
        result.remaining_bytes = payload.size() - attributes_extent;
    }
    // Simitone's pinned OBJD field order places TileWidth at field 65 and
    // FootprintMask at field 75 (byte offsets 134/154 after the version word).
    // Only expose the pair when both are present; shorter metadata views remain
    // valid for callers that need only the stable 32-byte prefix.
    constexpr std::uint64_t collision_extent=156;
    if(payload.size()>=collision_extent) {
        result.tile_width=payload.le16(134);
        result.footprint_mask=payload.le16(154);
        result.header_extent=collision_extent;
        result.remaining_bytes=payload.size()-collision_extent;
    }
    return result;
}

} // namespace odh
