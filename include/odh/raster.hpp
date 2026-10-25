#pragma once

#include "odh/bitmap.hpp"

namespace odh {

enum class TextureFilter { nearest, bilinear };

struct RasterVertex {
    std::array<float,3> position; // Screen x, y and depth; smaller depth wins.
    std::array<float,2> texcoord;
    std::array<float,3> normal;
    std::array<float,3> color{1,1,1}; // Opaque RGB modulation in [0,1].
};
struct RasterTriangle {
    std::array<RasterVertex,3> vertices;
    std::size_t texture;
    std::uint16_t owner_id = 0; // Optional caller-defined picking token; zero is unowned.
};
struct RasterOptions {
    std::array<std::uint8_t,4> clear{245,245,242,255};
    bool lighting = false;
    TextureFilter filter = TextureFilter::bilinear;
    bool vertex_colors = false;
    bool directional_up_light = false; // With lighting: (dot(normal,up)+1)/2.
    bool record_owner_ids = false; // Allocate/write one token per pixel only when requested.
    bool perspective_correct = false; // Correct depth and varyings; vertex depth must be positive.
};
struct RasterLimits {
    std::uint32_t width = 2048, height = 2048, pixels = 1048576;
    std::uint32_t triangles = 131072, textures = 16, texture_pixels = 1048576;
    std::uint64_t samples = 16777216;
};
struct RasterFrame {
    std::uint32_t width, height;
    std::vector<std::array<std::uint8_t,4>> pixels;
    std::vector<float> depth;
    std::uint64_t tested_samples = 0, shaded_samples = 0;
    // Empty unless a raster pass requested owner recording.
    std::vector<std::uint16_t> owner_ids;
};

// CPU rasterizer. Both windings use pixel centers and the top-left edge rule.
// Depth starts at +infinity; ties retain the first fragment. By default depth
// and UVs are affine. perspective_correct instead uses positive camera-space
// vertex depths for reciprocal-depth testing and perspective-correct varyings.
// UVs wrap in both axes, without a V flip; bilinear filtering uses texel centers.
// Output is opaque RGBA with rounded RGB, without gamma, alpha or mips.
// Optional lighting normalizes interpolated normals (zero chooses up), then
// applies sqrt((dot(normal, up)+1)/2)*0.5+0.5, a simple FreeSO shader profile.
// directional_up_light instead uses (dot(normal,up)+1)/2. vertex_colors enables
// affine RGB modulation before lighting; all vertex colors must be finite in
// [0,1], even when modulation is disabled. Defaults preserve previous output.
// All textures, including unused ones, must contain exactly width*height opaque
// pixels. Nonfinite vertices, invalid indices or exceeded budgets throw
// FormatError. Each clipped bounding-box candidate counts toward samples,
// including candidates outside triangle edges. No input is mutated and no
// partial frame is returned on failure. No GPU or original-game equivalence is
// implied, and edge arithmetic is double precision without subpixel quantization.
RasterFrame rasterize(std::uint32_t width, std::uint32_t height,
                      const std::vector<RasterTriangle>&,
                      const std::vector<BitmapImage>&,
                      RasterOptions = {}, RasterLimits = {});

} // namespace odh
