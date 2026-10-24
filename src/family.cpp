#include "odh/family.hpp"
#include <algorithm>

namespace odh {
namespace {
std::int32_t signed32(std::uint32_t n) {
    return static_cast<std::int32_t>(n < 0x80000000U ? static_cast<std::int64_t>(n)
                                                  : static_cast<std::int64_t>(n)-0x100000000LL);
}
std::int16_t house_key(std::int32_t n) {
    const auto low = static_cast<std::uint32_t>(n) & 0xffffU;
    return static_cast<std::int16_t>(low < 0x8000U ? static_cast<std::int32_t>(low)
                                                : static_cast<std::int32_t>(low)-65536);
}
}
Family read_fami(const View& view, const FamiLimits& limits) {
    if (view.size() > limits.payload_bytes) throw FormatError("FAMI payload exceeds configured limit");
    if (view.size() < 40) throw FormatError("truncated FAMI prefix");
    Family f;
    f.reserved=view.le32(0); f.version=view.le32(4);
    if (view.le32(8) != 0x46414d49U) throw FormatError("unsupported FAMI signature (IMAF profile)");
    f.house_number=signed32(view.le32(12)); f.family_number=signed32(view.le32(16));
    f.budget=signed32(view.le32(20)); f.value_in_arch=signed32(view.le32(24));
    f.family_friends=signed32(view.le32(28)); f.unknown=signed32(view.le32(32));
    const auto count=signed32(view.le32(36));
    if (count < 0) throw FormatError("negative FAMI member count");
    const auto members=static_cast<std::uint32_t>(count);
    if (members > limits.members || members > (view.size()-40)/4)
        throw FormatError("FAMI member count exceeds payload or configured limit");
    f.member_guids.reserve(members);
    for (std::uint32_t i=0;i<members;++i) f.member_guids.push_back(view.le32(40+4*static_cast<std::uint64_t>(i)));
    f.remaining_bytes=view.size()-40-4*static_cast<std::uint64_t>(members);
    return f;
}
std::vector<std::uint8_t> encode_fami(const Family& family, const FamiLimits& limits) {
    if (family.remaining_bytes != 0)
        throw FormatError("cannot encode FAMI with unpreserved tail");
    if (family.member_guids.size() > limits.members || family.member_guids.size() > 0x7fffffffU ||
        limits.payload_bytes < 40 || family.member_guids.size() > (limits.payload_bytes-40)/4)
        throw FormatError("FAMI encoding exceeds configured limit");
    if (family.member_guids.size() > (std::vector<std::uint8_t>().max_size()-40)/4)
        throw FormatError("FAMI encoding cannot fit in memory");
    std::vector<std::uint8_t> out;
    out.reserve(40+4*family.member_guids.size());
    const auto put=[&](std::uint32_t value) {
        for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(value>>(8*i)));
    };
    put(family.reserved);put(family.version);put(0x46414d49U);
    put(static_cast<std::uint32_t>(family.house_number));put(static_cast<std::uint32_t>(family.family_number));
    put(static_cast<std::uint32_t>(family.budget));put(static_cast<std::uint32_t>(family.value_in_arch));
    put(static_cast<std::uint32_t>(family.family_friends));put(static_cast<std::uint32_t>(family.unknown));
    put(static_cast<std::uint32_t>(family.member_guids.size()));
    for(const auto guid:family.member_guids)put(guid);
    return out;
}
FamilyContext::FamilyContext(const std::vector<FamilyEntry>& source) {
    if (source.size() > 4096) throw FormatError("family runtime exceeds bounded 4096 entries");
    std::size_t members=65536;
    // Validate before copying any owned member arrays or allocating indexes.
    for (const auto& entry:source) {
        const auto size=entry.family.member_guids.size();
        if (size > 256 || size > members) throw FormatError("family runtime member budget exceeded");
        members-=size;
    }
    for (std::size_t i=0;i<source.size();++i) {
        if (!ids_.emplace(source[i].chunk_id,i).second) throw FormatError("ambiguous duplicate family chunk ID");
        houses_[house_key(source[i].family.house_number)]=i;
    }
    source_=source;
}
const FamilyEntry* FamilyContext::by_house(std::int16_t house) const {
    const auto found=houses_.find(house);
    return found==houses_.end()?nullptr:&source_[found->second];
}
const FamilyEntry* FamilyContext::by_id(std::uint16_t id) const {
    const auto found=ids_.find(id);
    return found==ids_.end()?nullptr:&source_[found->second];
}
bool FamilyContext::contains(const FamilyEntry& entry,std::uint32_t guid) {
    const auto& members=entry.family.member_guids;
    return std::find(members.begin(),members.end(),guid)!=members.end();
}
} // namespace odh
