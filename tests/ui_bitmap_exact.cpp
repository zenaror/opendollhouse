// SPDX-License-Identifier: MPL-2.0
#include "odh/ui_bitmap.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Pixel=std::array<std::uint8_t,4>;
const std::array<std::uint8_t,3> key{255,0,255};
std::uint32_t state=0x563b19a7;
std::uint32_t random_word(){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
Pixel random_pixel(){const auto n=random_word();return {std::uint8_t(n),std::uint8_t(n>>8),std::uint8_t(n>>16),std::uint8_t(n>>24)};}

// Frozen pre-optimization implementation: keep its wide coordinate division,
// validation order, colour-key test and rounded alpha formula as the oracle.
std::size_t reference(odh::RasterFrame& destination,const odh::BitmapImage& source,odh::UiBitmapRect box,
        std::optional<odh::UiBitmapRect> source_crop,bool rgba){
    if(rgba&&source.bit_count!=32)throw odh::FormatError("RGBA UI bitmap requires32-bit pixels");
    const auto destination_pixels=std::uint64_t(destination.width)*destination.height;
    const auto source_pixels=std::uint64_t(source.width)*source.height;
    if(!destination.width||!destination.height||destination_pixels>std::numeric_limits<std::size_t>::max()||
       destination.pixels.size()!=static_cast<std::size_t>(destination_pixels))
        throw odh::FormatError("UI bitmap destination frame is malformed");
    if(!source.width||!source.height||source_pixels>std::numeric_limits<std::size_t>::max()||
       source.pixels.size()!=static_cast<std::size_t>(source_pixels))
        throw odh::FormatError("UI bitmap source image is malformed");
    if(source.width>INT32_MAX||source.height>INT32_MAX)throw odh::FormatError("UI bitmap source dimensions invalid");
    const auto crop=source_crop.value_or(odh::UiBitmapRect{0,0,static_cast<std::int32_t>(source.width),
        static_cast<std::int32_t>(source.height)});
    if(crop.x<0||crop.y<0||crop.width<=0||crop.height<=0||std::int64_t(crop.x)+crop.width>source.width||
       std::int64_t(crop.y)+crop.height>source.height)throw odh::FormatError("UI bitmap crop outside source image");
    if(box.width<=0||box.height<=0)return 0;
    const auto right=std::min<std::int64_t>(destination.width,std::int64_t(box.x)+box.width);
    const auto bottom=std::min<std::int64_t>(destination.height,std::int64_t(box.y)+box.height);
    std::size_t copied=0;
    for(std::int64_t dy=std::max(0,box.y);dy<bottom;++dy){
        const auto sy=crop.y+(dy-box.y)*crop.height/box.height;
        for(std::int64_t dx=std::max(0,box.x);dx<right;++dx){
            const auto sx=crop.x+(dx-box.x)*crop.width/box.width;
            const auto& pixel=source.pixels[static_cast<std::size_t>(sy)*source.width+static_cast<std::size_t>(sx)];
            if(!rgba&&pixel[0]==key[0]&&pixel[1]==key[1]&&pixel[2]==key[2])continue;
            const unsigned alpha=source.bit_count==32?pixel[3]:255;
            if(!alpha)continue;
            auto& output=destination.pixels[static_cast<std::size_t>(dy)*destination.width+static_cast<std::size_t>(dx)];
            for(unsigned channel=0;channel<3;++channel)
                output[channel]=static_cast<std::uint8_t>((unsigned(pixel[channel])*alpha+
                    unsigned(output[channel])*(255-alpha)+127)/255);
            output[3]=255;++copied;
        }
    }
    return copied;
}

std::size_t comparisons=0;
void compare(const odh::RasterFrame& initial,const odh::BitmapImage& source,odh::UiBitmapRect box,
        std::optional<odh::UiBitmapRect> crop,bool rgba){
    auto expected=initial,actual=initial;
    std::string expected_error,actual_error;std::size_t expected_count=0,actual_count=0;
    try{expected_count=reference(expected,source,box,crop,rgba);}catch(const odh::FormatError& error){expected_error=error.what();}
    try{actual_count=rgba?odh::blit_rgba_bitmap(actual,source,box,crop):
        odh::blit_color_keyed_bitmap(actual,source,box,crop,key);}catch(const odh::FormatError& error){actual_error=error.what();}
    ++comparisons;
    if(expected_error!=actual_error||expected_count!=actual_count||expected.pixels!=actual.pixels||
       initial.depth!=actual.depth||initial.owner_ids!=actual.owner_ids)
        throw std::runtime_error("bitmap reference mismatch at case "+std::to_string(comparisons));
}
odh::RasterFrame frame(std::uint32_t width,std::uint32_t height){
    odh::RasterFrame f{};f.width=width;f.height=height;f.pixels.resize(std::size_t(width)*height);
    for(auto& p:f.pixels)p=random_pixel();
    f.depth.assign(f.pixels.size(),7.0f);f.owner_ids.assign(f.pixels.size(),12);return f;
}
odh::BitmapImage image(std::uint32_t width,std::uint32_t height){
    odh::BitmapImage source{width,height,32,true,{},0};source.pixels.resize(std::size_t(width)*height);
    for(std::size_t i=0;i<source.pixels.size();++i){
        source.pixels[i]=random_pixel();
        if(i%13==0)source.pixels[i]={255,0,255,std::uint8_t(i)};
        if(i%7==0)source.pixels[i][3]=255;
        if(i%11==0)source.pixels[i][3]=0;
    }
    return source;
}
}
int main(){
    for(unsigned i=0;i<5000;++i){
        auto f=frame(1+random_word()%75,1+random_word()%53);
        auto source=image(1+random_word()%67,1+random_word()%59);
        source.bit_count=i%3==0?24:32;
        const int cx=int(random_word()%source.width),cy=int(random_word()%source.height);
        const odh::UiBitmapRect crop{cx,cy,1+int(random_word()%(source.width-std::uint32_t(cx))),
            1+int(random_word()%(source.height-std::uint32_t(cy)))};
        const odh::UiBitmapRect box{int(random_word()%151)-76,int(random_word()%107)-54,
            int(random_word()%151)-1,int(random_word()%107)-1};
        compare(f,source,box,i%2?std::optional{crop}:std::nullopt,false);
        if(source.bit_count==32)compare(f,source,box,crop,true);
    }
    // Real panel, 200px head and sprite sizes at original portable/PC displays.
    for(const auto& size:{std::pair{960u,544u},std::pair{1280u,720u},std::pair{800u,600u}}){
        const auto f=frame(size.first,size.second);
        for(const auto& extent:{std::pair{800u,183u},std::pair{200u,200u},std::pair{201u,201u},std::pair{128u,40u}}){
            auto source=image(extent.first,extent.second);
            for(const auto box:{odh::UiBitmapRect{0,int(size.second)-int(extent.second),int(extent.first),int(extent.second)},
                    odh::UiBitmapRect{-17,-13,int(extent.first)*3/2,int(extent.second)*3/2},
                    odh::UiBitmapRect{int(size.first)-20,int(size.second)-20,33,23}}){
                compare(f,source,box,std::nullopt,false);compare(f,source,box,std::nullopt,true);
                source.bit_count=24;compare(f,source,box,std::nullopt,false);source.bit_count=32;
            }
        }
    }
    // Every alpha and source channel value, with both positive and negative
    // rounding residuals against independently varying destination channels.
    auto exhaustive=image(256,256);auto base=frame(256,256);
    for(unsigned a=0;a<256;++a)for(unsigned c=0;c<256;++c){
        exhaustive.pixels[a*256+c]={std::uint8_t(c),std::uint8_t(255-c),std::uint8_t(c^a),std::uint8_t(a)};
        base.pixels[a*256+c]={std::uint8_t(a),std::uint8_t(c),std::uint8_t(255-a),std::uint8_t(c)};
    }
    compare(base,exhaustive,{0,0,256,256},std::nullopt,true);
    auto f=frame(17,13);auto source=image(4096,2);
    for(const auto box:{odh::UiBitmapRect{INT32_MIN,0,INT32_MAX,2},
            odh::UiBitmapRect{-1073741824,0,INT32_MAX,2},
            odh::UiBitmapRect{-1048575,0,INT32_MAX,2},
            odh::UiBitmapRect{INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX},
            odh::UiBitmapRect{-INT32_MAX+16,0,INT32_MAX,INT32_MAX},
            odh::UiBitmapRect{0,-1073741824,INT32_MAX,INT32_MAX},
            odh::UiBitmapRect{-1,-1,INT32_MAX,INT32_MAX}}){
        compare(f,source,box,std::nullopt,false);compare(f,source,box,std::nullopt,true);
    }
    source=image(2,4096);compare(f,source,{0,-1073741824,2,INT32_MAX},std::nullopt,true);
    for(const auto crop:{odh::UiBitmapRect{-1,0,1,1},odh::UiBitmapRect{0,0,0,1},
            odh::UiBitmapRect{INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX},odh::UiBitmapRect{0,4095,2,2}})
        compare(f,source,{0,0,-1,0},crop,false);
    auto broken=f;broken.pixels.pop_back();compare(broken,source,{0,0,1,1},std::nullopt,false);
    broken=f;broken.width=0;compare(broken,source,{0,0,1,1},std::nullopt,true);
    source.pixels.pop_back();compare(f,source,{0,0,1,1},std::nullopt,false);
    source=image(1,1);source.bit_count=24;compare(f,source,{0,0,1,1},std::nullopt,true);
    source.width=UINT32_MAX;source.height=UINT32_MAX;compare(f,source,{0,0,1,1},std::nullopt,false);
    std::cout<<"Compared "<<comparisons<<" bitmap cases with the previous algorithm\n";
}
