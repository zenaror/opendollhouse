#include "odh/entry_points.hpp"

#include <array>

namespace odh {

EntryPointTable read_objd_entry_points(const View& payload) {
    constexpr std::uint64_t prefix_size = 162;
    const auto prefix = payload.subview(0, prefix_size);
    const auto version = prefix.le32(0);
    if (version != 138)
        throw FormatError("unsupported OBJD entry point prefix version");
    if (prefix.le16(44) != 0)
        throw FormatError("OBJD entry points require an OBJf function table");

    // VMEntity.GenerateFunctionTable: Init through Repair, in entry order.
    // Byte offsets follow the OBJD138 on-disk words, not ObjectType at byte 18.
    constexpr std::array<std::uint16_t, 30> action_offsets = {
        86, 10, 94, 104, 120, 42, 124, 100, 156, 88,
        148, 96, 90, 106, 110, 34, 12, 24, 48, 50,
        52, 54, 56, 58, 60, 62, 64, 66, 118, 160
    };
    EntryPointTable result{version, std::nullopt, std::vector<EntryPoint>(33, {0, 0}),
                           payload.size() - prefix_size, EntryPointSource::objd_legacy};
    for (std::size_t index = 0; index < action_offsets.size(); ++index)
        result.entries[index].action = prefix.le16(action_offsets[index]);
    return result;
}

EntryPointTable read_objf(const View& payload, const EntryPointLimits& limits) {
    constexpr std::uint64_t header_size = 16;
    constexpr std::uint64_t entry_size = 4;
    if (payload.size() > limits.payload_bytes)
        throw FormatError("OBJf payload budget exceeded");
    const auto header = payload.subview(0, header_size);
    const auto reserved = header.le32(0);
    const auto version = header.le32(4);
    std::array<char, 4> magic{};
    header.read(8, magic.data(), magic.size());
    if (magic != std::array<char, 4>{'f', 'J', 'B', 'O'})
        throw FormatError("invalid OBJf signature");
    const auto count = header.le32(12);
    const auto bytes = static_cast<std::uint64_t>(count) * entry_size;
    if (count > limits.entries)
        throw FormatError("OBJf entry count budget exceeded");
    if (bytes > limits.decoded_bytes)
        throw FormatError("OBJf decoded byte budget exceeded");
    if (bytes > payload.size() - header_size)
        throw FormatError("truncated OBJf entry table");

    EntryPointTable result{version, reserved, {}, payload.size() - header_size - bytes,
                           EntryPointSource::objf};
    result.entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto offset = header_size + static_cast<std::uint64_t>(index) * entry_size;
        result.entries.push_back({payload.le16(offset), payload.le16(offset + 2)});
    }
    return result;
}

} // namespace odh
