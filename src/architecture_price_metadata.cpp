#include "odh/architecture_price_metadata.hpp"

namespace odh {
namespace {
bool ascii_space(unsigned char byte) noexcept {
    return byte==' ' || byte=='\t' || byte=='\r' || byte=='\n' || byte=='\f' || byte=='\v';
}
}
std::int32_t architecture_price_from_strings(const StringTable& table,std::size_t index,std::uint8_t language) {
    if(table.state!=StringTableState::decoded || !table.format || *table.format>0 || *table.format< -3)
        throw FormatError("architecture price requires a decoded supported STR# table");
    // Pinned FreeSO7e76c196 WorldFloorProvider/WorldWallProvider read dynamic
    // STR#0 price index1; build-global triples use explicit positions. This
    // primitive only extracts that chosen position, without resource remapping.
    const auto* entry=string_in_ts1_language_set(table,language,index);
    if(!entry)throw FormatError("architecture price string is absent in the selected language set");
    const auto& text=entry->value;
    if(text.size()>64)throw FormatError("architecture price text exceeds64 bytes");
    for(const unsigned char byte:text)if(byte>=128)
        throw FormatError("architecture price text is not ASCII decimal");
    std::size_t first=0,last=text.size();
    while(first<last && ascii_space(static_cast<unsigned char>(text[first])))++first;
    while(last>first && ascii_space(static_cast<unsigned char>(text[last-1])))--last;
    if(first==last)throw FormatError("architecture price text is empty");
    bool negative=false;
    if(text[first]=='+' || text[first]=='-'){negative=text[first]=='-';++first;}
    if(first==last)throw FormatError("architecture price has no decimal digits");
    const std::uint32_t limit=negative?2147483648U:2147483647U;
    std::uint32_t magnitude=0;
    for(;first<last;++first) {
        const auto byte=static_cast<unsigned char>(text[first]);
        if(byte<'0' || byte>'9')throw FormatError("architecture price is not signed decimal");
        const auto digit=static_cast<std::uint32_t>(byte-'0');
        if(magnitude>(limit-digit)/10U)throw FormatError("architecture price is outside signed32 range");
        magnitude=magnitude*10U+digit;
    }
    const auto signed_value=negative?-static_cast<std::int64_t>(magnitude):static_cast<std::int64_t>(magnitude);
    return static_cast<std::int32_t>(signed_value);
}
} // namespace odh
