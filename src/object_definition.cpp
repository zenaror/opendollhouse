#include "odh/object_definition.hpp"

namespace odh {

ObjectDefinition read_object_definition(const View& payload) {
    const auto version = payload.le32(0);
    // Both profiles share this prefix in OBJD.cs at pinned FreeSO revision
    // 4c6b3e8f5835b228723caea3c9f683c62f244f73. Their later field counts differ.
    if (version != 136 && version != 138)
        throw FormatError("unsupported OBJD metadata version (requires 136 or 138)");
    constexpr std::uint64_t extent = 32;
    const auto prefix = payload.subview(0, extent);
    const auto sub = prefix.le16(22);
    const auto signed_sub = static_cast<std::int16_t>(
        sub < 32768 ? static_cast<std::int32_t>(sub)
                    : static_cast<std::int32_t>(sub) - 65536);
    const auto master = prefix.le16(20);
    return {version, prefix.le32(28), prefix.le16(18), master, signed_sub,
            master != 0, extent, payload.size() - extent, prefix.le16(14)};
}

} // namespace odh
