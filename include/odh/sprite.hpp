#pragma once

#include "odh/formats.hpp"

namespace odh {

struct SpriteLimits {
    std::uint32_t frames = 4096, width = 2048, height = 2048,
                  pixels = 1048576, palette_entries = 256, commands = 1048576;
};

struct Palette {
    std::uint32_t version;
    std::array<std::uint8_t, 8> reserved;
    std::vector<std::array<std::uint8_t, 4>> colors; // RGB triples with alpha 255.
    std::uint64_t remaining_bytes;
};

// PALT structural profile: retain raw version/reserved bytes, 1..256 RGB entries.
Palette read_palt(const View&, SpriteLimits = {});

struct Spr2Frame {
    std::uint32_t version, default_palette_id, flags;
    std::uint16_t width, height, palette_id, effective_palette_id, transparent_index;
    std::int16_t x, y;
    std::vector<std::array<std::uint8_t, 4>> pixels; // Row-major RGBA.
    std::vector<std::uint8_t> indices, z; // Z is empty without the Z flag.
    bool ended_with_marker;
    std::uint64_t remaining_bytes; // Within this frame's bounded extent.
};

// SPR2 1000 only. The default palette overrides each frame's raw palette ID
// in this FreeSO 4c6b3e8 reference profile; the caller resolves that palette.
// Decode RLE without FloorCopy, Z-as-alpha or missing-palette fallback semantics.
Spr2Frame read_spr2_frame(const View&, std::uint32_t frame_index,
                          const Palette&, SpriteLimits = {});

// Both modes check the same offset table, frame header, palette key and
// dimension limits as read_spr2_frame. Full also checks RLE and palette
// indices without allocating pixel planes.
enum class Spr2Inspection { geometry, full };
Spr2Frame inspect_spr2_frame(const View&, std::uint32_t frame_index,
                             const Palette&, SpriteLimits, Spr2Inspection);

} // namespace odh
