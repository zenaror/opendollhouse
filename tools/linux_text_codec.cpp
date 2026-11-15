#include "linux_text_codec.hpp"

#include <cerrno>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif
#include <limits>
#include <stdexcept>
#include <string>

namespace odh_host {
namespace {
unsigned code_page(std::uint8_t language) {
    switch(language) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11: case 14: return 1252;
    case 12: return 1255; // Hebrew; this mapping remains to be checked against a localized original string.
    case 13: return 1251; // Russian; this mapping remains to be checked against a localized original string.
    case 15: return 932;
    case 16: return 1250;
    case 17: return 936;
    case 18: return 950;
    case 19: return 874;
    case 20: return 949;
    default: throw std::invalid_argument("unsupported STR language code");
    }
}
#if !defined(_WIN32)
std::string encoding(std::uint8_t language){return "CP"+std::to_string(code_page(language));}
#endif

void append(std::string& output,const char* data,std::size_t size,std::size_t limit) {
    if(size>limit-output.size())throw std::length_error("converted text exceeds configured byte limit");
    output.append(data,size);
}
constexpr char replacement[]="\xEF\xBF\xBD";
}

#if defined(_WIN32)
namespace {
std::wstring wide_text(std::string_view bytes,unsigned cp){
    if(bytes.size()>size_t(std::numeric_limits<int>::max()))throw std::length_error("text exceeds WinAPI limit");
    if(bytes.empty())return {};
    const int length=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);
    if(!length)throw std::invalid_argument("invalid bytes for selected text encoding");
    std::wstring result(size_t(length),L'\0');
    if(MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),result.data(),length)!=length)
        throw std::runtime_error("Windows text conversion failed");
    return result;
}
std::string narrow_text(const std::wstring& text,unsigned cp,size_t limit){
    if(text.empty())return {};
    BOOL usedDefault=FALSE;
    const DWORD flags=cp==CP_UTF8?WC_ERR_INVALID_CHARS:WC_NO_BEST_FIT_CHARS;
    BOOL* defaultFlag=cp==CP_UTF8?nullptr:&usedDefault;
    const int length=WideCharToMultiByte(cp,flags,text.data(),int(text.size()),nullptr,0,nullptr,defaultFlag);
    if(!length||usedDefault)throw std::invalid_argument("user text cannot be represented in selected game language");
    if(size_t(length)>limit)throw std::length_error("converted text exceeds configured byte limit");
    std::string result(size_t(length),'\0');
    if(WideCharToMultiByte(cp,flags,text.data(),int(text.size()),result.data(),length,nullptr,defaultFlag)!=length||usedDefault)
        throw std::invalid_argument("Windows text conversion would substitute characters");
    return result;
}
}
std::string legacy_text_to_utf8(std::string_view bytes,std::uint8_t language,size_t limit){
    if(bytes.size()>limit)throw std::length_error("source text exceeds configured byte limit");
    const unsigned cp=code_page(language);
    // The iconv presentation path replaces invalid source bytes. WinAPI can
    // decode supported legacy pages with its documented replacement policy.
    if(bytes.empty())return {};
    if(bytes.size()>size_t(std::numeric_limits<int>::max()))throw std::length_error("text exceeds WinAPI limit");
    const int count=MultiByteToWideChar(cp,0,bytes.data(),int(bytes.size()),nullptr,0);
    if(!count)throw std::runtime_error("Windows cannot decode selected game language");
    std::wstring wide(size_t(count),L'\0');
    if(MultiByteToWideChar(cp,0,bytes.data(),int(bytes.size()),wide.data(),count)!=count)throw std::runtime_error("Windows text decode failed");
    return narrow_text(wide,CP_UTF8,limit);
}
std::string utf8_to_legacy_text(std::string_view text,std::uint8_t language,size_t limit){
    if(text.size()>limit)throw std::length_error("source text exceeds configured byte limit");
    if(text.find('\0')!=std::string_view::npos)throw std::invalid_argument("user text contains an embedded NUL");
    return narrow_text(wide_text(text,CP_UTF8),code_page(language),limit);
}
#else
std::string legacy_text_to_utf8(std::string_view bytes,std::uint8_t language,std::size_t output_limit) {
    if(bytes.size()>output_limit)throw std::length_error("source text exceeds configured byte limit");
    iconv_t converter=iconv_open("UTF-8",encoding(language).c_str());
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

std::string utf8_to_legacy_text(std::string_view text,std::uint8_t language,std::size_t output_limit) {
    if(text.size()>output_limit)throw std::length_error("source text exceeds configured byte limit");
    if(text.find('\0')!=std::string_view::npos)throw std::invalid_argument("user text contains an embedded NUL");
    iconv_t converter=iconv_open(encoding(language).c_str(),"UTF-8");
    if(converter==reinterpret_cast<iconv_t>(-1))throw std::runtime_error("iconv cannot open the requested legacy encoding");
    struct Close { iconv_t value; ~Close(){iconv_close(value);} } close{converter};

    std::string output;
    output.reserve(text.size());
    const char* source=text.data();
    std::size_t remaining=text.size();
    while(remaining) {
        char buffer[4096];char* destination=buffer;std::size_t available=sizeof(buffer);
        char* mutable_source=const_cast<char*>(source);
        errno=0;
        const auto converted=iconv(converter,&mutable_source,&remaining,&destination,&available);
        source=mutable_source;
        append(output,buffer,sizeof(buffer)-available,output_limit);
        if(converted!=static_cast<std::size_t>(-1)){
            if(converted)throw std::invalid_argument("user text conversion would substitute characters");
            continue;
        }
        if(errno==E2BIG)continue;
        if(errno==EILSEQ||errno==EINVAL)
            throw std::invalid_argument("user text is invalid UTF-8 or cannot be represented in the selected game language");
        throw std::runtime_error("iconv failed while encoding user text");
    }
    char buffer[32];char* destination=buffer;std::size_t available=sizeof(buffer);
    if(iconv(converter,nullptr,nullptr,&destination,&available)==static_cast<std::size_t>(-1))
        throw std::runtime_error("iconv could not finish encoding user text");
    append(output,buffer,sizeof(buffer)-available,output_limit);
    return output;
}
#endif
} // namespace odh_host
