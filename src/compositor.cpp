#include "odh/compositor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace odh {
namespace {
struct Bounds {
    std::int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; // Exclusive upper bounds.
    std::uint64_t samples = 0;
};

Bounds bounds(const SpriteLayer& layer, const RasterFrame& base) {
    const auto& sprite = *layer.frame;
    const auto x0 = std::max(std::int64_t{0}, std::int64_t{layer.x});
    const auto y0 = std::max(std::int64_t{0}, std::int64_t{layer.y});
    const auto x1 = std::min(std::int64_t{base.width},
                             std::int64_t{layer.x} + sprite.width);
    const auto y1 = std::min(std::int64_t{base.height},
                             std::int64_t{layer.y} + sprite.height);
    if(x0 >= x1 || y0 >= y1) return {};
    return {x0, y0, x1, y1, static_cast<std::uint64_t>(x1-x0)*
                            static_cast<std::uint64_t>(y1-y0)};
}

double depth(const SpriteLayer& layer, std::uint8_t z) {
    return double(layer.back_depth) + ((1-double(z)/255.0)/0.4)*
                                      double(layer.front_depth_delta);
}

bool representable(double value) {
    const auto maximum = double(std::numeric_limits<float>::max());
    return std::isfinite(value) && value >= -maximum && value <= maximum;
}
} // namespace

RasterFrame merge_raster(const RasterFrame& base, const RasterFrame& layer,
                         RasterLimits limits) {
    if(base.width != layer.width || base.height != layer.height)
        throw FormatError("raster merge dimensions must match");
    std::uint64_t tested = 0, shaded = 0;
    for(const auto* frame : {&base, &layer}) {
        if(frame->width == 0 || frame->height == 0 || frame->width > limits.width ||
           frame->height > limits.height)
            throw FormatError("raster merge dimensions exceed limits or are zero");
        const auto count = std::uint64_t{frame->width}*frame->height;
        if(count > limits.pixels || count != frame->pixels.size() || count != frame->depth.size() ||
           (!frame->owner_ids.empty()&&count!=frame->owner_ids.size()))
            throw FormatError("raster merge pixel budget or arrays invalid");
        if(frame->shaded_samples > frame->tested_samples ||
           frame->tested_samples > limits.samples-tested)
            throw FormatError("raster merge sample counters or cumulative budget invalid");
        tested += frame->tested_samples;
        // Each shaded count is bounded by its tested count, whose sum fits.
        shaded += frame->shaded_samples;
        for(const auto& pixel : frame->pixels) if(pixel[3] != 255)
            throw FormatError("raster merge frames must be opaque");
        for(const auto value : frame->depth)
            if(!std::isfinite(value) && value != std::numeric_limits<float>::infinity())
                throw FormatError("raster merge depth must be finite or positive infinity");
    }
    RasterFrame result = base;
    if(result.owner_ids.empty()&&!layer.owner_ids.empty())
        result.owner_ids.assign(result.pixels.size(),0);
    result.tested_samples = tested;
    result.shaded_samples = shaded;
    for(std::size_t i = 0; i < result.pixels.size(); ++i) {
        if(layer.depth[i] < result.depth[i]) {
            result.pixels[i] = layer.pixels[i];
            result.depth[i] = layer.depth[i];
            if(!result.owner_ids.empty())
                result.owner_ids[i]=layer.owner_ids.empty()?0:layer.owner_ids[i];
        }
    }
    return result;
}

