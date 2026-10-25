#include "linux_vm_dialog.hpp"

#include "linux_text_renderer.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace odh_host {
namespace {
using Color=std::array<std::uint8_t,4>;

void fill(odh::RasterFrame& frame,std::int32_t x,std::int32_t y,
          std::int32_t width,std::int32_t height,Color color) {
    const auto left=std::clamp<std::int64_t>(x,0,frame.width);
    const auto top=std::clamp<std::int64_t>(y,0,frame.height);
    const auto right=std::clamp<std::int64_t>(static_cast<std::int64_t>(x)+width,0,frame.width);
    const auto bottom=std::clamp<std::int64_t>(static_cast<std::int64_t>(y)+height,0,frame.height);
    for(auto row=top;row<bottom;++row)for(auto column=left;column<right;++column)
        frame.pixels[static_cast<std::size_t>(row)*frame.width+static_cast<std::size_t>(column)]=color;
}

void dim(odh::RasterFrame& frame) {
    for(auto& pixel:frame.pixels)for(std::size_t channel=0;channel<3;++channel)
        pixel[channel]=static_cast<std::uint8_t>((static_cast<unsigned>(pixel[channel])*3U)/8U);
}
}

std::optional<VmMessageDialogLayout> vm_message_dialog_layout(
        std::uint32_t frame_width,std::uint32_t frame_height) noexcept {
    if(frame_width<240||frame_height<160||frame_width>8192||frame_height>8192)return {};
    const auto width=static_cast<std::int32_t>(std::min<std::uint32_t>(520,frame_width-32));
    const auto height=static_cast<std::int32_t>(std::min<std::uint32_t>(232,frame_height-32));
    const auto x=(static_cast<std::int32_t>(frame_width)-width)/2;
    const auto y=(static_cast<std::int32_t>(frame_height)-height)/2;
    const auto ok_width=std::min(92,width-32);
    const auto ok_height=30;
    return VmMessageDialogLayout{x,y,width,height,x+width-16-ok_width,
        y+height-16-ok_height,ok_width,ok_height};
}

bool vm_message_dialog_ok_hit(const VmMessageDialogLayout& layout,
        std::int32_t x,std::int32_t y) noexcept {
    return x>=layout.ok_x&&y>=layout.ok_y&&x<layout.ok_x+layout.ok_width&&
        y<layout.ok_y+layout.ok_height;
}

std::string expand_vm_dialog_local_references(std::string_view input,
        const std::vector<std::int16_t>& locals) {
    if(input.size()>4096)throw std::invalid_argument("VM dialog text exceeds its byte budget");
    std::string output;
    output.reserve(input.size());
    for(std::size_t cursor=0;cursor<input.size();){
        if(input[cursor]!='$'){
            output.push_back(input[cursor++]);
            continue;
        }
        std::size_t marker=cursor;
        if(input.compare(cursor,8,"$$Local:")==0){
            output.push_back('$');
            ++marker;
        }
        if(input.compare(marker,7,"$Local:")!=0)
            throw std::invalid_argument("VM dialog contains an unsupported string marker");
        const auto first_digit=marker+7;
        auto end=first_digit;
        while(end<input.size()&&input[end]>='0'&&input[end]<='9')++end;
        if(end==first_digit)throw std::invalid_argument("VM dialog Local reference has no index");
        std::size_t index=0;
        const auto parsed=std::from_chars(input.data()+first_digit,input.data()+end,index);
        if(parsed.ec!=std::errc{}||parsed.ptr!=input.data()+end||index>=locals.size())
            throw std::invalid_argument("VM dialog Local reference is outside the saved frame");
        output+=std::to_string(locals[index]);
        cursor=end;
        if(output.size()>4096)throw std::invalid_argument("expanded VM dialog text exceeds its byte budget");
    }
    return output;
}

void draw_vm_message_dialog(odh::RasterFrame& frame,std::string_view title,
        std::string_view message,std::string_view ok_label,std::uint8_t language) {
    const auto layout=vm_message_dialog_layout(frame.width,frame.height);
    if(!layout||frame.pixels.size()!=static_cast<std::size_t>(frame.width)*frame.height||
       title.size()>4096||message.empty()||message.size()>4096||ok_label.empty()||ok_label.size()>256)
        throw std::invalid_argument("VM message dialog input is outside its bounded profile");
    dim(frame);
    fill(frame,layout->x,layout->y,layout->width,layout->height,{38,43,48,255});
    fill(frame,layout->x,layout->y,layout->width,4,{82,186,170,255});
    fill(frame,layout->x+1,layout->y+1,layout->width-2,layout->height-2,{68,72,75,255});

    TextOverlayStyle style;
    style.x=layout->x+18;style.width=layout->width-36;style.padding=2;
    style.foreground={246,244,231,255};style.background={68,72,75,0};
    if(!title.empty()){
        style.y=layout->y+15;style.height=30;style.font_size_px=19;
        (void)draw_text_overlay(frame,title,language,style);
    }
    style.y=layout->y+(title.empty()?20:54);style.height=layout->height-112;
    style.font_size_px=15;style.padding=4;
    (void)draw_text_overlay(frame,message,language,style);

    fill(frame,layout->ok_x,layout->ok_y,layout->ok_width,layout->ok_height,{102,76,43,255});
    fill(frame,layout->ok_x,layout->ok_y,layout->ok_width,2,{181,145,85,255});
    style.x=layout->ok_x;style.y=layout->ok_y;style.width=layout->ok_width;
    style.height=layout->ok_height;style.font_size_px=14;style.padding=2;
    style.foreground={255,250,231,255};style.background={102,76,43,0};
    (void)draw_text_overlay(frame,ok_label,language,style);
}

} // namespace odh_host
