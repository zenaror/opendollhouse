#include "odh/ui_bitmap.hpp"

#include <algorithm>
#include <limits>

namespace odh {

std::size_t blit_color_keyed_bitmap(RasterFrame& destination,
    const BitmapImage& source, std::int32_t x, std::int32_t y,
    std::array<std::uint8_t, 3> color_key) {
    if(source.width>INT32_MAX||source.height>INT32_MAX)throw FormatError("UI bitmap source dimensions invalid");
    return blit_color_keyed_bitmap(destination,source,{x,y,static_cast<std::int32_t>(source.width),
        static_cast<std::int32_t>(source.height)},std::nullopt,color_key);
}

static std::size_t blit_bitmap(RasterFrame& destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> source_crop,std::optional<std::array<std::uint8_t,3>> color_key) {
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

    if(source.width>INT32_MAX||source.height>INT32_MAX)throw FormatError("UI bitmap source dimensions invalid");
    const auto crop=source_crop.value_or(UiBitmapRect{0,0,static_cast<std::int32_t>(source.width),
        static_cast<std::int32_t>(source.height)});
    if(crop.x<0||crop.y<0||crop.width<=0||crop.height<=0||std::int64_t(crop.x)+crop.width>source.width||
       std::int64_t(crop.y)+crop.height>source.height)throw FormatError("UI bitmap crop outside source image");
    if(box.width<=0||box.height<=0)return 0;
    const auto right=std::min<std::int64_t>(destination.width,std::int64_t(box.x)+box.width);
    const auto bottom=std::min<std::int64_t>(destination.height,std::int64_t(box.y)+box.height);
    std::size_t copied=0;
    for(std::int64_t destination_y=std::max(0,box.y);destination_y<bottom;++destination_y){
        const auto source_y=crop.y+(destination_y-box.y)*crop.height/box.height;
        for(std::int64_t destination_x=std::max(0,box.x);destination_x<right;++destination_x){
            const auto source_x=crop.x+(destination_x-box.x)*crop.width/box.width;
            const auto& pixel=source.pixels[static_cast<std::size_t>(source_y)*source.width+static_cast<std::size_t>(source_x)];
            if(color_key&&pixel[0]==(*color_key)[0]&&pixel[1]==(*color_key)[1]&&pixel[2]==(*color_key)[2])continue;
            // BMP remains opaque/color-keyed.32-bit UI TGA retains its actual
            // alpha, including antialiased curved edges, over the scene.
            const unsigned alpha=source.bit_count==32?pixel[3]:255;
            if(!alpha)continue;
            auto& output = destination.pixels[static_cast<std::size_t>(destination_y) * destination.width +
                static_cast<std::size_t>(destination_x)];
            for(unsigned channel=0;channel<3;++channel)
                output[channel]=static_cast<std::uint8_t>((unsigned(pixel[channel])*alpha+
                    unsigned(output[channel])*(255-alpha)+127)/255);
            output[3]=255;
            ++copied;
        }
    }
    return copied;
}

std::size_t blit_color_keyed_bitmap(RasterFrame& destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> crop,std::array<std::uint8_t,3> key){
    return blit_bitmap(destination,source,box,crop,key);
}
std::size_t blit_rgba_bitmap(RasterFrame& destination,const BitmapImage& source,UiBitmapRect box,
        std::optional<UiBitmapRect> crop){
    if(source.bit_count!=32)throw FormatError("RGBA UI bitmap requires32-bit pixels");
    return blit_bitmap(destination,source,box,crop,std::nullopt);
}

} // namespace odh