CompositeFrame composite_sprites(const RasterFrame& base,
                                const std::vector<SpriteLayer>& layers,
                                CompositorLimits limits, bool record_owners, bool record_sprite_rgba) {
    if(base.width == 0 || base.height == 0 || base.width > limits.width ||
       base.height > limits.height)
        throw FormatError("compositor base dimensions exceed limits or are zero");
    const auto count = std::uint64_t{base.width}*base.height;
    if(count > limits.pixels || count != base.pixels.size() || count != base.depth.size() ||
       (!base.owner_ids.empty()&&count!=base.owner_ids.size()))
        throw FormatError("compositor base pixel budget or arrays invalid");
    for(const auto& pixel : base.pixels) if(pixel[3] != 255)
        throw FormatError("compositor base must be opaque");
    for(const auto value : base.depth)
        if(!std::isfinite(value) && value != std::numeric_limits<float>::infinity())
            throw FormatError("compositor base depth must be finite or positive infinity");
    if(layers.size() > limits.layers)
        throw FormatError("compositor layer budget exceeded");

    std::uint64_t tested_samples = 0, sprite_pixels = 0;
    for(const auto& layer : layers) {
        if(!layer.frame) throw FormatError("compositor sprite frame is null");
        for(const auto value:layer.linear_light)if(!std::isfinite(value)||value<0||value>1)
            throw FormatError("compositor linear light outside [0,1]");
        const auto& sprite = *layer.frame;
        if((sprite.flags & 3U) != 3U || (sprite.flags & ~7U) != 0)
            throw FormatError("compositor requires pixel and Z flags without unknown flags");
        const auto pixels = std::uint64_t{sprite.width}*sprite.height;
        if(sprite.width > limits.width || sprite.height > limits.height ||
           pixels > limits.pixels-sprite_pixels ||
           pixels != sprite.pixels.size() || pixels != sprite.z.size())
            throw FormatError("compositor sprite dimensions, pixel budget or arrays invalid");
        sprite_pixels += pixels;
        if(!std::isfinite(layer.back_depth) || !std::isfinite(layer.front_depth_delta) ||
           !representable(depth(layer, 0)) || !representable(depth(layer, 255)))
            throw FormatError("compositor sprite depth range is not finite float");
        const auto candidates = bounds(layer, base).samples;
        if(candidates > limits.samples-tested_samples)
            throw FormatError("compositor sample budget exceeded");
        tested_samples += candidates;
    }

    CompositeFrame result{base, tested_samples, 0, 0, 0, {}};
    if(record_owners)result.owner_ids.assign(static_cast<std::size_t>(count),0);
    if(record_sprite_rgba)result.sprite_rgba.assign(static_cast<std::size_t>(count),{0,0,0,0});
    for(const auto& layer : layers) {
        const auto& sprite = *layer.frame;
        const auto box = bounds(layer, base);
        std::array<double,3> light;
        for(unsigned c=0;c<3;++c)light[c]=std::pow(double(layer.linear_light[c]),1.0/2.2);
        for(auto y = box.y0; y < box.y1; ++y)
            for(auto x = box.x0; x < box.x1; ++x) {
                auto sx = static_cast<std::uint64_t>(x-std::int64_t{layer.x});
                const auto sy = static_cast<std::uint64_t>(y-std::int64_t{layer.y});
                if(layer.flip) sx = std::uint64_t{sprite.width}-1-sx;
                const auto source = static_cast<std::size_t>(sy*sprite.width+sx);
                const auto& pixel = sprite.pixels[source];
                if(pixel[3] == 0) { ++result.transparent_samples; continue; }
                const auto destination = static_cast<std::size_t>(
                    static_cast<std::uint64_t>(y)*base.width+static_cast<std::uint64_t>(x));
                const auto z = static_cast<float>(depth(layer, sprite.z[source]));
                if(!(z < result.image.depth[destination])) {
                    ++result.occluded_samples;
                    continue;
                }
                auto& output = result.image.pixels[destination];
                const auto alpha = std::uint32_t{pixel[3]};
                const auto gray=0.2989*pixel[0]+0.5870*pixel[1]+0.1140*pixel[2];
                for(unsigned channel = 0; channel < 3; ++channel){
                    if(layer.linear_light[channel]==1&&!layer.grayscale&&!layer.invert)output[channel] = static_cast<std::uint8_t>(
                        (std::uint32_t{pixel[channel]}*alpha +
                         std::uint32_t{output[channel]}*(255-alpha) + 127)/255);
                    else {
                        const auto lit=layer.invert?255.0-double(pixel[channel]):layer.grayscale?gray:double(pixel[channel])*light[channel];
                        output[channel]=static_cast<std::uint8_t>(std::clamp(std::floor(
                            (lit*alpha+double(output[channel])*(255-alpha))/255.0+0.5),0.0,255.0));
                    }
                }
                if(record_sprite_rgba){
                    auto& rgba=result.sprite_rgba[destination];
                    const auto old_alpha=std::uint32_t{rgba[3]};
                    const auto combined=alpha*255+old_alpha*(255-alpha);
                    for(unsigned channel=0;channel<3;++channel){
                        const auto lit=layer.invert?255.0-double(pixel[channel]):layer.grayscale?gray:
                            double(pixel[channel])*light[channel];
                        const auto source=static_cast<std::uint32_t>(std::clamp(std::floor(lit+0.5),0.0,255.0));
                        rgba[channel]=static_cast<std::uint8_t>((source*alpha*255+
                            std::uint32_t{rgba[channel]}*old_alpha*(255-alpha)+combined/2)/combined);
                    }
                    rgba[3]=static_cast<std::uint8_t>((combined+127)/255);
                }
                output[3] = 255;
                result.image.depth[destination] = z;
                if(!result.image.owner_ids.empty())result.image.owner_ids[destination]=0;
                if(record_owners)result.owner_ids[destination]=layer.owner_id;
                ++result.shaded_samples;
            }
    }
    return result;
}
} // namespace odh
