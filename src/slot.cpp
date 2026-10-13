#include "odh/formats.hpp"

namespace odh {
namespace {
std::int32_t signed_word(std::uint32_t n) {
    return static_cast<std::int32_t>(n < 0x80000000U ? static_cast<std::int64_t>(n)
                                                  : static_cast<std::int64_t>(n) - 0x100000000LL);
}
}
SlotTable read_slot(const View& payload, const Limits& limits) {
    const auto header = payload.subview(0,16);
    SlotTable table{header.le32(0),header.le32(4),{},0};
    std::array<char,4> magic{}; header.read(8,magic.data(),magic.size());
    if (magic != std::array<char,4>{'T','O','L','S'}) throw FormatError("unsupported SLOT signature (TOLS profile)");
    if (table.version < 4 || table.version > 10) throw FormatError("unsupported SLOT version (4..10 profile)");
    const std::uint64_t span = table.version < 6 ? 34 : 54 + (table.version - 6) * 4;
    const auto count = header.le32(12);
    if (count > limits.slot_records || count > (payload.size()-16)/span)
        throw FormatError("SLOT record count exceeds payload or configured limit");
    table.records.reserve(count);
    for (std::uint32_t n=0;n<count;++n) {
        const auto r = payload.subview(16 + n*span,span);
        SlotRecord item{};
        item.type = r.le16(0);
        for (std::size_t axis=0;axis<3;++axis) item.offset_bits[axis] = r.le32(2+axis*4);
        item.standing = signed_word(r.le32(14)); item.sitting = signed_word(r.le32(18));
        item.ground = signed_word(r.le32(22)); item.flags = r.le32(26);
        item.snap_target_slot = signed_word(r.le32(30));
        if (table.version >= 6) {
            item.proximity = std::array<std::int32_t,3>{signed_word(r.le32(34)),signed_word(r.le32(38)),signed_word(r.le32(42))};
            item.max_size = signed_word(r.le32(46)); item.unknown_i10 = signed_word(r.le32(50));
        }
        if (table.version >= 7) item.gradient_bits = r.le32(54);
        if (table.version >= 8) item.height = signed_word(r.le32(58));
        if (table.version >= 9) item.facing = signed_word(r.le32(62));
        if (table.version >= 10) item.resolution = signed_word(r.le32(66));
        table.records.push_back(std::move(item));
    }
    table.remaining_bytes = payload.size()-16-count*span;
    return table;
}
const SlotRecord* slot_of_type(const SlotTable& table,std::uint16_t type,std::size_t index) {
    for (const auto& item : table.records) {
        if (item.type != type) continue;
        if (index == 0) return &item;
        --index;
    }
    return nullptr;
}
} // namespace odh
