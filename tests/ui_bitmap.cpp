#include "odh/ui_bitmap.hpp"

#include <cassert>
#include <limits>

int main() {
    using Pixel = std::array<std::uint8_t, 4>;
    const Pixel key{255, 0, 255, 255};
    const Pixel clear{12, 34, 56, 255};
    const Pixel ink{200, 100, 40, 255};
    odh::BitmapImage source{3, 2, 24, false,
        {key, ink, key, ink, key, ink}, 0};
    odh::RasterFrame destination{};
    destination.width = 3; destination.height = 3;
    destination.pixels.assign(9, clear);
    destination.depth.assign(9, 7.0f);
    destination.owner_ids.assign(9, 12);

    assert(odh::blit_color_keyed_bitmap(destination, source, 1, 1, {255, 0, 255}) == 3);
    assert(destination.pixels[3] == clear);
    assert(destination.pixels[4] == ink);
    assert(destination.pixels[5] == clear);
    assert(destination.pixels[7] == ink);
    assert(destination.depth == std::vector<float>(9, 7.0f));
    assert(destination.owner_ids == std::vector<std::uint16_t>(9, 12));

    destination.pixels.assign(9, clear);
    assert(odh::blit_color_keyed_bitmap(destination, source, -1, -1, {255, 0, 255}) == 1);
    assert(destination.pixels[0] == clear);
    assert(destination.pixels[1] == ink);
    assert(odh::blit_color_keyed_bitmap(destination, source, 4, 0, {255, 0, 255}) == 0);

    bool malformed_destination = false;
    auto broken = destination;
    broken.pixels.pop_back();
    try { (void)odh::blit_color_keyed_bitmap(broken, source, 0, 0, {255, 0, 255}); }
    catch (const odh::FormatError&) { malformed_destination = true; }
    assert(malformed_destination);

    bool malformed_source = false;
    auto broken_source = source;
    broken_source.pixels.pop_back();
    try { (void)odh::blit_color_keyed_bitmap(destination, broken_source, 0, 0, {255, 0, 255}); }
    catch (const odh::FormatError&) { malformed_source = true; }
    assert(malformed_source);

    bool extreme_coordinate_safe = false;
    try {
        assert(odh::blit_color_keyed_bitmap(destination, source,
            std::numeric_limits<std::int32_t>::min(), 0, {255, 0, 255}) == 0);
        extreme_coordinate_safe = true;
    } catch (...) {}
    assert(extreme_coordinate_safe);
}
