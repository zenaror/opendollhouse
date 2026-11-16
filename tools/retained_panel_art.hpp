// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/ui_bitmap.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace odh_host {

// Compose the immutable control-panel sprites once per changed tile. The lot is
// drawn again each frame, so opaque runs are copied and translucent edges must
// still blend over the current scene, in the original drawing order.
class RetainedPanelArt {
    using Rect = odh::UiBitmapRect;
    using Pixel = std::array<std::uint8_t,4>;
    struct Sprite {
        const odh::BitmapImage* image;
        Rect box, crop;
        static bool equal(const Rect& a, const Rect& b) {
            return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;
        }
        bool operator==(const Sprite& other) const {
            return image==other.image && equal(box,other.box) && equal(crop,other.crop);
        }
    };
    struct Tile {
        struct Run { std::uint8_t x,y; std::uint16_t length; };
        struct Edge { std::uint8_t x,y; Pixel color; };
        Rect box{};
        std::vector<Sprite> sprites;
        std::vector<Pixel> pixels;
        std::vector<Run> runs;
        std::vector<Edge> edges;
        static void blend(unsigned char* out,const Pixel& p) {
            for(int c=0;c<3;++c) out[c]=std::uint8_t((p[c]*p[3]+out[c]*(255-p[3])+127)/255);
            out[3]=255;
        }
        void rebuild() {
            pixels.assign(box.width*box.height,Pixel{});
            edges.clear();
            runs.clear();
            for(const auto& sprite:sprites) {
                const auto& image=*sprite.image;
                const auto& dest=sprite.box;
                const auto& src=sprite.crop;
                const int x0=std::max(box.x,dest.x),x1=std::min(box.x+box.width,dest.x+dest.width);
                const int y0=std::max(box.y,dest.y),y1=std::min(box.y+box.height,dest.y+dest.height);
                std::array<unsigned,64> columns;
                for(int x=x0;x<x1;++x)
                    columns[x-x0]=unsigned(src.x+(std::int64_t(x)-dest.x)*src.width/dest.width);
                for(int y=y0;y<y1;++y) {
                    const size_t row=size_t(src.y+(std::int64_t(y)-dest.y)*src.height/dest.height)*image.width;
                    for(int x=x0;x<x1;++x) {
                        auto p=image.pixels[row+columns[x-x0]];
                        if(p[0]==255 && p[1]==0 && p[2]==255) continue;
                        if(image.bit_count!=32)p[3]=255;
                        if(!p[3])continue;
                        const auto i=std::uint16_t((y-box.y)*box.width+x-box.x);
                        if(p[3]==255)pixels[i]=p;
                        else if(pixels[i][3])blend(pixels[i].data(),p);
                        else edges.push_back({std::uint8_t(x-box.x),std::uint8_t(y-box.y),p});
                    }
                }
            }
            edges.erase(std::remove_if(edges.begin(),edges.end(),
                [&](const Edge& edge){ return pixels[edge.y*box.width+edge.x][3]!=0; }),edges.end());
            for(int y=0;y<box.height;++y) {
                const int row=y*box.width;
                for(int x=0;x<box.width;) {
                    if(!pixels[row+x][3]){++x;continue;}
                    const int start=x++;
                    while(x<box.width && pixels[row+x][3])++x;
                    runs.push_back({std::uint8_t(start),std::uint8_t(y),std::uint16_t(x-start)});
                }
            }
        }
        void draw(odh::UiBitmapSurface frame) const {
            auto* base=frame.pixel(size_t(box.y)*frame.width+box.x);
            for(const auto& run:runs) {
                auto* dest=base+4*(size_t(run.y)*frame.width+run.x);
                const auto* src=pixels.data()+run.y*box.width+run.x;
                std::memcpy(dest,src,size_t(run.length)*4);
            }
            for(const auto& edge:edges)
                blend(base+4*(size_t(edge.y)*frame.width+edge.x),edge.color);
        }
    };
    std::vector<Sprite> current,previous,intersecting;
    std::vector<Tile> tiles;
    Rect area{};
public:
#if defined(__vita__) || defined(__SWITCH__)
    static constexpr std::uint32_t max_width=800;
#else
    static constexpr std::uint32_t max_width=3840;
#endif
    void begin(){current.clear();}
    void add(const odh::BitmapImage& image,Rect box,Rect crop) {
        if(box.width>0 && box.height>0)current.push_back({&image,box,crop});
    }
    void draw(odh::UiBitmapSurface frame,Rect bounds) {
        const bool resize=!Sprite::equal(bounds,area);
        if(resize) {
            area=bounds;
            tiles.clear();
            for(int y=0;y<area.height;y+=32)for(int x=0;x<area.width;x+=64) {
                tiles.emplace_back();
                tiles.back().box={area.x+x,area.y+y,std::min(64,area.width-x),std::min(32,area.height-y)};
            }
        }
        if(resize || current!=previous) {
            // The art owner is immutable. Revalidate any new command sequence
            // before sampling it, including the original source crop checks.
            for(const auto& sprite:current) {
                const auto& i=*sprite.image; const auto& r=sprite.crop;
                if(!i.width || !i.height || i.width>INT32_MAX || i.height>INT32_MAX ||
                   std::uint64_t(i.width)*i.height!=i.pixels.size())
                    throw odh::FormatError("UI bitmap source image is malformed");
                if(r.x<0 || r.y<0 || r.width<=0 || r.height<=0 ||
                   std::int64_t(r.x)+r.width>i.width || std::int64_t(r.y)+r.height>i.height)
                    throw odh::FormatError("UI bitmap crop outside source image");
            }
            for(auto& tile:tiles) {
                intersecting.clear();
                for(const auto& sprite:current) {
                    const auto& a=tile.box; const auto& b=sprite.box;
                    if(a.x<b.x+b.width && b.x<a.x+a.width && a.y<b.y+b.height && b.y<a.y+a.height)
                        intersecting.push_back(sprite);
                }
                if(tile.sprites!=intersecting) {
                    tile.sprites=intersecting;
                    tile.rebuild();
                }
            }
            previous=current;
        }
        replay(frame);
    }
    void replay(odh::UiBitmapSurface frame) const {
        if(!frame.rgba || frame.pixel_count!=std::uint64_t(frame.width)*frame.height ||
           area.x<0 || area.y<0 || std::int64_t(area.x)+area.width>frame.width ||
           std::int64_t(area.y)+area.height>frame.height)
            throw odh::FormatError("retained control panel outside destination");
        for(const auto& tile:tiles)tile.draw(frame);
    }
};
}
