#include "linux_text_codec.hpp"

#include <cerrno>
#include <iconv.h>
#include <stdexcept>
#include <string>

namespace odh_host {
namespace {
const char* encoding(std::uint8_t language) {
    switch(language) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11: case 14: return "CP1252";
    case 12: return "CP1255"; // Hebrew; this mapping remains to be checked against a localized original string.
    case 13: return "CP1251"; // Russian; this mapping remains to be checked against a localized original string.
    case 15: return "CP932";
    case 16: return "CP1250";
    case 17: return "CP936";
    case 18: return "CP950";
    case 19: return "CP874";
    case 20: return "CP949";
    default: throw std::invalid_argument("unsupported STR language code");
    }
}
void append(std::string& output,const char* data,std::size_t size,std::size_t limit) {
    if(size>limit-output.size())throw std::length_error("converted text exceeds configured byte limit");
    output.append(data,size);
}
constexpr char replacement[]="\xEF\xBF\xBD";
}

std::string legacy_text_to_utf8(std::string_view bytes,std::uint8_t language,std::size_t output_limit) {
    if(bytes.size()>output_limit)throw std::length_error("source text exceeds configured byte limit");
    iconv_t converter=iconv_open("UTF-8",encoding(language));
    if(converter==reinterpret_cast<iconv_t>(-1))throw std::runtime_error("iconv cannot open the requested legacy encoding");
    struct Close { iconv_t value; ~Close(){iconv_close(value);} } close{converter};

    std::string output;
    output.reserve(bytes.size());
    const char* source=bytes.data();
    std::size_t remaining=bytes.size();
    while(remaining) {
        char buffer[4096];char* destination=buffer;std::size_t available=sizeof(buffer);
        char* mutable_source=const_cast<char*>(source);
        errno=0;
        const auto converted=iconv(converter,&mutable_source,&remaining,&destination,&available);
        source=mutable_source;
        append(output,buffer,sizeof(buffer)-available,output_limit);
        if(converted!=static_cast<std::size_t>(-1))continue;
        if(errno==E2BIG)continue;
        if(errno!=EILSEQ&&errno!=EINVAL)throw std::runtime_error("iconv failed while converting game text");
        append(output,replacement,sizeof(replacement)-1,output_limit);
        ++source;--remaining;
        if(iconv(converter,nullptr,nullptr,nullptr,nullptr)==static_cast<std::size_t>(-1))
            throw std::runtime_error("iconv could not reset after an invalid game-text byte");
    }
    return output;
}
} // namespace odh_host
