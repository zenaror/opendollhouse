#pragma once

#include "odh/formats.hpp"

namespace odh {

struct DgrpLimits {
    std::uint32_t images = 4096;
    std::uint32_t sprites = 4096; // Cumulative across all images.
};
struct DgrpSprite {
    std::uint32_t sprite_id, frame, flags;
    std::int32_t x, y;
    std::array<std::uint32_t, 3> object_offset_bits{}; // X, Y, Z IEEE-754 bits.
    std::optional<std::uint16_t> legacy_type;
};
struct DgrpImage {
    std::uint32_t direction, zoom;
    std::vector<DgrpSprite> sprites;
};
struct Dgrp {
    std::uint16_t version;
    std::vector<DgrpImage> images;
    std::uint64_t remaining_bytes;
};
// Explicit on-disk layouts 20000..20004; flags and float bits stay raw.
Dgrp read_dgrp(const View&, const DgrpLimits& = {});
// Rotate the raw direction byte by two bits per world quarter turn, then
// match direction and zoom exactly. Missing is null; ambiguous is an error.
const DgrpImage* dgrp_image(const Dgrp&, std::uint8_t direction,
                            std::uint32_t zoom, std::uint8_t world_rotation = 0);

} // namespace odh
