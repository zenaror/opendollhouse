#include "odh/sprite.hpp"
#include <algorithm>

namespace odh {
namespace {
class SpriteCursor {
public:
    explicit SpriteCursor(const View& view): view_(view) {}
    std::uint64_t remaining() const { return view_.size()-position_; }
    std::uint8_t byte() { const auto n=view_.u8(position_); ++position_; return n; }
    std::uint16_t word() { const auto n=view_.le16(position_); position_+=2; return n; }
    View take(std::uint64_t count) {
        auto result=view_.subview(position_,count); position_+=count; return result;
    }
private:
    const View& view_;
    std::uint64_t position_=0;
};
std::int16_t signed16(std::uint16_t value) {
    return static_cast<std::int16_t>(value<0x8000U?std::int32_t{value}:
                                    std::int32_t{value}-0x10000);
}
void command(std::uint64_t& used, const SpriteLimits& limits) {
    if(used>=limits.commands)throw FormatError("SPR2 command budget exceeded");
    ++used;
}
}

Palette read_palt(const View& view, SpriteLimits limits) {
    std::array<std::uint8_t,16> header{};
    view.read(0,header.data(),header.size());
    Palette result{};
    result.version=view.le32(0);
    const auto count=view.le32(4);
    if(count==0 || count>256 || count>limits.palette_entries)
        throw FormatError("PALT color count exceeds supported or configured bounds");
    std::copy(header.begin()+8,header.end(),result.reserved.begin());
    const auto end=16+std::uint64_t{count}*3;
    view.subview(16,std::uint64_t{count}*3);
    result.colors.resize(count);
    for(std::uint32_t i=0;i<count;++i) {
        std::array<std::uint8_t,3> rgb{};
        view.read(16+std::uint64_t{i}*3,rgb.data(),rgb.size());
        result.colors[i]={rgb[0],rgb[1],rgb[2],255};
    }
    result.remaining_bytes=view.size()-end;
    return result;
}

Spr2Frame read_spr2_frame(const View& view, std::uint32_t frame_index,
                          const Palette& palette, SpriteLimits limits) {
    std::array<std::uint8_t,12> header{};
    view.read(0,header.data(),header.size());
    Spr2Frame result{};
    result.version=view.le32(0);
    if(result.version!=1000)throw FormatError("unsupported SPR2 version (supports 1000)");
    const auto frame_count=view.le32(4);
    result.default_palette_id=view.le32(8);
    const auto table_end=12+std::uint64_t{frame_count}*4;
    if(frame_count>limits.frames || frame_index>=frame_count || table_end>view.size() ||
       frame_count>std::vector<std::uint32_t>{}.max_size())
        throw FormatError("SPR2 frame table exceeds configured or payload bounds");
    std::vector<std::uint32_t> offsets(frame_count);
    for(std::uint32_t i=0;i<frame_count;++i) {
        offsets[i]=view.le32(12+std::uint64_t{i}*4);
        if(offsets[i]<table_end)throw FormatError("SPR2 frame overlaps offset table");
        // All frame headers must really exist, including unselected frames.
        std::array<std::uint8_t,16> frame_header{};
        view.read(offsets[i],frame_header.data(),frame_header.size());
    }
    const auto selected_offset=offsets[frame_index];
    std::sort(offsets.begin(),offsets.end());
    offsets.erase(std::unique(offsets.begin(),offsets.end()),offsets.end());
    std::uint64_t selected_end=view.size();
    for(std::size_t i=0;i<offsets.size();++i) {
        const auto end=i+1<offsets.size()?std::uint64_t{offsets[i+1]}:view.size();
        if(end-std::uint64_t{offsets[i]}<16)
            throw FormatError("SPR2 distinct frame headers overlap");
        if(offsets[i]==selected_offset)selected_end=end;
    }
    const auto frame=view.subview(selected_offset,selected_end-selected_offset);
    result.width=frame.le16(0); result.height=frame.le16(2);
    result.flags=frame.le32(4); result.palette_id=frame.le16(8);
    result.effective_palette_id=static_cast<std::uint16_t>(result.default_palette_id);
    result.transparent_index=frame.le16(10);
    result.y=signed16(frame.le16(12)); result.x=signed16(frame.le16(14));
    if((result.flags&1U)==0 || (result.flags&~7U)!=0)
        throw FormatError("unsupported SPR2 flags (pixel flag required)");
    if(palette.colors.empty() || palette.colors.size()>256 ||
       palette.colors.size()>limits.palette_entries ||
       result.transparent_index>=palette.colors.size())
        throw FormatError("SPR2 palette or transparent index outside bounds");
    const auto count=std::uint64_t{result.width}*result.height;
    if(result.width>limits.width || result.height>limits.height ||
       count>limits.pixels || count>result.pixels.max_size() ||
       count>result.indices.max_size() || count>result.z.max_size())
        throw FormatError("SPR2 dimensions exceed configured allocation budget: width="+
            std::to_string(result.width)+" height="+std::to_string(result.height)+
            " pixels="+std::to_string(count)+" pixel_limit="+std::to_string(limits.pixels)+
            " size_limit="+std::to_string(limits.width)+"x"+std::to_string(limits.height));
    result.pixels.resize(static_cast<std::size_t>(count));
    result.indices.resize(static_cast<std::size_t>(count));
    const bool has_z=(result.flags&2U)!=0;
    if(has_z)result.z.resize(static_cast<std::size_t>(count));
    auto transparent=palette.colors[result.transparent_index]; transparent[3]=0;
    const auto put_transparent=[&](std::uint32_t y,std::uint32_t x) {
        const auto at=static_cast<std::size_t>(std::uint64_t{y}*result.width+x);
        result.pixels[at]=transparent;
        result.indices[at]=static_cast<std::uint8_t>(result.transparent_index);
        if(has_z)result.z[at]=255;
    };
    const auto data=frame.subview(16,frame.size()-16);
    SpriteCursor input(data);
    std::uint32_t y=0;
    std::uint64_t commands=0;
    while(input.remaining()!=0) {
        command(commands,limits);
        const auto marker=input.word();
        const auto code=marker>>13;
        const auto run=marker&0x1FFFU;
        if(code==5) { result.ended_with_marker=true; break; }
        if(code==4) {
            if(run>std::uint32_t{result.height}-y)
                throw FormatError("SPR2 skipped rows exceed height");
            for(std::uint32_t row=0;row<run;++row,++y)
                for(std::uint32_t x=0;x<result.width;++x)put_transparent(y,x);
        } else if(code==0) {
            if(run<2 || y>=result.height)throw FormatError("invalid SPR2 row extent");
            const auto row=input.take(run-2);
            SpriteCursor packets(row);
            std::uint32_t x=0;
            while(packets.remaining()!=0) {
                command(commands,limits);
                const auto packet=packets.word();
                const auto operation=packet>>13;
                const auto length=packet&0x1FFFU;
                if(length>std::uint32_t{result.width}-x)
                    throw FormatError("SPR2 packet exceeds row width");
                if(operation!=1 && operation!=2 && operation!=3 && operation!=6)
                    throw FormatError("unsupported SPR2 pixel command");
                if((operation==1 && !has_z) ||
                   (operation==2 && (!has_z || (result.flags&4U)==0)))
                    throw FormatError("SPR2 pixel command requires missing Z or alpha flag");
                for(std::uint32_t n=0;n<length;++n,++x) {
                    if(operation==3) { put_transparent(y,x); continue; }
                    const auto z=operation==6?std::uint8_t{0}:packets.byte();
                    const auto index=packets.byte();
                    if(index>=palette.colors.size())throw FormatError("SPR2 index exceeds palette");
                    const auto alpha=operation==2?packets.byte():std::uint8_t{31};
                    if(alpha>31)throw FormatError("SPR2 alpha exceeds five-bit range");
                    const auto at=static_cast<std::size_t>(std::uint64_t{y}*result.width+x);
                    result.pixels[at]=palette.colors[index];
                    result.pixels[at][3]=static_cast<std::uint8_t>(std::uint32_t{alpha}*255/31);
                    result.indices[at]=index;
                    if(has_z)result.z[at]=z;
                }
                if((operation==2 || operation==6) && (length&1U))packets.byte();
            }
            // The source profile only fills Z in an unencoded row tail;
            // untouched RGBA/index values retain their zero initialization.
            if(has_z)for(;x<result.width;++x)
                result.z[static_cast<std::size_t>(std::uint64_t{y}*result.width+x)]=255;
            ++y;
        } else throw FormatError("unsupported SPR2 outer command");
    }
    if(!result.ended_with_marker && y!=result.height)
        throw FormatError("SPR2 ends before all rows without end marker");
    result.remaining_bytes=input.remaining();
    return result;
}
} // namespace odh
