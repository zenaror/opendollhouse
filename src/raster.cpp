#include "odh/raster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace odh {
namespace {
using Pixel = std::array<std::uint8_t,4>;

double edge(const RasterVertex& a, const RasterVertex& b, double x, double y) {
    return (double(b.position[0])-a.position[0])*(y-a.position[1])-
           (double(b.position[1])-a.position[1])*(x-a.position[0]);
}
bool top_left(const RasterVertex& a, const RasterVertex& b) {
    const double dx=double(b.position[0])-a.position[0];
    const double dy=double(b.position[1])-a.position[1];
    return dy<0 || (dy==0 && dx>0);
}
bool inside(double value, bool inclusive) {
    return value>0 || (value==0 && inclusive);
}
struct Bounds {
    std::uint32_t x0=0, y0=0, x1=0, y1=0;
    std::uint64_t samples=0;
};
Bounds bounds(const RasterTriangle& triangle, std::uint32_t width,
              std::uint32_t height) {
    const auto& v=triangle.vertices;
    if(edge(v[0],v[1],v[2].position[0],v[2].position[1])==0) return {};
    const double min_x=std::min({double(v[0].position[0]),double(v[1].position[0]),
                                 double(v[2].position[0])});
    const double max_x=std::max({double(v[0].position[0]),double(v[1].position[0]),
                                 double(v[2].position[0])});
    const double min_y=std::min({double(v[0].position[1]),double(v[1].position[1]),
                                 double(v[2].position[1])});
    const double max_y=std::max({double(v[0].position[1]),double(v[1].position[1]),
                                 double(v[2].position[1])});
    // Clip in double before converting potentially enormous screen coordinates.
    const double x0=std::max(0.0,std::ceil(min_x-0.5));
    const double y0=std::max(0.0,std::ceil(min_y-0.5));
    const double x1=std::min(double(width)-1,std::floor(max_x-0.5));
    const double y1=std::min(double(height)-1,std::floor(max_y-0.5));
    if(x0>x1 || y0>y1) return {};
    Bounds result{static_cast<std::uint32_t>(x0),static_cast<std::uint32_t>(y0),
                  static_cast<std::uint32_t>(x1),static_cast<std::uint32_t>(y1),0};
    result.samples=(std::uint64_t(result.x1)-result.x0+1)*
                   (std::uint64_t(result.y1)-result.y0+1);
    return result;
}
double fraction(double value) { return value-std::floor(value); }
std::uint32_t wrap(std::int64_t index, std::uint32_t extent) {
    const auto n=static_cast<std::int64_t>(extent);
    return static_cast<std::uint32_t>((index%n+n)%n);
}
std::array<double,3> sample(const BitmapImage& texture, double u, double v,
                           TextureFilter filter) {
    // Reduce before scaling, so finite UVs of any float magnitude are safe.
    const double x=fraction(u)*texture.width, y=fraction(v)*texture.height;
    const auto pixel=[&](std::int64_t px,std::int64_t py)->const Pixel& {
        return texture.pixels[std::size_t(wrap(py,texture.height))*texture.width+
                              wrap(px,texture.width)];
    };
    std::array<double,3> result{};
    if(filter==TextureFilter::nearest) {
        const auto& p=pixel(static_cast<std::int64_t>(std::floor(x)),
                            static_cast<std::int64_t>(std::floor(y)));
        for(unsigned c=0;c<3;++c)result[c]=p[c];
    } else {
        const double bx=x-0.5, by=y-0.5;
        const auto ix=static_cast<std::int64_t>(std::floor(bx));
        const auto iy=static_cast<std::int64_t>(std::floor(by));
        const double tx=bx-std::floor(bx), ty=by-std::floor(by);
        const auto& a=pixel(ix,iy);const auto& b=pixel(ix+1,iy);
        const auto& c=pixel(ix,iy+1);const auto& d=pixel(ix+1,iy+1);
        for(unsigned channel=0;channel<3;++channel)
            result[channel]=((1-tx)*a[channel]+tx*b[channel])*(1-ty)+
                             ((1-tx)*c[channel]+tx*d[channel])*ty;
    }
    return result;
}
double blend(const std::array<RasterVertex,3>& vertices,
             const std::array<double,3>& weights, unsigned component,
             bool normal=false, bool uv=false) {
    double value=0;
    for(unsigned i=0;i<3;++i)
        value+=weights[i]*(normal?vertices[i].normal[component]:
                           uv?vertices[i].texcoord[component]:vertices[i].position[component]);
    return value;
}
}

