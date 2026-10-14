#pragma once
#include "odh/sprite.hpp"
namespace odh {
struct SprHeader {
    std::uint32_t version,frame_count,palette_id_raw;
    std::uint16_t palette_id;
    bool big_endian;
};
struct SprFrame {
    SprHeader header;
    std::uint32_t reserved;
    std::uint16_t width,height;
    std::vector<std::array<std::uint8_t,4>> pixels; // RGBA, alpha0 or255.
    std::vector<std::uint8_t> indices; // Skipped pixels start at0; alpha disambiguates.
    std::uint64_t commands=0,consumed_bytes=0,remaining_bytes=0;
    bool ended_with_marker=false;
};
// Observed offset-table versions502/504/505 plus1000. Endianness is detected
// from the first two bytes, then the full uint32 version is validated. Palette
// reference uses low16, retained alongside raw32; the caller resolves it.
// No missing-palette fallback, wall style conversion, alpha/Z synthesis or
// runtime rendering. Unknown versions/commands and truncated runs fail strictly.
SprHeader read_spr_header(const View&,SpriteLimits={});
SprFrame read_spr_frame(const View&,std::uint32_t frame_index,const Palette&,
                       SpriteLimits={});
}
