#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>
namespace odh_json_detail {
inline bool valid_utf8(const std::string& s){
    for(std::size_t i=0;i<s.size();){
        const auto lead=static_cast<unsigned char>(s[i++]);
        if(lead<128)continue;
        unsigned remaining=0;std::uint32_t value=0,minimum=0;
        if(lead>=0xc2&&lead<=0xdf){remaining=1;value=lead&0x1fU;minimum=0x80;}
        else if(lead>=0xe0&&lead<=0xef){remaining=2;value=lead&0x0fU;minimum=0x800;}
        else if(lead>=0xf0&&lead<=0xf4){remaining=3;value=lead&0x07U;minimum=0x10000;}
        else return false;
        if(remaining>s.size()-i)return false;
        for(unsigned n=0;n<remaining;++n){
            const auto byte=static_cast<unsigned char>(s[i++]);
            if((byte&0xc0U)!=0x80U)return false;
            value=(value<<6)|(byte&0x3fU);
        }
        if(value<minimum||value>0x10ffffU||(value>=0xd800U&&value<=0xdfffU))return false;
    }
    return true;
}
inline std::string escaped_string(const std::string& s,bool utf8){std::ostringstream out;out<<'"';
    for(const unsigned char c:s){if(c=='"'||c=='\\')out<<'\\'<<static_cast<char>(c);
        else if(c<32 || (!utf8&&c>=127))out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<unsigned>(c);
        else out<<static_cast<char>(c);}out<<'"';return out.str();}
}
// Opaque resource bytes retain their existing one-byte-to-one-codepoint format.
inline std::string json_bytes(const std::string& s){return odh_json_detail::escaped_string(s,false);}
// Preserve decoded UTF-8 sequences rather than escaping their individual bytes.
// Older/no-codec checkpoints may contain opaque bytes: retain their byte format
// on invalid UTF-8 instead of emitting broken JSON or modifying saved state.
inline std::string json_utf8(const std::string& s){return odh_json_detail::escaped_string(s,odh_json_detail::valid_utf8(s));}
inline std::uint16_t selector(const std::string& s){if(s.empty()||s[0]<'0'||s[0]>'9')throw std::invalid_argument("invalid selector");
    std::size_t end=0;const auto v=std::stoull(s,&end,s.size()>2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')?16:10);
    if(end!=s.size()||v>65535)throw std::invalid_argument("selector outside uint16 range");
    return static_cast<std::uint16_t>(v);}