RasterFrame rasterize(std::uint32_t width, std::uint32_t height,
                      const std::vector<RasterTriangle>& triangles,
                      const std::vector<BitmapImage>& textures,
                      RasterOptions options, RasterLimits limits) {
    if(width==0 || height==0 || width>limits.width || height>limits.height)
        throw FormatError("raster dimensions exceed limits or are zero");
    const std::uint64_t pixels=std::uint64_t(width)*height;
    if(pixels>limits.pixels || pixels>std::vector<Pixel>().max_size() ||
       pixels>std::vector<float>().max_size())
        throw FormatError("raster pixel budget exceeded");
    if(options.record_owner_ids&&pixels>std::vector<std::uint16_t>().max_size())
        throw FormatError("raster owner token budget exceeded");
    if(triangles.size()>limits.triangles || textures.size()>limits.textures)
        throw FormatError("raster triangle or texture budget exceeded");
    if(options.clear[3]!=255 || (options.filter!=TextureFilter::nearest &&
                                options.filter!=TextureFilter::bilinear))
        throw FormatError("raster requires opaque clear color and supported filter");
    std::uint64_t texture_pixels=0;
    for(const auto& texture:textures) {
        const auto count=std::uint64_t(texture.width)*texture.height;
        if(texture.width==0 || texture.height==0 || count!=texture.pixels.size())
            throw FormatError("raster texture dimensions do not match pixels");
        if(count>limits.texture_pixels-texture_pixels)
            throw FormatError("raster texture pixel budget exceeded");
        texture_pixels+=count;
        for(const auto& p:texture.pixels)if(p[3]!=255)
            throw FormatError("raster texture must be opaque");
    }
    std::uint64_t tested_samples=0;
    for(const auto& triangle:triangles) {
        if(triangle.texture>=textures.size())throw FormatError("raster texture index out of range");
        for(const auto& vertex:triangle.vertices) {
            for(const auto x:vertex.position)if(!std::isfinite(x))
                throw FormatError("raster position must be finite");
            for(const auto x:vertex.texcoord)if(!std::isfinite(x))
                throw FormatError("raster UV must be finite");
            for(const auto x:vertex.normal)if(!std::isfinite(x))
                throw FormatError("raster normal must be finite");
            for(const auto x:vertex.color)if(!std::isfinite(x)||x<0||x>1)
                throw FormatError("raster vertex color outside finite unit range");
            if(options.perspective_correct&&vertex.position[2]<=0)
                throw FormatError("perspective raster depth must be positive");
        }
        const auto candidates=bounds(triangle,width,height).samples;
        if(candidates>limits.samples-tested_samples)
            throw FormatError("raster sample budget exceeded");
        tested_samples+=candidates;
    }
    RasterFrame frame{width,height,std::vector<Pixel>(static_cast<std::size_t>(pixels),options.clear),
                      std::vector<float>(static_cast<std::size_t>(pixels),
                                         std::numeric_limits<float>::infinity()),tested_samples,0,{}};
    if(options.record_owner_ids)frame.owner_ids.assign(static_cast<std::size_t>(pixels),0);
    for(const auto& triangle:triangles) {
        const auto box=bounds(triangle,width,height);
        if(box.samples==0)continue;
        auto vertices=triangle.vertices;
        if(edge(vertices[0],vertices[1],vertices[2].position[0],vertices[2].position[1])<0)
            std::swap(vertices[1],vertices[2]);
        const std::array<bool,3> inclusive{top_left(vertices[1],vertices[2]),
            top_left(vertices[2],vertices[0]),top_left(vertices[0],vertices[1])};
        // The widened counters also handle a requested UINT32_MAX dimension.
        for(std::uint64_t y=box.y0;y<=box.y1;++y)
            for(std::uint64_t x=box.x0;x<=box.x1;++x) {
                const double px=double(x)+0.5,py=double(y)+0.5;
                std::array<double,3> weights{edge(vertices[1],vertices[2],px,py),
                    edge(vertices[2],vertices[0],px,py),edge(vertices[0],vertices[1],px,py)};
                if(!inside(weights[0],inclusive[0]) || !inside(weights[1],inclusive[1]) ||
                   !inside(weights[2],inclusive[2]))continue;
                const double total=weights[0]+weights[1]+weights[2];
                if(total==0)continue;
                for(auto& w:weights)w/=total;
                auto varying_weights=weights;
                double z=blend(vertices,weights,2);
                if(options.perspective_correct){
                    double reciprocal_depth=0;
                    for(unsigned i=0;i<3;++i)
                        reciprocal_depth+=weights[i]/vertices[i].position[2];
                    if(!std::isfinite(reciprocal_depth)||reciprocal_depth<=0)
                        throw FormatError("perspective raster reciprocal depth is invalid");
                    z=1.0/reciprocal_depth;
                    for(unsigned i=0;i<3;++i)
                        varying_weights[i]=(weights[i]/vertices[i].position[2])/reciprocal_depth;
                }
                // Clamp tiny rounding drift to the finite convex vertex range.
                const float depth=static_cast<float>(std::clamp(z,
                    double(std::min({vertices[0].position[2],vertices[1].position[2],vertices[2].position[2]})),
                    double(std::max({vertices[0].position[2],vertices[1].position[2],vertices[2].position[2]}))));
                const auto index=static_cast<std::size_t>(y*width+x);
                if(!(depth<frame.depth[index]))continue;
                const double u=blend(vertices,varying_weights,0,false,true);
                const double v=blend(vertices,varying_weights,1,false,true);
                if(!std::isfinite(u) || !std::isfinite(v))
                    throw FormatError("raster interpolated UV overflow");
                auto color=sample(textures[triangle.texture],u,v,options.filter);
                if(options.vertex_colors)for(unsigned c=0;c<3;++c){
                    double modulation=0;
                    for(unsigned i=0;i<3;++i)modulation+=varying_weights[i]*vertices[i].color[c];
                    color[c]*=std::clamp(modulation,0.0,1.0);
                }
                double shade=1;
                if(options.lighting) {
                    const double nx=blend(vertices,varying_weights,0,true);
                    const double ny=blend(vertices,varying_weights,1,true);
                    const double nz=blend(vertices,varying_weights,2,true);
                    const double length=std::sqrt(nx*nx+ny*ny+nz*nz);
                    const double up=length==0?1:std::clamp(ny/length,-1.0,1.0);
                    shade=options.directional_up_light?(up+1)*0.5:std::sqrt((up+1)*0.5)*0.5+0.5;
                }
                for(unsigned channel=0;channel<3;++channel)
                    frame.pixels[index][channel]=static_cast<std::uint8_t>(
                        std::round(std::clamp(color[channel]*shade,0.0,255.0)));
                frame.pixels[index][3]=255;frame.depth[index]=depth;
                if(options.record_owner_ids)frame.owner_ids[index]=triangle.owner_id;
                ++frame.shaded_samples;
            }
    }
    return frame;
}
} // namespace odh
