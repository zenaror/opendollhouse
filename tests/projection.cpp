#include "odh/projection.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void close(float actual, double expected, const char* message) {
    require(std::abs(double(actual) - expected) < 0.0001, message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, message);
}
std::uint32_t bits(float value) {
    std::uint32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
} // namespace

int main() {
    try {
        odh::WorldProjection camera;
        const double root = std::sqrt(3.0 / 8.0);
        const double up_pixel = 128 * std::sqrt(3.0 / 8.0);
        // Analytic single-axis oracles, independently expressed in tile units.
        const std::array<std::array<int, 4>, 4> signs{{
            {1, -1, 1, 1}, {-1, -1, 1, -1},
            {-1, 1, -1, -1}, {1, 1, -1, 1}}};
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            camera.rotation = static_cast<std::uint8_t>(rotation);
            for (unsigned zoom = 1; zoom <= 3; ++zoom) {
                camera.zoom = static_cast<std::uint8_t>(zoom);
                const double width = 32U << (zoom - 1);
                const auto x = odh::project_tile(camera, {1, 0, 0});
                const auto y = odh::project_tile(camera, {0, 1, 0});
                const auto z = odh::project_tile(camera, {0, 0, 1});
                close(x[0], 200 + signs[rotation][0] * width / 2, "tile X screen horizontal");
                close(y[0], 200 + signs[rotation][1] * width / 2, "tile Y screen horizontal");
                close(x[1], 200 + signs[rotation][2] * width / 4, "tile X screen vertical");
                close(y[1], 200 + signs[rotation][3] * width / 4, "tile Y screen vertical");
                close(x[2], -3 * root * signs[rotation][2], "tile X depth independent of zoom");
                close(y[2], -3 * root * signs[rotation][3], "tile Y depth independent of zoom");
                close(z[0], 200, "up has no horizontal displacement");
                close(z[1], 200 - width * std::sqrt(3.0 / 8.0), "up axis foreshortening");
                close(z[2], -1.5, "up moves toward camera depth");
            }
        }
        camera = {};
        const auto world = odh::project_world(camera, {3, 6, 9});
        const auto tile = odh::project_tile(camera, {1, 3, 2});
        for (unsigned n = 0; n < 3; ++n) close(world[n], tile[n], "tile conversion swaps Y/Z exactly once");
        close(world[0], 72, "world XZ diamond horizontal");
        close(world[1], 328 - 2 * up_pixel, "world elevated diamond vertical");
        close(world[2], -12 * root - 3, "world signed affine depth");
        camera.center_tile = {2, 3, 1};
        const auto center = odh::project_tile(camera, camera.center_tile);
        close(center[0], 200, "camera center horizontal");
        close(center[1], 200, "camera center vertical");
        close(center[2], 0, "camera center depth");
        const auto shifted = odh::project_tile(camera, {3, 6, 3});
        for (unsigned n = 0; n < 3; ++n) close(shifted[n], tile[n], "translation of camera and point cancels");
        camera.width = 401; camera.height = 301;
        const auto odd = odh::project_tile(camera, camera.center_tile);
        close(odd[0], 200.5, "odd viewport horizontal center");
        close(odd[1], 150.5, "odd viewport vertical center");

        odh::DgrpSprite sprite{};
        sprite.x = 3; sprite.y = 4;
        odh::Spr2Frame frame{};
        frame.width = 6; frame.height = 8;
        // Raw SPR2 crop origin intentionally extreme: placement must ignore it.
        frame.x = -32768; frame.y = 32767;
        camera = {};
        const std::array<std::array<int, 2>, 4> expected{{
            {203, 228}, {139, 196}, {203, 164}, {267, 196}}};
        const std::array<std::array<float, 3>, 4> back{{
            {0.15f, 0, 0.15f}, {0.15f, 0, 2.85f},
            {2.85f, 0, 2.85f}, {2.85f, 0, 0.15f}}};
        const std::array<std::array<float, 3>, 4> front{{
            {3, 0, 3}, {3, 0, -3}, {-3, 0, -3}, {-3, 0, 3}}};
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
            camera.rotation = static_cast<std::uint8_t>(rotation);
            const auto placed = odh::place_sprite(camera, sprite, frame, {0, 0, 0});
            require(placed.x == expected[rotation][0] && placed.y == expected[rotation][1],
                    "rotation-specific sprite anchor and frame-height subtraction");
            require(!placed.flip, "unflipped default");
            const auto back_projection = odh::project_world(camera, back[rotation]);
            close(placed.back_depth, back_projection[2], "sprite back matches mesh world projection");
            close(placed.front_depth_delta, -6 * root, "sprite front direction toward camera in every rotation");
            const float z153 = placed.back_depth + ((1 - 153 / 255.0f) / 0.4f) * placed.front_depth_delta;
            auto surface = back[rotation];
            for (unsigned n = 0; n < 3; ++n) surface[n] += front[rotation][n];
            close(z153, odh::project_world(camera, surface)[2], "sprite shader Z and mesh share affine depth");
            require(placed.back_depth + 2.5f * placed.front_depth_delta < placed.back_depth,
                    "Z0 wins against Z255 under smaller-depth comparison");
            frame.x = 0; frame.y = 0;
            const auto no_crop = odh::place_sprite(camera, sprite, frame, {0, 0, 0});
            require(no_crop.x == placed.x && no_crop.y == placed.y, "SPR2 crop not counted twice");
            frame.x = -32768; frame.y = 32767;
        }
        camera = {};
        sprite.flags = 0x80000001U;
        sprite.object_offset_bits = {bits(16), bits(0), bits(5)};
        const auto offset = odh::place_sprite(camera, sprite, frame, {0, 0, 0});
        require(offset.x == 267 && offset.y == 181 && offset.flip,
                "DGRP planar and up scaling with horizontal flip bit");
        close(offset.back_depth, float(-3.3 * root - 1.5), "scaled DGRP offset affects shared depth");
        sprite.flags = 2;
        require(!odh::place_sprite(camera, sprite, frame, {0, 0, 0}).flip, "only low flag bit flips");
        const auto rotated_offset = odh::place_sprite(camera, sprite, frame, {0, 0, 0}, float(std::acos(-1.0) / 2));
        require(std::abs(rotated_offset.x - 139) <= 1 && std::abs(rotated_offset.y - 181) <= 1,
                "object facing rotates planar DGRP offset around tile Z");
        close(rotated_offset.back_depth, offset.back_depth, "quarter-turn symmetric offset has equal camera depth");
        camera.center_tile = {1, 2, 3};
        const auto centered_sprite = odh::place_sprite(camera, sprite, frame, {1, 2, 3});
        require(centered_sprite.x == offset.x && centered_sprite.y == offset.y,
                "sprite position translated with camera center");
        close(centered_sprite.back_depth, offset.back_depth, "sprite depth translated with camera center");
        camera = {};
        sprite = {};
        sprite.object_offset_bits[0] = bits(-0.125f); // -0.5 pixel at zoom 3.
        require(odh::place_sprite(camera, sprite, frame, {0, 0, 0}).x == 199,
                "cadge origin included before truncating negative fractional offset");
        sprite.object_offset_bits[0] = bits(-0.25f); // -0.5 vertical pixel.
        require(odh::place_sprite(camera, sprite, frame, {0, 0, 0}).y == 223,
                "cadge baseline included before truncating negative vertical fraction");
        sprite.object_offset_bits = {};
        camera.width = 401; camera.height = 301;
        const auto fractional_origin = odh::place_sprite(camera, sprite, frame, {0, 0, 0});
        require(fractional_origin.x == 200 && fractional_origin.y == 174,
                "explicit final truncation with fractional viewport center");
        auto small_camera = camera;
        small_camera.width = small_camera.height = 16;
        small_camera.center_tile = {0.5f, 0.5f, 0};
        auto pixel_frame = frame;
        pixel_frame.width = pixel_frame.height = 1;
        const auto single_pixel = odh::place_sprite(small_camera, sprite, pixel_frame, {});
        require(single_pixel.x == 8 && single_pixel.y == 7,
                "exact half-tile planar boundary survives sprite truncation");

        const auto infinity = std::numeric_limits<float>::infinity();
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        for (unsigned n = 0; n < 3; ++n) {
            std::array<float, 3> invalid{}; invalid[n] = nan;
            rejects([&] { odh::project_world(camera, invalid); }, "nonfinite world coordinate");
            invalid[n] = infinity;
            rejects([&] { odh::project_tile(camera, invalid); }, "nonfinite tile coordinate");
            auto invalid_camera = camera; invalid_camera.center_tile[n] = nan;
            rejects([&] { odh::project_world(invalid_camera, {}); }, "nonfinite camera center");
            auto invalid_sprite = sprite; invalid_sprite.object_offset_bits[n] = 0x7fc01234U;
            rejects([&] { odh::place_sprite(camera, invalid_sprite, frame, {}); }, "nonfinite raw DGRP bits");
        }
        rejects([&] { odh::place_sprite(camera, sprite, frame, {}, infinity); }, "nonfinite object rotation");
        for (const auto dims : std::array<std::array<unsigned, 2>, 5>{{
                 {0, 1}, {1, 0}, {2049, 1}, {1, 2049}, {1025, 1024}}}) {
            auto invalid_camera = camera; invalid_camera.width = dims[0]; invalid_camera.height = dims[1];
            rejects([&] { odh::project_world(invalid_camera, {}); }, "bounded viewport dimensions");
            auto invalid_frame = frame; invalid_frame.width = static_cast<std::uint16_t>(dims[0]);
            invalid_frame.height = static_cast<std::uint16_t>(dims[1]);
            rejects([&] { odh::place_sprite(camera, sprite, invalid_frame, {}); }, "bounded sprite dimensions");
        }
        camera = {};
        camera.width = 2048; camera.height = 512;
        require(std::isfinite(odh::project_world(camera, {})[0]), "exact viewport pixel and width bounds accepted");
        for (const unsigned zoom : {0U, 4U, 255U}) {
            auto invalid = camera; invalid.zoom = static_cast<std::uint8_t>(zoom);
            rejects([&] { odh::project_world(invalid, {}); }, "invalid zoom");
        }
        auto invalid = camera; invalid.rotation = 4;
        rejects([&] { odh::project_tile(invalid, {}); }, "invalid rotation");
        const auto maximum = std::numeric_limits<float>::max();
        rejects([&] { odh::project_world(camera, {maximum, 0, 0}); }, "projection result float overflow");
        rejects([&] { odh::project_tile(camera, {maximum, 0, 0}); }, "tile-to-world float overflow");
        invalid = camera; invalid.center_tile = {maximum, 0, 0};
        rejects([&] { odh::project_world(invalid, {}); }, "camera world conversion overflow");
        rejects([&] { odh::place_sprite(camera, sprite, frame, {100000000, 0, 0}); },
                "sprite final integer placement overflow");
        auto huge_sprite = sprite; huge_sprite.x = std::numeric_limits<std::int32_t>::max();
        rejects([&] { odh::place_sprite(camera, huge_sprite, frame, {}); }, "local sprite int overflow");
        // The origin fits while the exclusive right edge exceeds INT_MAX.
        camera = {};
        huge_sprite = sprite; huge_sprite.x = std::numeric_limits<std::int32_t>::max() - 200 - 5;
        rejects([&] { odh::place_sprite(camera, huge_sprite, frame, {}); }, "final sprite edge overflow");
        std::cout << checks << " projection checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
