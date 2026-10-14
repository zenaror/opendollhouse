#include "odh/object_types.hpp"

namespace odh {
const ObjtEntry* Objt::entry_for_type(std::uint16_t type) const {
    if (type == 0 || type > entries.size()) return nullptr;
    return &entries[static_cast<std::size_t>(type)-1];
}
Objt read_objt(const View& view, const ObjtLimits& limits) {
    if (view.size() > limits.payload_bytes)
        throw FormatError("OBJT payload exceeds configured limit");
    std::uint64_t offset=0;
    auto bound=[&](std::uint64_t n) {
        if (n > view.size()-offset) throw FormatError("truncated OBJT entry");
    };
    auto u16=[&]() {bound(2);const auto n=view.le16(offset);offset+=2;return n;};
    auto u32=[&]() {bound(4);const auto n=view.le32(offset);offset+=4;return n;};
    bound(12);
    Objt result{};result.reserved=u32();result.version=u32();
    view.read(offset,result.magic.data(),result.magic.size());offset+=4;
    // ReadInt32 in the reference: raw versions with bit31 set are negative.
    const bool has_extra=result.version > 2 && result.version < 0x80000000U;
    std::uint64_t names=0;
    while (offset < view.size()) {
        if (result.entries.size() >= limits.entries)
            throw FormatError("OBJT entry count exceeds configured limit");
        ObjtEntry entry{};entry.guid=u32();
        if (entry.guid != 0) {
            bound(12);
            ObjtMetadata metadata{};
            metadata.unknown1a=u16();metadata.init_tree_version=u16();
            metadata.unknown2a=u16();metadata.main_tree_version=u16();
            metadata.type_id=u16();metadata.objd_type=u16();
            entry.metadata=metadata;
            const auto start=offset;
            // Validate the terminator, per-name and cumulative budgets, padding
            // and version extension before allocating any owned string bytes.
            while (true) {
                bound(1);
                if (view.u8(offset)==0) break;
                const auto length=offset-start;
                if (length >= limits.name_length || names >= limits.name_bytes ||
                    length >= limits.name_bytes-names)
                    throw FormatError("OBJT name budget exceeded");
                ++offset;
            }
            const auto length=offset-start;
            ++offset; // NUL
            if ((length&1U)==0) {bound(1);entry.name_padding=view.u8(offset++);}
            if (has_extra) entry.version_extra=u32();
            entry.name.resize(static_cast<std::size_t>(length));
            view.read(start,entry.name.data(),entry.name.size());
            names+=length;
        }
        result.entries.push_back(std::move(entry));
    }
    return result;
}
}
