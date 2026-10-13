#include "odh/bitmap.hpp"
#include <limits>

namespace odh {
namespace {
std::int64_t signed32(std::uint32_t value) {
    return value < 0x80000000U ? std::int64_t{value} :
        std::int64_t{value} - 0x100000000LL;
}
std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if(b && a > std::numeric_limits<std::uint64_t>::max()/b)
        throw FormatError("BMP extent overflow");
    return a*b;
}
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if(a > std::numeric_limits<std::uint64_t>::max()-b)
        throw FormatError("BMP extent overflow");
    return a+b;
}
}

BitmapImage read_bmp(const View& view, BitmapLimits limits) {
    // Read the complete mandatory headers, including metadata not interpreted
    // by this profile. A partially backed View must not hide truncated fields.
    std::array<std::uint8_t,54> header{};
    view.read(0,header.data(),header.size());
    if(view.le16(0)!=0x4D42 || view.le32(14)!=40)
        throw FormatError("unsupported BMP signature or DIB header");
    if(view.le16(6)!=0 || view.le16(8)!=0)
        throw FormatError("nonzero BMP file reserved fields");
    const auto declared_size=view.le32(2);
    const auto pixel_offset=view.le32(10);
    const auto signed_width=signed32(view.le32(18));
    const auto signed_height=signed32(view.le32(22));
    const auto bits=view.le16(28);
    if(signed_width<=0 || signed_height==0 || signed_height==-2147483648LL)
        throw FormatError("invalid BMP dimensions");
    if(view.le16(26)!=1 || view.le32(30)!=0 || (bits!=8 && bits!=24))
        throw FormatError("unsupported BMP planes, compression or bit depth");
    BitmapImage result{};
    result.width=static_cast<std::uint32_t>(signed_width);
    result.top_down=signed_height<0;
    result.height=static_cast<std::uint32_t>(result.top_down?-signed_height:signed_height);
    result.bit_count=bits;
    const auto pixel_count=multiply(result.width,result.height);
    if(result.width>limits.width || result.height>limits.height ||
       pixel_count>limits.pixels || pixel_count>result.pixels.max_size())
        throw FormatError("BMP dimensions exceed configured allocation budget");
    const auto stride=multiply(add(multiply(result.width,bits),31)/32,4);
    if(stride>std::vector<std::uint8_t>{}.max_size())
        throw FormatError("BMP row allocation extent exceeded");
    const auto image_bytes=multiply(stride,result.height);
    const auto size_image=view.le32(34);
    if(size_image!=0 && size_image!=image_bytes)
        throw FormatError("BMP image size does not match row extent");
    const auto used=view.le32(46);
    // True-color files with color tables are outside this narrow profile.
    if((bits==8 && used>256) || (bits==24 && used!=0))
        throw FormatError("unsupported BMP palette size");
    const auto palette_count=bits==8?(used==0?256U:used):0U;
    const auto palette_end=add(54,multiply(palette_count,4));
    const auto pixel_end=add(pixel_offset,image_bytes);
    if(pixel_offset<palette_end || declared_size==0 || declared_size<pixel_end ||
       declared_size>view.size() || pixel_end>view.size())
        throw FormatError("BMP pixel or declared file extent outside payload");
    std::vector<std::array<std::uint8_t,4>> palette(palette_count);
    for(std::uint32_t i=0;i<palette_count;++i) {
        std::array<std::uint8_t,4> entry{};
        view.read(54+std::uint64_t{i}*4,entry.data(),entry.size());
        palette[i]={entry[2],entry[1],entry[0],255};
    }
    result.pixels.resize(static_cast<std::size_t>(pixel_count));
    std::vector<std::uint8_t> row(static_cast<std::size_t>(stride));
    for(std::uint32_t y=0;y<result.height;++y) {
        view.read(pixel_offset+std::uint64_t{y}*stride,row.data(),row.size());
        const auto output_y=result.top_down?y:result.height-1-y;
        for(std::uint32_t x=0;x<result.width;++x) {
            auto& pixel=result.pixels[static_cast<std::size_t>(std::uint64_t{output_y}*result.width+x)];
            if(bits==8) {
                if(row[x]>=palette_count)throw FormatError("BMP index exceeds palette");
                pixel=palette[row[x]];
            } else {
                const auto at=static_cast<std::size_t>(x)*3;
                pixel={row[at+2],row[at+1],row[at],255};
            }
        }
    }
    result.remaining_bytes=view.size()-pixel_end;
    return result;
}
} // namespace odh
