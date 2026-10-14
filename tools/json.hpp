#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>
inline std::string json_bytes(const std::string& s){std::ostringstream out;out<<'"';
    for(const unsigned char c:s){if(c=='"'||c=='\\')out<<'\\'<<static_cast<char>(c);
        else if(c<32 || c>=127)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(c);
        else out<<static_cast<char>(c);}out<<'"';return out.str();}
inline std::uint16_t selector(const std::string& s){if(s.empty()||s[0]<'0'||s[0]>'9')throw std::invalid_argument("invalid selector");
    std::size_t end=0;const auto v=std::stoull(s,&end,s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')?16:10);
    if(end!=s.size()||v>65535)throw std::invalid_argument("selector outside uint16 range");
    return static_cast<std::uint16_t>(v);}
