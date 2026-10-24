#include "odh/ui_bitmap.hpp"

#include <algorithm>
#include <limits>

namespace odh {

std::size_t blit_color_keyed_bitmap(RasterFrame& destination,
    const BitmapImage& source, std::int32_t x, std::int32_t y,
    std::array<std::uint8_t, 3> color_key) {
    const auto destination_pixels = static_cast<std::uint64_t>(destination.width) * destination.height;
    const auto source_pixels = static_cast<std::uint64_t>(source.width) * source.height;
    if (!destination.width || !destination.height ||
        destination_pixels > std::numeric_limits<std::size_t>::max() ||
        destination.pixels.size() != static_cast<std::size_t>(destination_pixels))
        throw FormatError("UI bitmap destination frame is malformed");
    if (!source.width || !source.height ||
        source_pixels > std::numeric_limits<std::size_t>::max() ||
        source.pixels.size() != static_cast<std::size_t>(source_pixels))
        throw FormatError("UI bitmap source image is malformed");

    const auto left = static_cast<std::int64_t>(x);
    const auto top = static_cast<std::int64_t>(y);
    const auto source_left = std::max<std::int64_t>(0, -left);
    const auto source_top = std::max<std::int64_t>(0, -top);
    const auto source_right = std::min<std::int64_t>(source.width,
        static_cast<std::int64_t>(destination.width) - left);
    const auto source_bottom = std::min<std::int64_t>(source.height,
        static_cast<std::int64_t>(destination.height) - top);
    if (source_left >= source_right || source_top >= source_bottom) return 0;

    std::size_t copied = 0;
    for (auto source_y = source_top; source_y < source_bottom; ++source_y) {
        const auto destination_y = top + source_y;
        for (auto source_x = source_left; source_x < source_right; ++source_x) {
            const auto& pixel = source.pixels[static_cast<std::size_t>(source_y) * source.width +
                static_cast<std::size_t>(source_x)];
            if (pixel[0] == color_key[0] && pixel[1] == color_key[1] && pixel[2] == color_key[2])
                continue;
            const auto destination_x = left + source_x;
            auto& output = destination.pixels[static_cast<std::size_t>(destination_y) * destination.width +
                static_cast<std::size_t>(destination_x)];
            output = {pixel[0], pixel[1], pixel[2], 255};
            ++copied;
        }
    }
    return copied;
}

} // namespace odh
