#include "odh/spr.hpp"
#include <algorithm>
namespace odh {
namespace {
std::uint32_t dword(const View& v,std::uint64_t at,bool big){return big?v.be32(at):v.le32(at);}
std::uint16_t word(const View& v,std::uint64_t at,bool big){return big?v.be16(at):v.le16(at);}
void command(SprFrame& f,const SpriteLimits& limits){if(f.commands>=limits.commands)throw FormatError("SPR# command budget exceeded");++f.commands;}
}
SprHeader read_spr_header(const View& v,SpriteLimits limits){
    const bool big=v.le16(0)==0;const auto version=dword(v,0,big);
    if(version!=502&&version!=504&&version!=505&&version!=1000)throw FormatError("unsupported SPR# offset-table version");
    const auto count=dword(v,4,big),palette=dword(v,8,big);
    if(count>limits.frames||count>std::vector<std::uint32_t>{}.max_size()||12+std::uint64_t(count)*4>v.size())throw FormatError("SPR# frame table exceeds bounds");
    return {version,count,palette,static_cast<std::uint16_t>(palette),big};
}
SprFrame read_spr_frame(const View& v,std::uint32_t index,const Palette& palette,SpriteLimits limits){
    SprFrame out{};out.header=read_spr_header(v,limits);
    if(index>=out.header.frame_count)throw FormatError("SPR# frame selector exceeds count");
    if(palette.colors.empty()||palette.colors.size()>256||palette.colors.size()>limits.palette_entries)throw FormatError("SPR# palette extent exceeds bounds");
    for(const auto& c:palette.colors)if(c[3]!=255)throw FormatError("SPR# palette must be opaque");
    const auto table_end=12+std::uint64_t(out.header.frame_count)*4;
    std::vector<std::uint32_t> offsets(out.header.frame_count);
    for(std::uint32_t i=0;i<out.header.frame_count;++i){offsets[i]=dword(v,12+std::uint64_t(i)*4,out.header.big_endian);
        if(offsets[i]<table_end)throw FormatError("SPR# frame overlaps offset table");
        v.subview(offsets[i],8);}
    const auto start=offsets[index];std::sort(offsets.begin(),offsets.end());offsets.erase(std::unique(offsets.begin(),offsets.end()),offsets.end());
    std::uint64_t end=v.size();
    for(std::size_t i=0;i<offsets.size();++i){const auto next=i+1<offsets.size()?std::uint64_t(offsets[i+1]):v.size();
        if(next-offsets[i]<8)throw FormatError("SPR# distinct frame headers overlap");
        if(offsets[i]==start)end=next;}
    const auto frame=v.subview(start,end-start);out.reserved=dword(frame,0,out.header.big_endian);out.height=word(frame,4,out.header.big_endian);out.width=word(frame,6,out.header.big_endian);
    const auto pixels=std::uint64_t(out.width)*out.height;
    if(out.width>limits.width||out.height>limits.height||pixels>limits.pixels||pixels>out.pixels.max_size()||pixels>out.indices.max_size())throw FormatError("SPR# pixel extent exceeds budget");
    out.pixels.resize(static_cast<std::size_t>(pixels));out.indices.resize(static_cast<std::size_t>(pixels));
    std::uint64_t at=8;std::uint32_t y=0;
    auto byte=[&](){const auto value=frame.u8(at);++at;return value;};
    auto paint=[&](std::uint32_t x,std::uint8_t color){if(color>=palette.colors.size())throw FormatError("SPR# color index outside palette");
        const auto p=std::size_t(y)*out.width+x;out.pixels[p]=palette.colors[color];out.indices[p]=color;};
    while(!out.ended_with_marker){command(out,limits);const auto op=byte(),count=byte();
        if(op==0||op==16)continue; // Start markers retain their opaque count.
        if(op==5){out.ended_with_marker=true;break;}
        if(op==9){if(count>std::uint32_t(out.height)-y)throw FormatError("SPR# skipped rows exceed height");y+=count;continue;}
        if(op!=4||count<2||y>=out.height)throw FormatError("SPR# invalid row command/count");
        const auto row_end=at+count-2;frame.subview(at,count-2);std::uint32_t x=0;
        while(at<row_end){command(out,limits);if(row_end-at<2)throw FormatError("SPR# truncated pixel opcode");const auto pixel_op=byte(),n=byte();
            if(n>std::uint32_t(out.width)-x)throw FormatError("SPR# pixel run exceeds width");
            if(pixel_op==1){x+=n;continue;}
            if(pixel_op==2){if(row_end-at<2)throw FormatError("SPR# truncated fill index/padding");const auto color=byte();byte();
                if(color>=palette.colors.size())throw FormatError("SPR# fill index outside palette");
                for(unsigned j=0;j<n;++j)paint(x++,color);
            }else if(pixel_op==3){if(std::uint64_t(n)+(n&1U)>row_end-at)throw FormatError("SPR# truncated literal pixels/padding");
                for(unsigned j=0;j<n;++j)paint(x++,byte());
                if(n&1U)byte();
            }else throw FormatError("SPR# unknown pixel opcode");
        }
        ++y;
    }
    out.consumed_bytes=at;out.remaining_bytes=frame.size()-at;return out;
}
}
