#include "odh/neighborhood.hpp"
#include <unordered_set>

namespace odh {
namespace {
std::int32_t signed32(std::uint32_t n) {
    return static_cast<std::int32_t>(n < 0x80000000U ? static_cast<std::int64_t>(n)
                                                  : static_cast<std::int64_t>(n) - 0x100000000LL);
}
std::int16_t signed16(std::uint16_t n) {
    return static_cast<std::int16_t>(n < 0x8000U ? static_cast<std::int32_t>(n)
                                              : static_cast<std::int32_t>(n) - 65536);
}
struct Cursor {
    const View& view;
    std::uint64_t offset = 0;
    void bound(std::uint64_t bytes) const {
        if (bytes > view.size() - offset) throw FormatError("truncated NBRS payload");
    }
    std::uint8_t u8() { bound(1); auto n=view.u8(offset); ++offset; return n; }
    std::uint16_t u16() { bound(2); auto n=view.le16(offset); offset+=2; return n; }
    std::uint32_t u32() { bound(4); auto n=view.le32(offset); offset+=4; return n; }
    std::int32_t i32() { return signed32(u32()); }
};
void charge(std::uint64_t& used, std::uint64_t amount, std::uint64_t limit) {
    if (used > limit || amount > limit-used) throw FormatError("NBRS cumulative budget exceeded");
    used += amount;
}
std::uint32_t count(Cursor& c) {
    const auto n=c.i32();
    if (n < 0) throw FormatError("negative NBRS count");
    return static_cast<std::uint32_t>(n);
}
}
Nbrs read_nbrs(const View& view, const NbrsLimits& limits) {
    if (view.size() > limits.payload_bytes) throw FormatError("NBRS payload exceeds configured limit");
    Cursor c{view};
    Nbrs result{};
    result.reserved=c.u32(); result.version=c.u32();
    if (result.version != 62 && result.version != 63 && result.version != 73)
        throw FormatError("unsupported NBRS container version");
    if (c.u32() != 0x4e425253U) throw FormatError("unsupported NBRS signature (SRBN profile)");
    const auto entries=count(c);
    if (entries > limits.entries || entries > (view.size()-c.offset)/4)
        throw FormatError("NBRS entry count exceeds payload or configured limit");
    result.entries.reserve(entries);
    std::uint64_t names=0, decoded=0, relations=0, values=0;
    for (std::uint32_t i=0;i<entries;++i) {
        NeighborEntry entry{c.i32(),std::nullopt};
        if (entry.marker == 0) { result.entries.push_back(std::move(entry)); continue; }
        if (entry.marker != 1) throw FormatError("unsupported NBRS entry marker");
        NeighborRecord r{};
        r.version=c.i32();
        if (r.version != 4 && r.version != 10) throw FormatError("unsupported NBRS record version");
        if (r.version == 10) r.unknown3=c.i32();
        // Scan and budget the complete name before allocating its storage.
        const auto start=c.offset;
        std::uint64_t length=0;
        while (c.u8() != 0) {
            ++length;
            if (length > limits.name_bytes || names > limits.total_name_bytes ||
                length > limits.total_name_bytes-names || decoded > limits.decoded_bytes ||
                length > limits.decoded_bytes-decoded)
                throw FormatError("NBRS name exceeds configured budget");
        }
        charge(names,length,limits.total_name_bytes); charge(decoded,length,limits.decoded_bytes);
        r.name.resize(static_cast<std::size_t>(length));
        view.read(start,r.name.data(),r.name.size());
        if (length % 2 == 0) r.name_padding=c.u8();
        r.mystery_zero=c.i32(); r.person_mode=c.i32();
        if (r.person_mode > 0) {
            const std::size_t words = r.version == 4 ? 80 : 256;
            c.bound(words*2); charge(decoded,words*2,limits.decoded_bytes);
            r.person_data.emplace(); r.person_data->reserve(words);
            for (std::size_t word=0;word<words;++word) r.person_data->push_back(signed16(c.u16()));
        }
        r.neighbor_id=signed16(c.u16()); r.guid=c.u32(); r.unknown_neg_one=c.i32();
        const auto relation_count=count(c);
        if (relation_count > (view.size()-c.offset)/12)
            throw FormatError("NBRS relationship count exceeds payload");
        charge(relations,relation_count,limits.relationships);
        r.relationships.reserve(relation_count);
        std::unordered_set<std::int32_t> keys;
        for (std::uint32_t relation=0;relation<relation_count;++relation) {
            NeighborRelationship rel{};
            rel.key_count=c.i32();
            if (rel.key_count != 1) throw FormatError("unsupported NBRS relationship key count");
            rel.key=c.i32();
            if (!keys.insert(rel.key).second) throw FormatError("ambiguous duplicate NBRS relationship key");
            const auto value_count=count(c);
            c.bound(static_cast<std::uint64_t>(value_count)*4);
            charge(values,value_count,limits.relationship_values);
            charge(decoded,static_cast<std::uint64_t>(value_count)*4,limits.decoded_bytes);
            rel.values.reserve(value_count);
            for (std::uint32_t value=0;value<value_count;++value) rel.values.push_back(c.i32());
            r.relationships.push_back(std::move(rel));
        }
        entry.record=std::move(r); result.entries.push_back(std::move(entry));
    }
    result.remaining_bytes=view.size()-c.offset;
    return result;
}
} // namespace odh
