// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/formats.hpp"
#include <charconv>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

namespace odh {
// ConvertAnimation at exact FreeSO7e76 consumes fields0/2/3/4/5, requires
// at least six, and ignores remaining fields. Diagnostic integers are optional.
struct OriginalAnimationRecord {
    std::string name;
    std::int32_t priority=0,speed=0,normalized_frame=0,weight=0,unknown1=0,unknown2=0;
    bool loop=false;
};
inline OriginalAnimationRecord parse_original_animation_record(const std::string& raw){
    if(raw.empty()||raw.size()>4096||raw.find('\0')!=std::string::npos)
        throw FormatError("original animation string exceeds bounded profile");
    std::vector<std::string> fields;std::size_t first=0;
    for(;;){const auto end=raw.find(';',first);fields.push_back(raw.substr(first,end==std::string::npos?end:end-first));
        if(end==std::string::npos)break;first=end+1;}
    if(fields.size()<6||fields[0].empty())throw FormatError("original animation requires six source fields");
    const auto integer=[&](unsigned n,bool required){
        std::int32_t value=0;if(n>=fields.size())return value;
        auto text=fields[n];const auto begin=text.find_first_not_of(" \t\r\n"),end=text.find_last_not_of(" \t\r\n");
        if(begin!=std::string::npos)text=text.substr(begin,end-begin+1);else text.clear();
        if(!text.empty()&&text[0]=='+'){text.erase(0,1);if(!text.empty()&&(text[0]=='+'||text[0]=='-'))text.clear();}
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()){
            if(required)throw FormatError("invalid original animation integer");return std::int32_t{0};}
        return value;
    };
    OriginalAnimationRecord result;result.name=fields[0];result.priority=integer(1,false);
    result.speed=integer(2,true);result.normalized_frame=integer(3,true);result.weight=integer(4,true);result.loop=fields[5]=="1";
    result.unknown1=integer(6,false);result.unknown2=integer(7,false);
    if(result.speed==INT32_MIN)throw FormatError("original animation Math.Abs(Int32.MinValue) throws in source");
    return result;
}
} // namespace odh
