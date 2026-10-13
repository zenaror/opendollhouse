#include "odh/compositor.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using Pixel = std::array<std::uint8_t,4>;
const Pixel red{255,0,0,255}, green{0,255,0,255}, blue{0,0,255,255};
int checks = 0;
void require(bool valid, const char* why) {
    ++checks;
    if(!valid) throw std::runtime_error(why);
}
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch(const odh::FormatError&) { rejected = true; }
    require(rejected, "invalid compositor input or budget rejected");
}
odh::RasterFrame base(std::uint32_t width = 1, std::uint32_t height = 1,
                      Pixel color = blue, float z = std::numeric_limits<float>::infinity()) {
    const auto count = static_cast<std::size_t>(width)*height;
    return {width, height, std::vector<Pixel>(count,color), std::vector<float>(count,z), 19, 7};
}
odh::Spr2Frame sprite(std::uint16_t width = 1, std::uint16_t height = 1,
                     Pixel color = red, std::uint8_t z = 255) {
    odh::Spr2Frame result{};
    result.version = 1000; result.flags = 3; result.width = width; result.height = height;
    const auto count = static_cast<std::size_t>(width)*height;
    result.pixels.assign(count,color); result.z.assign(count,z);
    return result;
}
odh::SpriteLayer layer(const odh::Spr2Frame& frame, std::int32_t x = 0,
                       std::int32_t y = 0, float back = 0, float delta = 0, bool flip = false) {
    return {&frame,x,y,back,delta,flip};
}
} // namespace

