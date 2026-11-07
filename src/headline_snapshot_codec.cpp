#include "odh/headline_snapshot_codec.hpp"
#include "odh/formats.hpp"

namespace odh {
namespace {
constexpr std::size_t state_bytes=37,header_bytes=8;
void put(std::vector<std::uint8_t>& bytes,std::uint64_t value,unsigned width) {
    for(unsigned i=0;i<width;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));
}
std::uint64_t get(const std::vector<std::uint8_t>& bytes,std::size_t& at,unsigned width) {
    std::uint64_t value=0;
    for(unsigned i=0;i<width;++i)value|=static_cast<std::uint64_t>(bytes[at++])<<(i*8);
    return value;
}
std::int32_t i32(std::uint64_t raw) {
    return static_cast<std::int32_t>(raw<=0x7fffffffU ? static_cast<std::int64_t>(raw)
        : static_cast<std::int64_t>(raw)-0x100000000LL);
}
std::int16_t i16(std::uint64_t raw) {return static_cast<std::int16_t>(raw<32768?static_cast<int>(raw):static_cast<int>(raw)-65536);}
}
std::vector<std::uint8_t> encode_balloon_headline_snapshot(const BalloonHeadlineSnapshot& state) {
    if(!valid_balloon_headline_snapshot(state))throw FormatError("invalid headline snapshot");
    std::vector<std::uint8_t> bytes;bytes.reserve(header_bytes+state_bytes*state.size());
    put(bytes,1,4);put(bytes,state.size(),4);
    for(const auto& s:state) {
        bytes.insert(bytes.end(),s.operand.begin(),s.operand.end());
        put(bytes,static_cast<std::uint16_t>(s.target),2);put(bytes,s.target_generation,8);
        put(bytes,static_cast<std::uint16_t>(s.icon_target),2);put(bytes,s.icon_generation,8);
        put(bytes,static_cast<std::uint8_t>(s.index),1);
        put(bytes,static_cast<std::uint32_t>(s.duration),4);put(bytes,static_cast<std::uint32_t>(s.anim),4);
    }
    return bytes;
}
BalloonHeadlineSnapshot decode_balloon_headline_snapshot(const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()<header_bytes||bytes.size()>header_bytes+4096*state_bytes)
        throw FormatError("headline snapshot byte budget or header invalid");
    std::size_t at=0;
    if(get(bytes,at,4)!=1)throw FormatError("unsupported headline snapshot version");
    const auto count=get(bytes,at,4);
    if(count>4096||bytes.size()!=header_bytes+count*state_bytes)
        throw FormatError("headline snapshot count or trailing bytes invalid");
    BalloonHeadlineSnapshot result;result.reserve(static_cast<std::size_t>(count));
    for(std::uint64_t n=0;n<count;++n) {
        BalloonHeadlineState s;
        for(auto& b:s.operand)b=bytes[at++];
        s.target=i16(get(bytes,at,2));s.target_generation=get(bytes,at,8);
        s.icon_target=i16(get(bytes,at,2));s.icon_generation=get(bytes,at,8);
        const auto index=get(bytes,at,1);
        s.index=static_cast<std::int8_t>(index<128?static_cast<int>(index):static_cast<int>(index)-256);
        s.duration=i32(get(bytes,at,4));s.anim=i32(get(bytes,at,4));result.push_back(s);
    }
    if(!valid_balloon_headline_snapshot(result))throw FormatError("invalid headline snapshot fields");
    return result;
}
} // namespace odh
