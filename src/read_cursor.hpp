#pragma once
#include "odh/formats.hpp"
namespace odh::detail {
class Cursor {
public:
    Cursor(const View& view,std::size_t text_budget):view_(view),text_budget_(text_budget){}
    std::uint64_t remaining() const {return view_.size()-position_;}
    std::uint8_t byte(){const auto x=view_.u8(position_);++position_;return x;}
    std::uint16_t u16(){const auto x=view_.le16(position_);position_+=2;return x;}
    std::uint32_t u32(){const auto x=view_.le32(position_);position_+=4;return x;}
    std::int32_t i32(){const auto n=u32();return static_cast<std::int32_t>(n<0x80000000U?
        static_cast<std::int64_t>(n):static_cast<std::int64_t>(n)-0x100000000LL);}
    std::string pascal(){const auto n=byte();if(n>text_budget_)throw FormatError("text budget exceeded");
        view_.subview(position_,n);std::string s(n,'\0');view_.read(position_,s.data(),n);position_+=n;text_budget_-=n;return s;}
    std::string terminated(){std::string s;while(true){const auto c=byte();if(!c)return s;
        if(text_budget_==0)throw FormatError("text budget exceeded");
        --text_budget_;s+=static_cast<char>(c);}}
private:
    const View& view_;std::uint64_t position_=0;std::size_t text_budget_;
};
}
