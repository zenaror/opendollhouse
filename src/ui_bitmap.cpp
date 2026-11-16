#include "odh/ui_bitmap.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace odh {

namespace {
// floor((offset + n) * source_extent / destination_extent), advanced without
// division. Extents are positive int32 values, so adding two remainders fits
// uint32 even at their limits. The caller checks the initial product.
struct BitmapAxis {
    std::uint32_t index, remainder, step, remainder_step, divisor;
    BitmapAxis(std::uint32_t product, std::uint32_t source_extent,
            std::uint32_t destination_extent, std::uint32_t origin)
        : index(origin + product / destination_extent), remainder(product % destination_extent),
          step(source_extent / destination_extent), remainder_step(source_extent % destination_extent),
          divisor(destination_extent) {}
    void advance() {
        index += step;
        remainder += remainder_step;
        if (remainder >= divisor) { remainder -= divisor; ++index; }
    }
};

bool composite_bitmap_pixel(unsigned char* output,
        const std::array<std::uint8_t,4>& pixel, bool rgba,
        const std::optional<std::array<std::uint8_t,3>>& color_key) {
    if(color_key&&pixel[0]==(*color_key)[0]&&pixel[1]==(*color_key)[1]&&pixel[2]==(*color_key)[2])return false;
    const unsigned alpha=rgba?pixel[3]:255;
    if(!alpha)return false;
    if(alpha==255) {
        std::memcpy(output,pixel.data(),3);
        output[3]=255;
    } else {
        for(unsigned channel=0;channel<3;++channel)
            output[channel]=static_cast<std::uint8_t>((unsigned(pixel[channel])*alpha+
                unsigned(output[channel])*(255-alpha)+127)/255);
        output[3]=255;
    }
    return true;
}
}

std::size_t blit_color_keyed_bitmap(RasterFrame& destination,
    const BitmapImage& source, std::int32_t x, std::int32_t y,
    std::array<std::uint8_t, 3> color_key) {
    if(source.width>INT32_MAX||source.height>INT32_MAX)throw FormatError("UI bitmap source dimensions invalid");
    return blit_color_keyed_bitmap(destination,source,{x,y,static_cast<std::int32_t>(source.width),
        static_cast<std::int32_t>(source.height)},std::nullopt,color_key);
}

static std::size_t blit_bitmap(UiBitmapSurface destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> source_crop,std::optional<std::array<std::uint8_t,3>> color_key) {
    const auto destination_pixels = static_cast<std::uint64_t>(destination.width) * destination.height;
    const auto source_pixels = static_cast<std::uint64_t>(source.width) * source.height;
    if (!destination.width || !destination.height ||
        destination_pixels > std::numeric_limits<std::size_t>::max() ||
        !destination.rgba || destination.pixel_count != static_cast<std::size_t>(destination_pixels))
        throw FormatError("UI bitmap destination frame is malformed");
    if (!source.width || !source.height ||
        source_pixels > std::numeric_limits<std::size_t>::max() ||
        source.pixels.size() != static_cast<std::size_t>(source_pixels))
        throw FormatError("UI bitmap source image is malformed");

    if(source.width>INT32_MAX||source.height>INT32_MAX)throw FormatError("UI bitmap source dimensions invalid");
    const auto crop=source_crop.value_or(UiBitmapRect{0,0,static_cast<std::int32_t>(source.width),
        static_cast<std::int32_t>(source.height)});
    if(crop.x<0||crop.y<0||crop.width<=0||crop.height<=0||std::int64_t(crop.x)+crop.width>source.width||
       std::int64_t(crop.y)+crop.height>source.height)throw FormatError("UI bitmap crop outside source image");
    if(box.width<=0||box.height<=0)return 0;
    const auto right=std::min<std::int64_t>(destination.width,std::int64_t(box.x)+box.width);
    const auto bottom=std::min<std::int64_t>(destination.height,std::int64_t(box.y)+box.height);
    const auto left=std::max(0,box.x),top=std::max(0,box.y);
    if(left>=right||top>=bottom)return 0;
    std::size_t copied=0;
    const bool rgba=source.bit_count==32;
    const auto initial_x=std::uint64_t(std::int64_t(left)-box.x)*static_cast<std::uint32_t>(crop.width);
    const auto initial_y=std::uint64_t(std::int64_t(top)-box.y)*static_cast<std::uint32_t>(crop.height);
    if(initial_x<=UINT32_MAX&&initial_y<=UINT32_MAX) {
        const BitmapAxis first_x(static_cast<std::uint32_t>(initial_x),static_cast<std::uint32_t>(crop.width),
            static_cast<std::uint32_t>(box.width),static_cast<std::uint32_t>(crop.x));
        BitmapAxis sy(static_cast<std::uint32_t>(initial_y),static_cast<std::uint32_t>(crop.height),
            static_cast<std::uint32_t>(box.height),static_cast<std::uint32_t>(crop.y));
        for(std::int64_t dy=top;dy<bottom;++dy,sy.advance()) {
            const auto* source_row=source.pixels.data()+std::size_t(sy.index)*source.width;
            auto* output=destination.pixel(std::size_t(dy)*destination.width+static_cast<std::size_t>(left));
            const auto* end=output+4*static_cast<std::size_t>(right-left);
            auto sx=first_x;
            for(;output!=end;output+=4,sx.advance())
                copied+=composite_bitmap_pixel(output,source_row[sx.index],rgba,color_key);
        }
        return copied;
    }
    // Extreme clipped coordinates keep the original wide mapping. Ordinary
    // controls use the exact incremental path above, including scaled crops.
    for(std::int64_t destination_y=top;destination_y<bottom;++destination_y){
        const auto source_y=crop.y+(destination_y-box.y)*crop.height/box.height;
        for(std::int64_t destination_x=left;destination_x<right;++destination_x){
            const auto source_x=crop.x+(destination_x-box.x)*crop.width/box.width;
            const auto& pixel=source.pixels[static_cast<std::size_t>(source_y)*source.width+static_cast<std::size_t>(source_x)];
            // BMP remains opaque/color-keyed.32-bit UI TGA retains its actual
            // alpha, including antialiased curved edges, over the scene.
            auto* output = destination.pixel(static_cast<std::size_t>(destination_y) * destination.width +
                static_cast<std::size_t>(destination_x));
            copied+=composite_bitmap_pixel(output,pixel,rgba,color_key);
        }
    }
    return copied;
}

std::size_t blit_color_keyed_bitmap(RasterFrame& destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> crop,std::array<std::uint8_t,3> key){
    return blit_bitmap(UiBitmapSurface::from(destination),source,box,crop,key);
}
std::size_t blit_color_keyed_bitmap(UiBitmapSurface destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> crop,std::array<std::uint8_t,3> key){
    return blit_bitmap(destination,source,box,crop,key);
}
std::size_t blit_rgba_bitmap(RasterFrame& destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> crop){
    if(source.bit_count!=32)throw FormatError("RGBA UI bitmap requires32-bit pixels");
    return blit_bitmap(UiBitmapSurface::from(destination),source,box,crop,std::nullopt);
}

} // namespace odh
