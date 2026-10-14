#pragma once

#include "odh/compositor.hpp"
#include "odh/spr.hpp"

namespace odh {
struct WallDepth {
    std::uint32_t width, height;
    std::vector<std::uint8_t> pixels;
};
struct WallSpriteLayer {
    const SprFrame* pattern;
    const SprFrame* mask; // Null uses pattern alpha.
    const WallDepth* depth;
    std::int32_t x, y;
    std::uint32_t width, height; // Destination rectangle, independent of sources.
    float back_depth, front_depth_delta;
};
struct WallSpriteLimits {
    std::uint32_t layers = 8192, width = 2048, height = 2048, pixels = 1048576;
    std::uint64_t source_pixel_visits = 16777216, destination_pixels = 16777216,
                  samples = 16777216;
};
// Pure 2D wall profile from the pinned FreeSO wall shader contract. Each source
// is POINT/CLAMP sampled at normalized destination pixel centers, independently.
// Mask alpha replaces pattern alpha; only binary alpha is accepted. Zero alpha
// discards without depth. RGB is unlit and output opaque. Byte Z uses the same
// double-to-float depth mapping as composite_sprites; smaller wins, ties retain
// the existing fragment. No GPU or original-game equivalence is implied.
// All extents must be nonzero and within width/height limits, with exact arrays;
// SPR metadata/indices are ignored. Base must be opaque, depth finite or +inf.
// Every input is validated before copying base, including clipped layers.
// pixels bounds base; source_pixel_visits sums pattern + effective mask + depth
// extents per layer (aliases/repeated layers count again); destination_pixels
// sums full rectangle areas; samples sums clipped candidates. Budget failure
// throws FormatError without changing inputs or returning a partial frame.
CompositeFrame composite_wall_sprites(const RasterFrame& base,
    const std::vector<WallSpriteLayer>& layers, WallSpriteLimits = {});

// FreeSO TextureGenerator's twelve wall configurations: zoom 1=far, 2=medium,
// 3=near; orientation 0..3 follows WallZBufferConfig order. Iterative float
// increments, upper clamp 255, explicit ties-to-even independent of host fenv.
WallDepth wall_sprite_depth(std::uint8_t zoom, std::uint8_t orientation);
} // namespace odh
