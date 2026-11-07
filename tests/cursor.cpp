// SPDX-License-Identifier: MPL-2.0
#include "odh/cursor.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void at(std::string& s,std::size_t offset,std::uint32_t value,unsigned width=4){
    for(unsigned i=0;i<width;++i)s.at(offset+i)=static_cast<char>(value>>(8*i));
}
std::string dib(unsigned width,unsigned height,unsigned size_profile=2){
    const auto stride=((width+31)/32)*4,plane=stride*height;
    std::string s(48+plane*2,'\0');at(s,0,40);at(s,4,width);at(s,8,height*2);at(s,12,1,2);at(s,14,1,2);
    at(s,20,plane*size_profile);at(s,32,2);s[44]=s[45]=s[46]=static_cast<char>(255);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const unsigned operation=(x+3*y)%4;const auto byte=(height-1-y)*stride+x/8;
        const auto mask=static_cast<unsigned char>(0x80u>>(x%8));
        if(operation&1)s[48+byte]=static_cast<char>(static_cast<unsigned char>(s[48+byte])|mask);
        if(operation&2)s[48+plane+byte]=static_cast<char>(static_cast<unsigned char>(s[48+plane+byte])|mask);
    }
    return s;
}
std::string cur(const std::vector<std::array<unsigned,2>>& dimensions,unsigned size_profile=2){
    std::string s(6+dimensions.size()*16,'\0');at(s,2,2,2);at(s,4,static_cast<unsigned>(dimensions.size()),2);
    for(unsigned i=0;i<dimensions.size();++i){
        const auto w=dimensions[i][0],h=dimensions[i][1],p=6+i*16;const auto image=dib(w,h,size_profile);
        s[p]=static_cast<char>(w==256?0:w);s[p+1]=static_cast<char>(h==256?0:h);s[p+2]=2;
        at(s,p+4,w-1,2);at(s,p+6,h-1,2);at(s,p+8,static_cast<unsigned>(image.size()));at(s,p+12,static_cast<unsigned>(s.size()));s+=image;
    }return s;
}
odh::WindowsCursor read(const std::string& bytes,odh::CursorLimits limits={}){
    std::istringstream stream(bytes,std::ios::binary);return odh::read_windows_cur(odh::View(stream,bytes.size()),limits);
}
void reject(const std::string& bytes,odh::CursorLimits limits={}){
    bool failed=false;try{(void)read(bytes,limits);}catch(const odh::FormatError&){failed=true;}
    need(failed,"invalid or over-budget CUR admitted");
}
void cases(){
    for(unsigned profile=0;profile<3;++profile)for(unsigned width:{1u,9u,16u,17u,31u,32u,33u,256u}){
        const auto result=read(cur({{width,7},{16,16}},profile)); // Input stream already destroyed.
        need(result.images.size()==2,"all directory images decoded in original order");
        for(const auto& image:result.images){
            need(image.hotspot_x==image.width-1&&image.hotspot_y==image.height-1,"hotspot is independent of opacity and preserved exactly");
            need(image.pixels.size()==std::size_t(image.width)*image.height&&image.invert.size()==image.pixels.size(),"owned RGBA/invert arrays exact");
            for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){
                const auto operation=(x+3*y)%4;
                const auto index=std::size_t(y)*image.width+x;
                const auto c=std::uint8_t(operation==1?255:0);
                const std::array<std::uint8_t,4> expected=operation<2?std::array<std::uint8_t,4>{c,c,c,255}:std::array<std::uint8_t,4>{0,0,0,0};
                need(image.pixels[index]==expected&&image.invert[index]==(operation==3?1:0),"four AND/XOR states, top-down rows and DWORD padding");
            }
        }
    }
    const auto maximum=read(cur({{256,256}}));need(maximum.images[0].pixels.size()==65536,"zero directory bytes encode256 dimensions");
    const auto valid=cur({{32,32},{16,16}});const auto first=38u,second=first+304;
    for(unsigned change=0;change<23;++change){
        auto bad=valid;
        switch(change){
        case 0:at(bad,0,1,2);break;
        case 1:at(bad,2,1,2);break;
        case 2:at(bad,4,0,2);break;
        case 3:at(bad,4,100,2);break;
        case 4:bad[9]=1;break;
        case 5:bad[8]=3;break;
        case 6:at(bad,10,32,2);break;
        case 7:at(bad,12,32,2);break;
        case 8:at(bad,18,6);break;
        case 9:at(bad,18,0xffffffffu);break;
        case 10:at(bad,14,0xffffffffu);break;
        case 11:at(bad,34,first);break; // Overlapping second image.
        case 12:at(bad,first,108);break;
        case 13:at(bad,first+4,31);break;
        case 14:at(bad,first+8,32);break;
        case 15:at(bad,first+8,0xffffffc0u);break;
        case 16:at(bad,first+12,2,2);break;
        case 17:at(bad,first+14,8,2);break;
        case 18:at(bad,first+16,1);break;
        case 19:at(bad,first+20,17);break;
        case 20:at(bad,first+32,3);break;
        case 21:bad[first+40]=1;break;
        case 22:at(bad,second+14,32,2);break; // Unselected image is also validated.
        }
        reject(bad);
    }
    auto truncated=valid;truncated.pop_back();reject(truncated);
    auto bad_size=valid;at(bad_size,14,303);reject(bad_size);
    odh::CursorLimits limit;limit.bytes=valid.size()-1;reject(valid,limit);
    limit={};limit.images=1;reject(valid,limit);
    limit={};limit.width=31;reject(valid,limit);
    limit={};limit.height=31;reject(valid,limit);
    limit={};limit.pixels=1279;reject(valid,limit); //32²+16²=1280.
    limit.pixels=1280;need(read(valid,limit).images.size()==2,"cumulative pixel limit inclusive");
}
}
int main(){try{cases();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
