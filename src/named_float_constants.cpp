// SPDX-License-Identifier: MPL-2.0
#include "odh/named_float_constants.hpp"
#include "read_cursor.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace odh {
NamedFloatConstants read_named_float_constants(const View& view,NamedFloatLimits limits){
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"FCNS requires IEEE754 float32");
    if(view.size()>limits.bytes||view.size()<16)throw FormatError("FCNS payload byte budget/header invalid");
    detail::Cursor input(view,limits.text_bytes);
    if(input.u32()!=0||input.u32()!=1||input.u32()!=0x46434e53u)
        throw FormatError("FCNS header outside selected version1 SNCF profile");
    const auto count=input.u32();
    if(count>limits.records||count>input.remaining()/8)throw FormatError("FCNS record count exceeds bounds");
    NamedFloatConstants out;out.records.reserve(count);std::set<std::string> names;
    const auto text=[&]{auto value=input.terminated();
        if(value.size()%2==0&&input.byte()!=0)throw FormatError("FCNS string alignment padding is nonzero");
        return value;};
    for(std::uint32_t i=0;i<count;++i){
        NamedFloatConstant record;record.name=text();
        if(record.name.empty()||!names.insert(record.name).second)throw FormatError("FCNS name empty or duplicated");
        const auto bits=input.u32();std::memcpy(&record.value,&bits,4);
        if(!std::isfinite(record.value))throw FormatError("FCNS named float is nonfinite");
        record.description=text();out.records.push_back(std::move(record));
    }
    if(input.remaining())throw FormatError("FCNS payload has trailing bytes");
    return out;
}
}
