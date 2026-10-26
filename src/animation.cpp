#include "odh/animation.hpp"
#include "read_cursor.hpp"
#include <cmath>
#include <cstring>
#include <limits>
namespace odh {
StringTable read_strings(const View& view,AnimationLimits limits) {
    StringTable result{StringTableState::absent,{}, {},view.size()};
    if(!view.size())return result;
    detail::Cursor c(view,limits.text_bytes);const auto raw=c.u16();
    const auto format=static_cast<std::int16_t>(raw<32768?static_cast<std::int32_t>(raw):static_cast<std::int32_t>(raw)-65536);
    result.format=format;
    if(format>0 || format<-3)throw FormatError("unsupported string table format (0..-3 profile)");
    if(c.remaining()==0){result.state=StringTableState::header_only;result.remaining_bytes=0;return result;}
    const auto count=c.u16();const auto minimum=format==0?1U:format==-1?1U:format==-2?2U:3U;
    if(count>limits.strings || count>c.remaining()/minimum)throw FormatError("string count exceeds payload or limit");
    result.entries.reserve(count);
    for(std::uint32_t n=0;n<count;++n){StringEntry entry{1,{}, {}};
        if(format==-3)entry.language=c.byte();
        entry.value=format==0?c.pascal():c.terminated();
        if(format<=-2)entry.comment=c.terminated();
        result.entries.push_back(std::move(entry));
    }
    result.state=StringTableState::decoded;result.remaining_bytes=c.remaining();return result;
}
const StringEntry* string_in_language(const StringTable& table,std::uint8_t language,std::size_t index) {
    if(language==0)language=1;
    for(const auto& entry:table.entries){const auto code=entry.language==0?1:entry.language;
        if(code!=language)continue;
        if(index==0)return &entry;
        --index;}
    return nullptr;
}
const StringEntry* string_in_ts1_language_set(const StringTable& table,std::uint8_t language,std::size_t index) {
    // A flat table represents an absent and an empty language set identically.
    // Test set membership before indexing: an empty string is still an entry.
    const auto selected=string_in_language(table,language,0)?language:std::uint8_t{1};
    return string_in_language(table,selected,index);
}
const StringEntry* string_in_language_or_english(const StringTable& table,std::uint8_t language,std::size_t index) {
    if(const auto* selected=string_in_language(table,language,index))return selected;
    if(language==0||language==1)return nullptr;
    return string_in_language(table,1,index);
}
namespace {
std::uint32_t bounded(detail::Cursor& c,std::uint32_t& budget,std::uint32_t minimum){const auto n=c.u32();
    if(n>budget || n>c.remaining()/minimum)throw FormatError("animation count exceeds payload or configured budget");
    budget-=n;return n;}
AnimationProperties pairs(detail::Cursor& c,std::uint32_t& budget) {
    const auto n=bounded(c,budget,2);AnimationProperties result;result.reserve(n);
    for(std::uint32_t i=0;i<n;++i){auto key=c.pascal();auto value=c.pascal();result.emplace_back(std::move(key),std::move(value));}return result;
}
}
AnimationCollection read_bcf_animations(const View& view,AnimationLimits limits) {
    detail::Cursor c(view,limits.text_bytes);
    if(c.u32()!=0)throw FormatError("BCF skeleton section outside animation-only profile");
    if(c.u32()!=0)throw FormatError("BCF appearance section outside animation-only profile");
    const auto count=bounded(c,limits.animations,26);AnimationCollection result{{},0};result.animations.reserve(count);
    for(std::uint32_t n=0;n<count;++n){AnimationHeader a{};a.name=c.pascal();a.xskill=c.pascal();
        a.duration_bits=c.u32();a.distance_bits=c.u32();a.moving=c.u32();a.translations=c.u32();a.rotations=c.u32();
        if(a.translations>limits.samples || a.rotations>limits.samples)throw FormatError("animation sample count exceeds configured limit");
        const auto motions=bounded(c,limits.motions,33);a.motions.reserve(motions);
        for(std::uint32_t m=0;m<motions;++m){BoneMotion b{};b.bone=c.pascal();b.frames=c.u32();b.duration_bits=c.u32();
            b.has_translation=c.u32();b.has_rotation=c.u32();b.first_translation=c.i32();b.first_rotation=c.i32();
            if(b.has_translation>1 || b.has_rotation>1 || b.frames>limits.samples)throw FormatError("unsupported animation motion flags or frame limit");
            for(const auto active:{true,false}){const auto present=active?b.has_translation:b.has_rotation;
                const auto first=active?b.first_translation:b.first_rotation;const auto total=active?a.translations:a.rotations;
                if(present && (first<0 || static_cast<std::uint32_t>(first)>total || b.frames>total-static_cast<std::uint32_t>(first)))
                    throw FormatError("animation motion sample range outside declared arrays");}
            const auto props=bounded(c,limits.properties,4);b.properties.reserve(props);
            for(std::uint32_t p=0;p<props;++p)b.properties.push_back(pairs(c,limits.properties));
            const auto groups=bounded(c,limits.properties,4);b.time_properties.reserve(groups);
            for(std::uint32_t g=0;g<groups;++g){const auto events=bounded(c,limits.properties,8);std::vector<AnimationTimeProperty> group;group.reserve(events);
                for(std::uint32_t e=0;e<events;++e){const auto id=c.i32();group.push_back({id,pairs(c,limits.properties)});}b.time_properties.push_back(std::move(group));}
            a.motions.push_back(std::move(b));}
        result.animations.push_back(std::move(a));}
    result.remaining_bytes=c.remaining();return result;
}
CfpPlanes read_cfp(const View& view,std::uint32_t translations,std::uint32_t rotations,AnimationLimits limits) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"CFP requires IEEE-754 binary32");
    const auto samples=static_cast<std::uint64_t>(translations)*3+static_cast<std::uint64_t>(rotations)*4;
    if(samples>limits.samples)throw FormatError("CFP decoded sample budget exceeded");
    // Each nonempty plane needs at least one code. Check before vector allocation.
    const auto planes=(translations?3U:0U)+(rotations?4U:0U);
    if(view.size()<planes)throw FormatError("CFP payload too short for declared planes");
    detail::Cursor c(view,0);CfpPlanes result{{},0};
    for(std::size_t plane=0;plane<7;++plane){const auto count=plane<3?translations:rotations;
        auto& values=result.bits[plane];values.reserve(count);float last=0;
        while(values.size()<count){const auto code=c.byte();std::uint32_t run=1;
            if(code==0xFF){const auto raw=c.u32();std::memcpy(&last,&raw,4);}
            else if(code==0xFE){run=static_cast<std::uint32_t>(c.u16())+1;}
            else if(code==0xFD)throw FormatError("reserved CFP delta code FD");
            else {const std::int64_t n=static_cast<int>(code)-126;const auto product=n*n*n*(n<0?-n:n);
                const auto delta=static_cast<float>(3.9676e-10*static_cast<double>(product));last=static_cast<float>(last+delta);}
            if(!std::isfinite(last))throw FormatError("nonfinite CFP transform value");
            if(run>count-values.size())throw FormatError("CFP repeat crosses declared plane boundary");
            std::uint32_t bits;std::memcpy(&bits,&last,4);values.insert(values.end(),run,bits);
        }}
    result.remaining_bytes=c.remaining();return result;
}
}