int main() { try {
    const auto backdrop = base();
    auto solid = sprite();
    const auto unchanged = odh::composite_sprites(backdrop,{});
    require(unchanged.image.pixels == backdrop.pixels && unchanged.image.depth == backdrop.depth &&
            unchanged.tested_samples == 0 && unchanged.shaded_samples == 0 &&
            unchanged.transparent_samples == 0 && unchanged.occluded_samples == 0,
            "no layers copies base with zero sprite counters");
    require(unchanged.image.tested_samples == 19 && unchanged.image.shaded_samples == 7,
            "sprite counters do not overwrite base raster counters");
    const auto opaque = odh::composite_sprites(backdrop,{layer(solid)});
    require(opaque.image.pixels[0] == red && opaque.image.depth[0] == 0 &&
            opaque.tested_samples == 1 && opaque.shaded_samples == 1,
            "opaque sprite writes color and depth");
    auto colored_base = base(1,1,{17,123,244,255});
    auto alpha_sprite = sprite(1,1,{240,80,1,255});
    alpha_sprite.flags = 7;
    for(unsigned alpha = 0; alpha <= 255; ++alpha) {
        alpha_sprite.pixels[0][3] = static_cast<std::uint8_t>(alpha);
        const auto frame = odh::composite_sprites(colored_base,{layer(alpha_sprite)});
        Pixel expected{};
        for(unsigned channel = 0; channel < 3; ++channel)
            expected[channel] = static_cast<std::uint8_t>(std::lround(
                (double(alpha_sprite.pixels[0][channel])*alpha +
                 double(colored_base.pixels[0][channel])*(255-alpha))/255.0));
        expected[3] = 255;
        require(frame.image.pixels[0] == expected, "all byte alpha values round straight source-over RGB");
        require(frame.shaded_samples == (alpha ? 1U : 0U) &&
                frame.transparent_samples == (alpha ? 0U : 1U) &&
                frame.occluded_samples == 0 && frame.tested_samples == 1,
                "transparent or shaded counters account for each byte alpha sample");
        require(alpha ? frame.image.depth[0] == 0 : std::isinf(frame.image.depth[0]),
                "zero alpha preserves depth; every partial alpha writes depth");
    }
    const auto geometry = base(1,1,blue,1);
    const auto behind = odh::composite_sprites(geometry,{layer(solid,0,0,2)});
    const auto front = odh::composite_sprites(geometry,{layer(solid,0,0,0)});
    const auto tie = odh::composite_sprites(geometry,{layer(solid,0,0,1)});
    require(behind.image.pixels == geometry.pixels && behind.occluded_samples == 1 &&
            front.image.pixels[0] == red && front.shaded_samples == 1 &&
            tie.image.pixels == geometry.pixels && tie.occluded_samples == 1,
            "base geometry occludes farther sprites and retains ties");
    const auto second = sprite(1,1,green);
    const auto ties = odh::composite_sprites(backdrop,{layer(solid),layer(second)});
    require(ties.image.pixels[0] == red && ties.shaded_samples == 1 && ties.occluded_samples == 1,
            "sprite depth ties retain first submitted fragment");
    auto transparent = sprite(1,1,{255,255,0,0});
    const auto through = odh::composite_sprites(backdrop,
        {layer(transparent,0,0,-10),layer(second,0,0,5)});
    require(through.image.pixels[0] == green && through.image.depth[0] == 5 &&
            through.transparent_samples == 1 && through.shaded_samples == 1,
            "transparent front sprite never blocks a farther sprite");
    const auto transparent_behind = odh::composite_sprites(geometry,{layer(transparent,0,0,3)});
    require(transparent_behind.transparent_samples == 1 && transparent_behind.occluded_samples == 0,
            "transparent samples are discarded before depth test");
    auto partial = sprite(1,1,{255,0,0,128}); partial.flags = 7;
    const auto partially_occluding = odh::composite_sprites(backdrop,
        {layer(partial,0,0,0),layer(second,0,0,1)});
    require(partially_occluding.image.pixels[0] == Pixel{128,0,127,255} &&
            partially_occluding.image.depth[0] == 0 && partially_occluding.occluded_samples == 1,
            "partial alpha writes depth and blocks subsequent farther fragments");
    auto depths = sprite(3,1);
    depths.pixels = {red,green,blue}; depths.z = {255,153,0};
    const auto varying = odh::composite_sprites(base(3,1),{layer(depths,0,0,2,-2)});
    require(varying.image.depth == std::vector<float>{2,0,-3},
            "byte Z maps back, one delta, and 2.5 deltas with signed depth delta");
    const auto mirrored = odh::composite_sprites(base(3,1),{layer(depths,0,0,2,-2,true)});
    require(mirrored.image.pixels == std::vector<Pixel>{blue,green,red} &&
            mirrored.image.depth == std::vector<float>{-3,0,2},
            "horizontal flip mirrors both colors and byte Z");
    const auto flipped_geometry = odh::composite_sprites(base(3,1,green,1),
        {layer(depths,0,0,2,-2,true)});
    require(flipped_geometry.image.pixels == std::vector<Pixel>{blue,green,green} &&
            flipped_geometry.shaded_samples == 2 && flipped_geometry.occluded_samples == 1,
            "mirrored byte Z decides occlusion at mirrored pixel positions");
    auto quad = sprite(2,2); quad.pixels = {red,green,blue,{99,88,77,255}};
    const auto clipped = odh::composite_sprites(base(2,2),{layer(quad,-1,-1)});
    require(clipped.tested_samples == 1 && clipped.image.pixels[0] == quad.pixels[3],
            "negative top-left clips source coordinates correctly");
    const auto clipped_flip = odh::composite_sprites(base(2,2),{layer(quad,-1,-1,0,0,true)});
    require(clipped_flip.tested_samples == 1 && clipped_flip.image.pixels[0] == blue,
            "clipping and horizontal mirroring use original source width");
    const auto lower_edge = odh::composite_sprites(base(2,2),{layer(quad,1,1)});
    require(lower_edge.tested_samples == 1 && lower_edge.image.pixels[3] == red,
            "right and bottom edges clip to one sample");
    for(const auto coordinate : {std::numeric_limits<std::int32_t>::min(),
                                std::numeric_limits<std::int32_t>::max(),-2,2}) {
        const auto outside_x = odh::composite_sprites(base(2,2),{layer(quad,coordinate,0)});
        const auto outside_y = odh::composite_sprites(base(2,2),{layer(quad,0,coordinate)});
        require(outside_x.tested_samples == 0 && outside_y.tested_samples == 0,
                "fully offscreen and integer-extreme placement costs zero samples");
    }
    const auto empty = sprite(0,2);
    const auto empty_height = sprite(2,0);
    odh::CompositorLimits zero_work; zero_work.samples = 0;
    require(odh::composite_sprites(backdrop,{layer(empty),layer(empty_height)},zero_work).tested_samples == 0,
            "zero-width and zero-height sprites are valid and consume no samples");
    rejects([&]{odh::composite_sprites(backdrop,{layer(transparent)},zero_work);});
    auto budgets = odh::CompositorLimits{};
    budgets.width = 2; budgets.height = 2; budgets.pixels = 8; budgets.layers = 2; budgets.samples = 8;
    require(odh::composite_sprites(base(2,2),{layer(quad),layer(quad)},budgets).tested_samples == 8,
            "exact dimensions, cumulative raw pixels, layers and samples budgets accepted");
    for(unsigned which = 0; which < 5; ++which) {
        auto limits = budgets;
        if(which == 0) limits.width = 1;
        if(which == 1) limits.height = 1;
        if(which == 2) limits.pixels = 7;
        if(which == 3) limits.layers = 1;
        if(which == 4) limits.samples = 7;
        rejects([&]{odh::composite_sprites(base(2,2),{layer(quad),layer(quad)},limits);});
    }
    auto single_pixel_budget = odh::CompositorLimits{}; single_pixel_budget.pixels = 1;
    rejects([&]{odh::composite_sprites(base(2,1),{},single_pixel_budget);});
    rejects([&]{odh::composite_sprites(backdrop,{layer(quad,100,100)},single_pixel_budget);});
    auto narrow = odh::CompositorLimits{}; narrow.width = 1;
    auto short_frame = odh::CompositorLimits{}; short_frame.height = 1;
    rejects([&]{odh::composite_sprites(backdrop,{layer(quad,100,100)},narrow);});
    rejects([&]{odh::composite_sprites(backdrop,{layer(quad,100,100)},short_frame);});
    rejects([&]{odh::composite_sprites(backdrop,{layer(quad,100,100),layer(quad,100,100)},
                                     odh::CompositorLimits{2,7,2,2,0});});
    rejects([&]{odh::composite_sprites(base(0,1),{});});
    rejects([&]{odh::composite_sprites(base(1,0),{});});
    for(unsigned which = 0; which < 4; ++which) {
        auto malformed = backdrop;
        if(which == 0) malformed.pixels.clear();
        if(which == 1) malformed.depth.clear();
        if(which == 2) malformed.pixels.push_back(red);
        if(which == 3) malformed.pixels[0][3] = 254;
        rejects([&]{odh::composite_sprites(malformed,{});});
    }
    for(const auto invalid : {std::numeric_limits<float>::quiet_NaN(),
                              -std::numeric_limits<float>::infinity()})
        rejects([&]{odh::composite_sprites(base(1,1,blue,invalid),{});});
    for(const auto flags : {0U,1U,2U,4U,5U,6U,8U,11U,0xffffffffU}) {
        auto malformed = solid; malformed.flags = flags;
        rejects([&]{odh::composite_sprites(backdrop,{layer(malformed,100,100)});});
    }
    for(unsigned which = 0; which < 5; ++which) {
        auto malformed = solid;
        if(which == 0) malformed.pixels.clear();
        if(which == 1) malformed.z.clear();
        if(which == 2) malformed.pixels.push_back(red);
        if(which == 3) malformed.z.push_back(0);
        if(which == 4) malformed.width = 0;
        rejects([&]{odh::composite_sprites(backdrop,{layer(malformed,100,100)});});
    }
    auto null_layer = layer(solid); null_layer.frame = nullptr;
    rejects([&]{odh::composite_sprites(backdrop,{null_layer});});
    for(const auto invalid : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()}) {
        rejects([&]{odh::composite_sprites(backdrop,{layer(solid,100,100,invalid,0)});});
        rejects([&]{odh::composite_sprites(backdrop,{layer(solid,100,100,0,invalid)});});
    }
    const auto maximum = std::numeric_limits<float>::max();
    rejects([&]{odh::composite_sprites(backdrop,{layer(solid,100,100,0,maximum)});});
    rejects([&]{odh::composite_sprites(backdrop,{layer(solid,100,100,-maximum,-maximum)});});
    rejects([&]{odh::composite_sprites(backdrop,{layer(empty,100,100,0,maximum)});});
    require(odh::composite_sprites(backdrop,{layer(solid,0,0,maximum,0)}).image.depth[0] == maximum,
            "largest representable finite depth remains accepted");
    auto malformed_late = solid; malformed_late.z.clear();
    const auto saved_base_pixels = backdrop.pixels, saved_sprite_pixels = solid.pixels;
    const auto saved_base_depth = backdrop.depth;
    const auto saved_z = solid.z;
    rejects([&]{odh::composite_sprites(backdrop,{layer(solid),layer(malformed_late,100,100)});});
    require(backdrop.pixels == saved_base_pixels && backdrop.depth == saved_base_depth &&
            solid.pixels == saved_sprite_pixels && solid.z == saved_z,
            "late offscreen rejection leaves base and sprites unchanged");
    solid.indices = {1,2,3}; solid.x = 123; solid.y = -123;
    require(odh::composite_sprites(backdrop,{layer(solid)}).image.pixels[0] == red,
            "index metadata and decoded offsets are ignored by explicit placement API");
    std::cout << checks << " compositor checks passed (synthetic fixtures only)\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr << "compositor test failed: " << e.what() << '\n';
    return 1;
} }
