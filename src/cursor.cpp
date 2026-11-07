// SPDX-License-Identifier: MPL-2.0
#include "odh/cursor.hpp"

namespace odh {
WindowsCursor read_windows_cur(const View& view,CursorLimits limits){
    if(view.size()>limits.bytes||view.size()<6||view.le16(0)!=0||view.le16(2)!=2)
        throw FormatError("CUR header/type or byte budget invalid");
    const auto count=view.le16(4);
    const std::uint64_t directory_end=6+std::uint64_t(count)*16;
    if(!count||count>limits.images||directory_end>view.size())throw FormatError("CUR directory count/extent invalid");
    struct Entry {std::uint16_t width,height,x,y;std::uint32_t bytes,offset;};
    std::vector<Entry> entries;entries.reserve(count);
    std::uint64_t pixels=0;
    for(std::uint32_t i=0;i<count;++i){
        const auto at=6+std::uint64_t(i)*16;
        Entry entry{std::uint16_t(view.u8(at)?view.u8(at):256),std::uint16_t(view.u8(at+1)?view.u8(at+1):256),
            view.le16(at+4),view.le16(at+6),view.le32(at+8),view.le32(at+12)};
        if(view.u8(at+3)||(view.u8(at+2)!=0&&view.u8(at+2)!=2)||entry.x>=entry.width||entry.y>=entry.height||
           entry.width>limits.width||entry.height>limits.height||entry.offset<directory_end||
           entry.offset>view.size()||entry.bytes>view.size()-entry.offset)
            throw FormatError("CUR directory dimensions/hotspot/image range invalid");
        pixels+=std::uint64_t(entry.width)*entry.height;
        if(pixels>limits.pixels)throw FormatError("CUR cumulative pixel budget exceeded");
        for(const auto& previous:entries)if(std::uint64_t(entry.offset)<std::uint64_t(previous.offset)+previous.bytes&&
            std::uint64_t(previous.offset)<std::uint64_t(entry.offset)+entry.bytes)
            throw FormatError("CUR image ranges overlap");
        entries.push_back(entry);
    }
    WindowsCursor result;result.images.reserve(count);
    for(const auto& entry:entries){
        const auto dib=view.subview(entry.offset,entry.bytes);
        const std::uint64_t stride=((std::uint64_t(entry.width)+31)/32)*4,plane=stride*entry.height;
        if(dib.size()!=48+plane*2||dib.le32(0)!=40||dib.le32(4)!=entry.width||
           dib.le32(8)!=std::uint32_t(entry.height)*2||dib.le16(12)!=1||dib.le16(14)!=1||dib.le32(16)!=0||
           (dib.le32(20)!=0&&dib.le32(20)!=plane&&dib.le32(20)!=plane*2)||
           (dib.le32(32)!=0&&dib.le32(32)!=2)||dib.le32(36)>2)
            throw FormatError("CUR DIB outside original uncompressed monochrome profile");
        for(unsigned channel=0;channel<3;++channel)
            if(dib.u8(40+channel)!=0||dib.u8(44+channel)!=255)
                throw FormatError("CUR monochrome palette differs from original profile");
        CursorImage image;image.width=entry.width;image.height=entry.height;image.hotspot_x=entry.x;image.hotspot_y=entry.y;
        image.pixels.resize(std::size_t(entry.width)*entry.height);image.invert.assign(image.pixels.size(),0);
        for(std::uint32_t y=0;y<entry.height;++y)for(std::uint32_t x=0;x<entry.width;++x){
            const auto byte=(entry.height-1-y)*stride+x/8;
            const auto mask=std::uint8_t(0x80u>>(x%8));
            const bool xor_bit=(dib.u8(48+byte)&mask)!=0,and_bit=(dib.u8(48+plane+byte)&mask)!=0;
            const auto index=std::size_t(y)*entry.width+x;
            if(and_bit){image.pixels[index]={0,0,0,0};image.invert[index]=xor_bit?1:0;}
            else{const auto color=std::uint8_t(xor_bit?255:0);image.pixels[index]={color,color,color,255};}
        }
        result.images.push_back(std::move(image));
    }
    return result;
}
}
