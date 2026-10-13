#include "odh/projection.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace odh {
namespace {
using Point = std::array<double, 3>;
constexpr double root_half = 0.7071067811865475244;
constexpr double cos_pitch = 0.8660254037844386468;

float checked_float(double value) {
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        throw FormatError("projection float overflow or nonfinite coordinate");
    return static_cast<float>(value);
}
void dimensions(std::uint32_t width, std::uint32_t height) {
    if (!width || !height || width > 2048 || height > 2048 ||
        std::uint64_t(width) * height > 1048576)
        throw FormatError("projection dimensions exceed bounded profile");
}
void finite(Point point) {
    for (const auto value : point) checked_float(value);
}
Point point(std::array<float, 3> value) {
    Point result{value[0], value[1], value[2]};
    finite(result);
    return result;
}
Point tile_world(Point tile) {
    Point result{3 * tile[0], 3 * tile[2], 3 * tile[1]};
    finite(result);
    return result;
}
void camera(const WorldProjection& projection) {
    dimensions(projection.width, projection.height);
    if (projection.zoom < 1 || projection.zoom > 3 || projection.rotation > 3)
        throw FormatError("invalid world projection zoom or rotation");
    tile_world(point(projection.center_tile));
}
double tile_width(const WorldProjection& projection) {
    return double(32U << (projection.zoom - 1));
}
std::array<double, 2> planar(const WorldProjection& projection, Point relative) {
    // Exact quarter-turn signs avoid trig rounding at integer pixel boundaries.
    const double x = relative[0], z = relative[2];
    double horizontal, forward;
    switch (projection.rotation) {
    case 0: horizontal = x - z; forward = x + z; break;
    case 1: horizontal = -x - z; forward = x - z; break;
    case 2: horizontal = -x + z; forward = -x - z; break;
    default: horizontal = x + z; forward = -x + z; break;
    }
    return {horizontal, forward};
}
Point rotated(const WorldProjection& projection, Point relative) {
    const auto axes = planar(projection, relative);
    const auto forward = axes[1] * root_half;
    return {axes[0] * root_half, relative[1] * cos_pitch - forward * 0.5,
            relative[1] * 0.5 + forward * cos_pitch};
}
std::array<double, 2> pixel_offset(const WorldProjection& projection, Point relative) {
    const auto axes = planar(projection, relative);
    const auto width = tile_width(projection);
    // Algebraically eliminate sqrt(1/2)^2 for planar coordinates. This keeps
    // integer tile pixel boundaries exact before the placement truncation.
    return {width * axes[0] / 6,
            width * axes[1] / 12 - width * root_half * cos_pitch * relative[1] / 3};
}
Point screen(const WorldProjection& projection, Point world) {
    const auto center = tile_world(point(projection.center_tile));
    for (unsigned n = 0; n < 3; ++n) world[n] -= center[n];
    finite(world);
    const auto view = rotated(projection, world);
    const auto pixels = pixel_offset(projection, world);
    Point result{projection.width / 2.0 + pixels[0],
                 projection.height / 2.0 + pixels[1], -view[2]};
    finite(result);
    return result;
}
std::array<float, 3> narrow(Point point) {
    return {checked_float(point[0]), checked_float(point[1]), checked_float(point[2])};
}
float offset_float(std::uint32_t bits) {
    static_assert(sizeof(float) == sizeof(bits) && std::numeric_limits<float>::is_iec559,
                  "DGRP projection requires IEEE-754 binary32 floats");
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return checked_float(value);
}
double truncate(double value) {
    if (!std::isfinite(value)) throw FormatError("nonfinite sprite placement");
    const auto result = std::trunc(value);
    if (result < std::numeric_limits<std::int32_t>::min() ||
        result > std::numeric_limits<std::int32_t>::max())
        throw FormatError("sprite placement int32 overflow");
    return result;
}
} // namespace

std::array<float, 3> project_world(const WorldProjection& projection,
                                 std::array<float, 3> world) {
    camera(projection);
    return narrow(screen(projection, point(world)));
}
std::array<float, 3> project_tile(const WorldProjection& projection,
                                std::array<float, 3> tile) {
    camera(projection);
    return narrow(screen(projection, tile_world(point(tile))));
}

SpritePlacement place_sprite(const WorldProjection& projection, const DgrpSprite& sprite,
                             const Spr2Frame& frame, std::array<float, 3> object_tile,
                             float object_radians) {
    camera(projection);
    dimensions(frame.width, frame.height);
    checked_float(object_radians);
    auto object = tile_world(point(object_tile));
    Point offset{offset_float(sprite.object_offset_bits[0]) / 16.0,
                 offset_float(sprite.object_offset_bits[1]) / 16.0,
                 offset_float(sprite.object_offset_bits[2]) / 5.0};
    const double c = std::cos(double(object_radians)), s = std::sin(double(object_radians));
    const auto ox = offset[0];
    offset[0] = ox * c - offset[1] * s;
    offset[1] = ox * s + offset[1] * c;
    const auto offset_world = tile_world(offset);
    const auto offset_pixels = pixel_offset(projection, offset_world);
    // The cadge origin cancels the scroll terms only AFTER local truncation:
    // trunc(17 - 0.5) - 17 is -1, while trunc(-0.5) would incorrectly be zero.
    const double cadge_half = 17U << (projection.zoom - 1);
    const double cadge_baseline = 87U << (projection.zoom - 1);
    const auto local_x = truncate(cadge_half + double(sprite.x) + offset_pixels[0]) - cadge_half;
    const auto local_y = truncate(cadge_baseline + double(sprite.y) - frame.height +
                                  offset_pixels[1]) - cadge_baseline;
    const auto origin = screen(projection, object);
    const auto width = tile_width(projection);
    const std::array<std::array<double, 2>, 4> anchor{{
        {0, width / 4}, {-width / 2, 0}, {0, -width / 4}, {width / 2, 0}}};
    const auto x = truncate(origin[0] + local_x + anchor[projection.rotation][0]);
    const auto y = truncate(origin[1] + local_y + anchor[projection.rotation][1]);
    truncate(x + frame.width);
    truncate(y + frame.height);
    const std::array<Point, 4> back{{{0.15, 0, 0.15}, {0.15, 0, 2.85},
                                  {2.85, 0, 2.85}, {2.85, 0, 0.15}}};
    const std::array<Point, 4> front{{{3, 0, 3}, {3, 0, -3},
                                   {-3, 0, -3}, {-3, 0, 3}}};
    for (unsigned n = 0; n < 3; ++n)
        object[n] += offset_world[n] + back[projection.rotation][n];
    finite(object);
    const auto back_depth = checked_float(screen(projection, object)[2]);
    const auto front_delta = checked_float(-rotated(projection, front[projection.rotation])[2]);
    return {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
            back_depth, front_delta, (sprite.flags & 1U) != 0};
}

} // namespace odh
