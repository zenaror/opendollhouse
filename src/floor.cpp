#include "odh/floor.hpp"

#include <cmath>
#include <limits>

namespace odh {
namespace {
using Pixel = std::array<std::uint8_t, 4>;

std::uint64_t extent(std::uint32_t width, std::uint32_t height,
                     std::size_t actual, BitmapLimits limits) {
    const auto count = std::uint64_t(width) * height;
    if (!width || !height || width > limits.width || height > limits.height ||
        count > limits.pixels || count != actual)
        throw FormatError("floor texture extent exceeds bounded profile");
    return count;
}
float narrow(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw FormatError("floor geometry nonfinite or unrepresentable coordinate");
    return static_cast<float>(value);
}
double ties_even(double value) {
    const auto lower = std::floor(value);
    const auto remainder = value - lower;
    if (remainder < 0.5) return lower;
    if (remainder > 0.5) return lower + 1;
    return std::fmod(lower, 2.0) == 0 ? lower : lower + 1;
}
} // namespace

BitmapImage floor_texture(const Spr2Frame& source, BitmapLimits limits) {
    if (source.version != 1000 || !(source.flags & 1U) || (source.flags & ~7U))
        throw FormatError("floor texture requires SPR2 1000 with pixel data");
    // Validate both extents before any allocation. Padding is a rendering copy,
    // not a change to the original SPR2 frame width or auxiliary channel strides.
    extent(source.width, source.height, source.pixels.size(), limits);
    const std::uint32_t width = source.width + (source.width & 1U);
    const std::uint32_t height = source.height;
    const auto count = std::uint64_t(width) * height;
    if (width > limits.width || count > limits.pixels ||
        count > std::vector<Pixel>().max_size())
        throw FormatError("floor texture padded extent exceeds budget");
    BitmapImage result{width, height, 32, true, {}, 0};
    result.pixels.resize(static_cast<std::size_t>(count));
    const auto at = [&](std::uint32_t x, std::uint32_t y) -> Pixel {
        if (x == source.width) return {0, 0, 0, 0};
        return source.pixels[std::size_t(y) * source.width + x];
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const auto peer = at((x + width / 2) % width, (y + height / 2) % height);
            const auto pixel = peer[3] >= 254 ? peer : at(x, y);
            if (pixel[3] != 255)
                throw FormatError("FloorCopy retains transparency or partial alpha");
            result.pixels[std::size_t(y) * width + x] = pixel;
        }
    }
    return result;
}

std::vector<RasterTriangle> floor_triangles(
    const WorldProjection& camera, float x, float y, std::uint8_t level,
    const BitmapImage& texture, std::size_t texture_index, FloorHalf half,
    bool align_uv, std::array<float, 4> altitude) {
    extent(texture.width, texture.height, texture.pixels.size(), {});
    for (const auto& pixel : texture.pixels)
        if (pixel[3] != 255) throw FormatError("floor geometry requires opaque texture");
    if (!std::isfinite(x) || !std::isfinite(y) || x < -32768 || x > 32767 ||
        y < -32768 || y > 32767 || level < 1 || level > 2)
        throw FormatError("floor tile coordinate or level outside bounded profile");
    if (half != FloorHalf::whole && half != FloorHalf::horizontal_first &&
        half != FloorHalf::horizontal_second && half != FloorHalf::vertical_first &&
        half != FloorHalf::vertical_second)
        throw FormatError("invalid floor half selection");
    const std::array<std::array<float, 2>, 4> corners{{
        {x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}}};
    std::array<RasterVertex, 4> vertices{};
    for (unsigned i = 0; i < 4; ++i) {
        const auto& corner = corners[i];
        auto& vertex = vertices[i];
        const float z = narrow((level - 1) * 2.95 + altitude[i]);
        vertex.position = project_tile(camera, {corner[0], corner[1], z});
        vertex.normal = {0, 1, 0};
        std::array<double, 2> uv{
            (double(corner[0]) - corner[1] + 1) / 2,
            (double(corner[0]) + corner[1]) / 2};
        if (camera.rotation & 1U) std::swap(uv[0], uv[1]);
        if (align_uv) {
            const auto flattened = project_tile(camera, {corner[0], corner[1], 0});
            const std::array<double, 2> size{double(texture.width), double(texture.height)};
            for (unsigned axis = 0; axis < 2; ++axis)
                uv[axis] = (ties_even(uv[axis] * size[axis] + flattened[axis]) -
                            flattened[axis]) / size[axis];
        }
        vertex.texcoord = {narrow(uv[0]), narrow(uv[1])};
    }
    const auto triangle = [&](unsigned a, unsigned b, unsigned c) {
        return RasterTriangle{{vertices[a], vertices[b], vertices[c]}, texture_index};
    };
    switch (half) {
    case FloorHalf::whole: return {triangle(0, 1, 2), triangle(2, 3, 0)};
    case FloorHalf::horizontal_first: return {triangle(1, 2, 3)};
    case FloorHalf::horizontal_second: return {triangle(0, 1, 3)};
    case FloorHalf::vertical_first: return {triangle(0, 1, 2)};
    case FloorHalf::vertical_second: return {triangle(2, 3, 0)};
    }
    throw FormatError("invalid floor half selection");
}
} // namespace odh
