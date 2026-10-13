#pragma once

#include "odh/bitmap.hpp"

namespace odh {

enum class TextureFilter { nearest, bilinear };

struct RasterVertex {
    std::array<float,3> position; // Screen x, y and depth; smaller depth wins.
    std::array<float,2> texcoord;
    std::array<float,3> normal;
};
struct RasterTriangle {
    std::array<RasterVertex,3> vertices;
    std::size_t texture;
};
struct RasterOptions {
    std::array<std::uint8_t,4> clear{245,245,242,255};
    bool lighting = false;
    TextureFilter filter = TextureFilter::bilinear;
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
};

// Pure orthographic CPU rasterizer. Both windings use pixel centers and the
// top-left edge rule. Depth starts at +infinity; ties retain the first fragment.
// Affine UVs wrap in both axes, without a V flip; bilinear filtering uses texel
// centers. Output is opaque RGBA with rounded RGB, without gamma, alpha or mips.
// Optional lighting normalizes interpolated normals (zero chooses up), then
// applies sqrt((dot(normal, up)+1)/2)*0.5+0.5, a simple FreeSO shader profile.
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
