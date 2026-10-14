#include "odh/inventory.hpp"

namespace odh {
namespace {
std::int32_t signed32(std::uint32_t n) {
    return static_cast<std::int32_t>(n < 0x80000000U ? static_cast<std::int64_t>(n)
                                                  : static_cast<std::int64_t>(n)-0x100000000LL);
}
std::int16_t signed16(std::uint16_t n) {
    return static_cast<std::int16_t>(n < 0x8000U ? static_cast<std::int32_t>(n)
                                               : static_cast<std::int32_t>(n)-65536);
}
struct Cursor {
    const View& view;
    std::uint64_t offset = 0;
    void bound(std::uint64_t bytes) const {
        if (bytes > view.size()-offset) throw FormatError("truncated NGBH payload");
    }
    std::uint16_t u16() { bound(2); const auto n=view.le16(offset); offset+=2; return n; }
    std::uint32_t u32() { bound(4); const auto n=view.le32(offset); offset+=4; return n; }
    std::int32_t i32() { return signed32(u32()); }
    std::uint32_t count() {
        const auto n=i32();
        if (n < 0) throw FormatError("negative NGBH count");
        return static_cast<std::uint32_t>(n);
    }
};
}
Ngbh read_ngbh(const View& view, const NgbhLimits& limits) {
    if (view.size() > limits.payload_bytes) throw FormatError("NGBH payload exceeds configured limit");
    Cursor c{view}; c.bound(44);
    Ngbh result{};
    result.reserved=c.u32(); result.version=c.u32();
    view.read(c.offset,result.magic.data(),result.magic.size()); c.offset+=4;
    for (auto& word:result.neighborhood_data) word=signed16(c.u16());
    if (c.offset == view.size()) return result;
    result.inventory_present=true;
    const auto entries=c.count();
    if (entries > limits.entries || entries > (view.size()-c.offset)/10)
        throw FormatError("NGBH entry count exceeds payload or configured limit");
    std::uint64_t total_items=0;
    for (std::uint32_t i=0;i<entries;++i) {
        const auto marker=c.i32(); const auto id=signed16(c.u16()); const auto items=c.count();
        c.bound(static_cast<std::uint64_t>(items)*10);
        if (total_items > limits.inventory_items || items > limits.inventory_items-total_items)
            throw FormatError("NGBH cumulative inventory item budget exceeded");
        total_items+=items;
        std::vector<InventoryItem> list; list.reserve(items);
        for (std::uint32_t j=0;j<items;++j) {
            const auto type=c.i32(); const auto guid=c.u32(); const auto count=c.u16();
            list.push_back({type,guid,count});
        }
        result.inventory_by_id[id]=std::move(list);
        result.inventory_markers[id]=marker;
    }
    result.remaining_bytes=view.size()-c.offset;
    return result;
}
}
