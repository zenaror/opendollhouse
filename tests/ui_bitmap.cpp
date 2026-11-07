#include "odh/ui_bitmap.hpp"

#include <cassert>
#include <limits>
#include <stdexcept>

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

    // Crop [ink,key] and enlarge each texel to two pixels. The first
    // destination column is clipped, leaving one ink and two clear pixels.
    destination.pixels.assign(9,clear);
    const auto count=odh::blit_color_keyed_bitmap(destination,source,{-1,0,4,2},
        odh::UiBitmapRect{1,0,2,1},{255,0,255});
    if(count!=2||destination.pixels!=std::vector<Pixel>{ink,clear,clear,ink,clear,clear,clear,clear,clear}||
       destination.depth!=std::vector<float>(9,7.0f)||destination.owner_ids!=std::vector<std::uint16_t>(9,12))
        throw std::runtime_error("scaled crop or color-key visibility changed after clipping");
    bool invalid_crop=false;
    try{odh::blit_color_keyed_bitmap(destination,source,{0,0,3,3},odh::UiBitmapRect{2,1,2,1},{255,0,255});}
    catch(const odh::FormatError&){invalid_crop=true;}
    if(!invalid_crop)throw std::runtime_error("out-of-bounds artwork crop accepted");

    // TGA32 backgrounds use actual alpha. Transparent black leaves the lot
    // visible; partial edges blend, while BMP callers retain opaque semantics.
    odh::BitmapImage alpha_source{3,1,32,true,
        {{0,0,0,0},{200,100,40,128},ink},0};
    destination.pixels.assign(9,clear);
    const auto alpha_count=odh::blit_color_keyed_bitmap(destination,alpha_source,0,0,{255,0,255});
    const Pixel blended{106,67,48,255};
    if(alpha_count!=2||destination.pixels[0]!=clear||destination.pixels[1]!=blended||destination.pixels[2]!=ink||
       destination.depth!=std::vector<float>(9,7.0f)||destination.owner_ids!=std::vector<std::uint16_t>(9,12))
        throw std::runtime_error("TGA alpha composition changed scene visibility, depth or picking");
    alpha_source.bit_count=24;destination.pixels.assign(9,clear);
    if(odh::blit_color_keyed_bitmap(destination,alpha_source,0,0,{255,0,255})!=3||
       destination.pixels[0]!=Pixel{0,0,0,255}||destination.pixels[1]!=ink)
        throw std::runtime_error("BMP colour-key callers unexpectedly acquired alpha semantics");

    odh::BitmapImage rgba{4,1,32,true,{{255,0,255,255},{0,0,0,255},{40,80,120,128},{255,0,255,0}},0};
    destination.width=4;destination.height=1;destination.pixels.assign(4,clear);
    destination.depth.assign(4,7.f);destination.owner_ids.assign(4,12);
    const auto rgba_count=odh::blit_rgba_bitmap(destination,rgba,{0,0,4,1});
    if(rgba_count!=3||destination.pixels[0]!=Pixel{255,0,255,255}||destination.pixels[1]!=Pixel{0,0,0,255}||
       destination.pixels[2]!=Pixel{26,57,88,255}||destination.pixels[3]!=clear||
       destination.depth!=std::vector<float>(4,7.f)||destination.owner_ids!=std::vector<std::uint16_t>(4,12))
        throw std::runtime_error("RGBA sprites must retain opaque key colours and blend real alpha without picking changes");
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
