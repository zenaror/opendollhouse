#pragma once
#include <cstddef>
#include <string_view>
namespace odh {
// Native names are UTF8, bounded by the source's32 UTF16 code units.
// Reject malformed strings/NUL and never substitute a replacement character.
inline bool valid_entity_name(std::string_view text) noexcept {
    std::size_t at=0,units=0;
    while(at<text.size()){
        const auto first=static_cast<unsigned char>(text[at]);unsigned cp=0;std::size_t n=0;
        if(first&&first<128){cp=first;n=1;}else if(first>=0xc2&&first<=0xdf){cp=first&31;n=2;}
        else if(first>=0xe0&&first<=0xef){cp=first&15;n=3;}else if(first>=0xf0&&first<=0xf4){cp=first&7;n=4;}else return false;
        if(at+n>text.size())return false;
        for(std::size_t i=1;i<n;++i){const auto c=static_cast<unsigned char>(text[at+i]);if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}
        if((n==2&&cp<128)||(n==3&&cp<2048)||(n==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
        units+=cp>0xffff?2:1;if(units>32)return false;at+=n;
    }
    return true;
}
}
