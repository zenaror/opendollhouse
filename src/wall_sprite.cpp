#include "odh/wall_sprite.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace odh {
namespace {
struct Bounds { std::int64_t x0, y0, x1, y1; std::uint64_t samples; };
Bounds bounds(const WallSpriteLayer& l, const RasterFrame& b) {
    const auto x0 = std::max(std::int64_t{0}, std::int64_t{l.x});
    const auto y0 = std::max(std::int64_t{0}, std::int64_t{l.y});
    const auto x1 = std::min(std::int64_t{b.width}, std::int64_t{l.x}+l.width);
    const auto y1 = std::min(std::int64_t{b.height}, std::int64_t{l.y}+l.height);
    if(x0 >= x1 || y0 >= y1) return {0,0,0,0,0};
    return {x0,y0,x1,y1,std::uint64_t(x1-x0)*std::uint64_t(y1-y0)};
}
void charge(std::uint64_t amount, std::uint64_t& used, std::uint64_t limit) {
    if(amount > limit-used) throw FormatError("wall sprite budget exceeded");
    used += amount;
}
std::uint64_t extent(std::uint32_t w, std::uint32_t h, WallSpriteLimits limits) {
    if(!w || !h || w > limits.width || h > limits.height)
        throw FormatError("wall sprite dimensions invalid");
    return std::uint64_t{w}*h;
}
void validate(const SprFrame& f, WallSpriteLimits limits) {
    if(extent(f.width,f.height,limits) != f.pixels.size())
        throw FormatError("wall sprite RGBA array invalid");
    for(const auto& p : f.pixels) if(p[3] != 0 && p[3] != 255)
        throw FormatError("wall sprite requires binary alpha");
}
double depth(const WallSpriteLayer& l, std::uint8_t z) {
    return double(l.back_depth)+((1-double(z)/255.0)/0.4)*double(l.front_depth_delta);
}
bool representable(double v) {
    const auto max = double(std::numeric_limits<float>::max());
    return std::isfinite(v) && v >= -max && v <= max;
}
// Equivalent to floor((2*d+1)*s/(2*n)), without overflowing uint64 even
// when source/destination extents approach UINT32_MAX.
std::uint64_t coordinate(std::uint64_t d, std::uint32_t s, std::uint32_t n) {
    const auto product = d*s;
    return std::min(std::uint64_t{s}-1, product/n+(2*(product%n)+s)/(2*std::uint64_t{n}));
}
std::size_t sample(std::uint64_t x, std::uint64_t y, std::uint32_t w,
                   std::uint32_t h, const WallSpriteLayer& l) {
    return static_cast<std::size_t>(coordinate(y,h,l.height)*w+coordinate(x,w,l.width));
}
} // namespace

CompositeFrame composite_wall_sprites(const RasterFrame& base,
    const std::vector<WallSpriteLayer>& layers, WallSpriteLimits limits) {
    const auto count = extent(base.width,base.height,limits);
    if(count > limits.pixels || count != base.pixels.size() || count != base.depth.size())
        throw FormatError("wall sprite base arrays or budget invalid");
    for(const auto& p : base.pixels) if(p[3] != 255)
        throw FormatError("wall sprite base must be opaque");
    for(const auto z : base.depth)
        if(!std::isfinite(z) && z != std::numeric_limits<float>::infinity())
            throw FormatError("wall sprite base depth invalid");
    if(layers.size() > limits.layers) throw FormatError("wall sprite layer budget exceeded");
    std::uint64_t sources = 0, destinations = 0, candidates = 0;
    for(const auto& l : layers) {
        if(!l.pattern || !l.depth) throw FormatError("wall sprite null source");
        const auto& m = l.mask ? *l.mask : *l.pattern;
        validate(*l.pattern,limits);
        validate(m,limits);
        const auto dc = extent(l.depth->width,l.depth->height,limits);
        if(dc != l.depth->pixels.size()) throw FormatError("wall sprite depth array invalid");
        charge(std::uint64_t{l.pattern->width}*l.pattern->height,sources,limits.source_pixel_visits);
        charge(std::uint64_t{m.width}*m.height,sources,limits.source_pixel_visits);
        charge(dc,sources,limits.source_pixel_visits);
        charge(extent(l.width,l.height,limits),destinations,limits.destination_pixels);
        if(!std::isfinite(l.back_depth) || !std::isfinite(l.front_depth_delta) ||
           !representable(depth(l,0)) || !representable(depth(l,255)))
            throw FormatError("wall sprite depth range invalid");
        charge(bounds(l,base).samples,candidates,limits.samples);
    }
    CompositeFrame result{base,candidates,0,0,0,{}};
    for(const auto& l : layers) {
        const auto& p = *l.pattern;
        const auto& m = l.mask ? *l.mask : p;
        const auto& d = *l.depth;
        const auto b = bounds(l,base);
        for(auto y=b.y0; y<b.y1; ++y) for(auto x=b.x0; x<b.x1; ++x) {
            const auto sx = std::uint64_t(x-std::int64_t{l.x});
            const auto sy = std::uint64_t(y-std::int64_t{l.y});
            if(m.pixels[sample(sx,sy,m.width,m.height,l)][3] == 0) {
                ++result.transparent_samples; continue;
            }
            const auto target = static_cast<std::size_t>(std::uint64_t(y)*base.width+std::uint64_t(x));
            const auto z = static_cast<float>(depth(l,d.pixels[sample(sx,sy,d.width,d.height,l)]));
            if(!(z < result.image.depth[target])) { ++result.occluded_samples; continue; }
            auto color = p.pixels[sample(sx,sy,p.width,p.height,l)];
            color[3] = 255;
            result.image.pixels[target] = color;
            result.image.depth[target] = z;
            ++result.shaded_samples;
        }
    }
    return result;
}

WallDepth wall_sprite_depth(std::uint8_t zoom, std::uint8_t orientation) {
    if(zoom < 1 || zoom > 3 || orientation > 3)
        throw FormatError("wall depth zoom or orientation invalid");
    struct Config { std::uint32_t w,h; float start,dx,dy; };
    static constexpr Config configs[] = {
        {64,271,74,1,.5f},{64,271,135,-1,.5f},{128,240,89.5f,0,.5f},{16,232,45,0,.5f},
        {32,135,74,2,1},{32,135,135,-2,1},{64,120,89.5f,0,1},{8,116,45,0,1},
        {16,67,74,4,2},{16,67,135,-4,2},{32,60,89.5f,0,2},{4,58,45,0,2}
    };
    const auto c = configs[(3-zoom)*4+orientation];
    WallDepth result{c.w,c.h,std::vector<std::uint8_t>(std::size_t(c.w)*c.h)};
    auto yi = c.start;
    for(std::uint32_t y=0; y<c.h; ++y) {
        auto xi = yi;
        for(std::uint32_t x=0; x<c.w; ++x) {
            const auto value = std::min(255.f,xi);
            const auto low = static_cast<unsigned>(value);
            const auto fraction = value-static_cast<float>(low);
            const auto rounded = low+(fraction > .5f || (fraction == .5f && (low&1U)) ? 1U : 0U);
            result.pixels[std::size_t(y)*c.w+x] = static_cast<std::uint8_t>(rounded);
            xi += c.dx;
        }
        yi += c.dy;
    }
    return result;
}
} // namespace odh
