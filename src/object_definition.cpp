#include "odh/object_definition.hpp"

namespace odh {

std::vector<std::int16_t> read_object_definition_words(const View& payload) {
    const auto definition=read_object_definition(payload);
    const auto limit=definition.version==136 ? 80U : 95U;
    const auto count=static_cast<std::size_t>(
        payload.size()/2<limit ? payload.size()/2 : limit);
    const auto signed_word=[](std::uint32_t raw) {
        return static_cast<std::int16_t>(raw<=32767 ? static_cast<std::int32_t>(raw)
            : static_cast<std::int32_t>(raw)-65536);
    };
    std::vector<std::int16_t> words;
    words.reserve(count);
    // Match the source's modulo65535 (not a low-word mask).
    words.push_back(signed_word(definition.version%0xffffU));
    words.push_back(signed_word(definition.version>>16));
    for(std::size_t index=2;index<count;++index)
        words.push_back(signed_word(payload.le16(index*2)));
    return words;
}

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
            master != 0, extent, payload.size() - extent, prefix.le16(14), {}, {}, {}, {}, {}, {}, {}};
    if (payload.size() >= 42) {
        result.slot_table_id = payload.le16(40);
        result.header_extent = 42;
        result.remaining_bytes = payload.size() - result.header_extent;
    }
    constexpr std::uint64_t global_extent=86;
    if(payload.size()>=global_extent){
        result.global_flag=payload.le16(84);
        result.header_extent=global_extent;
        result.remaining_bytes=payload.size()-global_extent;
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
    constexpr std::uint64_t build_mode_extent=140;
    if(payload.size()>=build_mode_extent) {
        result.build_mode_type=payload.le16(138);
        result.header_extent=build_mode_extent;
        result.remaining_bytes=payload.size()-build_mode_extent;
    }
    constexpr std::uint64_t collision_extent=156;
    if(payload.size()>=collision_extent) {
        result.tile_width=payload.le16(134);
        result.footprint_mask=payload.le16(154);
        result.header_extent=collision_extent;
        result.remaining_bytes=payload.size()-collision_extent;
    }
    if(version==136&&payload.size()>=160)result.type_attribute_guid=0;
    if(version==138&&payload.size()>=188){
        const auto raw=payload.le32(184);
        result.type_attribute_guid=raw?raw:result.guid;
        result.header_extent=188;
        result.remaining_bytes=payload.size()-188;
    }
    return result;
}

} // namespace odh
