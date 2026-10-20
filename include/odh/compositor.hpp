#pragma once

#include "odh/raster.hpp"
#include "odh/sprite.hpp"

namespace odh {

struct SpriteLayer {
    const Spr2Frame* frame;
    std::int32_t x, y; // Top-left screen placement; decoded frame offsets are caller-owned.
    float back_depth, front_depth_delta;
    bool flip = false;
    std::uint16_t owner_id = 0; // Optional picking token; zero denotes no object.
};

struct CompositorLimits {
    std::uint32_t layers = 4096, pixels = 1048576, width = 2048, height = 2048;
    std::uint64_t samples = 16777216;
};

struct CompositeFrame {
    RasterFrame image;
    // Sprite-only counters; image retains the base raster's counters.
    std::uint64_t tested_samples = 0, shaded_samples = 0,
                  transparent_samples = 0, occluded_samples = 0;
    // Opt-in, one token per base pixel. Transparent/occluded samples preserve
    // the prior token; accepted samples write the layer token with depth.
    std::vector<std::uint16_t> owner_ids = {};
};

// Merge opaque raster frames in the same screen/depth coordinate space. Smaller
// depth wins; ties keep base, and layer +infinity never covers base. Dimensions
// must be equal and nonzero, arrays exact, RGBA opaque, and depths finite or
// +infinity. Viewport/pixel limits bound each frame and the owned result. Raster
// tested/shaded counters are summed (not final visible-pixel counts); each input
// must have shaded <= tested and the cumulative tested count must fit samples
// without overflow. Merge comparisons do not add raster samples. All validation
// precedes copying; invalid inputs throw FormatError with inputs unchanged.
RasterFrame merge_raster(const RasterFrame& base, const RasterFrame& layer,
                         RasterLimits = {});

// Integer 1:1 placement and nearest sampling, with horizontal color/Z mirroring.
// Depth is back_depth + ((1 - z/255.0)/0.4)*front_depth_delta, evaluated in double
// then stored as float. Smaller depth wins; ties keep the existing fragment.
// Explicit ODH straight-RGBA profile: alpha zero discards without writing depth;
// other alpha values write depth and source-over RGB onto an opaque destination,
// using (src*alpha + dst*(255-alpha) + 127)/255; output alpha remains 255.
// This profile does not establish GPU or original-game blending equivalence.
// Base dimensions must be nonzero, arrays exact, RGBA opaque, and depth finite or
// +infinity. All layers, including clipped/empty ones, require nonnull frames,
// pixel and Z flags, no unknown flags, exact RGBA/Z arrays, and finite parameters
// whose full byte-Z depth range is representable as float. Indices are ignored.
// Width/height limits apply to the base and each sprite. The pixel limit bounds
// the base and, separately, the sum of all layer extents (repeated frames count
// again). Every clipped bounding-box candidate counts toward the cumulative
// sample budget. Empty sprite extents are allowed. All validation precedes
// copying/rendering; invalid
// inputs or exceeded budgets throw FormatError, without mutating inputs or
// returning a partial result.
// record_owners adds a zero-initialized uint16 token for each base pixel (at most
// 2*limits.pixels bytes), updated in this same fragment pass; disabled is empty.
CompositeFrame composite_sprites(const RasterFrame& base,
                                const std::vector<SpriteLayer>& layers,
                                CompositorLimits = {}, bool record_owners = false);

} // namespace odh
