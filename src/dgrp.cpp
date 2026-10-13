#include "odh/dgrp.hpp"

namespace odh {
namespace {
std::int32_t signed32(std::uint32_t value) {
    return static_cast<std::int32_t>(value < 0x80000000U
        ? static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value) - 0x100000000LL);
}
std::int32_t signed16(std::uint16_t value) {
    return value < 32768 ? value : static_cast<std::int32_t>(value) - 65536;
}
}

Dgrp read_dgrp(const View& payload, const DgrpLimits& limits) {
    const auto version = payload.le16(0);
    if (version < 20000 || version > 20004)
        throw FormatError("unsupported DGRP version (20000..20004 profile)");
    const bool legacy = version < 20003;
    const std::uint64_t header_size = legacy ? 4 : 6;
    const auto header = payload.subview(0, header_size);
    const std::uint32_t count = legacy ? header.le16(2) : header.le32(2);
    const std::uint64_t image_size = legacy ? 4 : 12;
    const std::uint64_t sprite_size = legacy ? (version == 20001 ? 16 : 12)
                                           : (version == 20004 ? 32 : 24);
    if (count > limits.images || count > (payload.size() - header_size) / image_size)
        throw FormatError("DGRP image count exceeds payload or configured limit");
    Dgrp result{version, {}, 0};
    result.images.reserve(count);
    std::uint64_t offset = header_size, total_sprites = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto image_header = payload.subview(offset, image_size);
        DgrpImage image{};
        const std::uint32_t sprites = legacy ? image_header.le16(0) : image_header.le32(8);
        image.direction = legacy ? image_header.u8(2) : image_header.le32(0);
        image.zoom = legacy ? image_header.u8(3) : image_header.le32(4);
        offset += image_size;
        if (sprites > limits.sprites - total_sprites || sprites > (payload.size() - offset) / sprite_size)
            throw FormatError("DGRP cumulative sprite count exceeds payload or configured limit");
        total_sprites += sprites;
        image.sprites.reserve(sprites);
        for (std::uint32_t n = 0; n < sprites; ++n) {
            const auto record = payload.subview(offset, sprite_size);
            DgrpSprite sprite{};
            if (legacy) {
                sprite.legacy_type = record.le16(0);
                sprite.sprite_id = record.le16(2);
                sprite.frame = record.le16(4);
                sprite.flags = record.le16(6);
                sprite.x = signed16(record.le16(8));
                sprite.y = signed16(record.le16(10));
                if (version == 20001) sprite.object_offset_bits[2] = record.le32(12);
            } else {
                sprite.sprite_id = record.le32(0);
                sprite.frame = record.le32(4);
                sprite.x = signed32(record.le32(8));
                sprite.y = signed32(record.le32(12));
                sprite.object_offset_bits[2] = record.le32(16);
                sprite.flags = record.le32(20);
                if (version == 20004) {
                    sprite.object_offset_bits[0] = record.le32(24);
                    sprite.object_offset_bits[1] = record.le32(28);
                }
            }
            image.sprites.push_back(sprite);
            offset += sprite_size;
        }
        result.images.push_back(std::move(image));
    }
    result.remaining_bytes = payload.size() - offset;
    return result;
}

const DgrpImage* dgrp_image(const Dgrp& group, std::uint8_t direction,
                            std::uint32_t zoom, std::uint8_t world_rotation) {
    if (world_rotation > 3) throw FormatError("DGRP world rotation exceeds three quarter turns");
    const auto shift = static_cast<unsigned>(world_rotation) * 2;
    const auto rotated = shift == 0 ? direction : static_cast<std::uint8_t>(
        ((static_cast<unsigned>(direction) << shift) & 255U) | (direction >> (8 - shift)));
    const DgrpImage* found = nullptr;
    for (const auto& image : group.images) {
        if (image.direction != rotated || image.zoom != zoom) continue;
        if (found) throw FormatError("ambiguous DGRP direction and zoom match");
        found = &image;
    }
    return found;
}
} // namespace odh
