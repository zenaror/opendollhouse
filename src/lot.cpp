#include "odh/lot.hpp"
#include <limits>

namespace odh {
namespace {
std::int16_t signed_half(std::uint16_t value) {
    return static_cast<std::int16_t>(value < 0x8000U ? static_cast<std::int32_t>(value)
                                                   : static_cast<std::int32_t>(value) - 0x10000);
}
std::int32_t signed_word(std::uint32_t value) {
    return static_cast<std::int32_t>(value < 0x80000000U ? static_cast<std::int64_t>(value)
                                                     : static_cast<std::int64_t>(value) - 0x100000000LL);
}
}
std::int32_t Simi::purchase_value() const {
    const auto value = static_cast<std::int64_t>(lot_value) +
        static_cast<std::int64_t>(objects_value) +
        (static_cast<std::int64_t>(architecture_value) * 7) / 10;
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        throw FormatError("SIMI purchase value exceeds signed 32-bit range");
    return static_cast<std::int32_t>(value);
}
Simi read_simi(const View& payload) {
    const auto header = payload.subview(0, 12);
    Simi result{};
    result.reserved = header.le32(0);
    result.version = header.le32(4);
    std::array<char, 4> magic{};
    header.read(8, magic.data(), magic.size());
    if (magic != std::array<char, 4>{'I','M','I','S'})
        throw FormatError("unsupported SIMI signature (IMIS profile)");
    switch (result.version) {
    case 0x33: case 0x38: case 0x3e: case 0x3f: case 0x45: case 0x48: case 0x49:
        break;
    default: throw FormatError("unsupported SIMI version (native observed profile)");
    }
    const std::size_t count = result.version <= 0x3f ? 32 : 64;
    std::uint64_t position = 12;
    const auto fixed = payload.subview(position, count * 2 + 34);
    result.global_data.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        result.global_data.push_back(signed_half(fixed.le16(i * 2)));
    position = count * 2;
    result.unknown1 = signed_half(fixed.le16(position));
    position += 2;
    auto word = [&] {
        const auto value = signed_word(fixed.le32(position));
        position += 4;
        return value;
    };
    result.unknown2 = word(); result.unknown3 = word();
    result.guid1 = word(); result.guid2 = word(); result.unknown4 = word();
    result.lot_value = word(); result.objects_value = word(); result.architecture_value = word();
    position += 12;
    for (auto& day : result.budget_days) {
        day.valid = signed_word(payload.le32(position));
        position += 4;
        if (day.valid == 0) continue;
        const auto amounts = payload.subview(position, 32);
        day.amounts = LotBudgetAmounts{
            signed_word(amounts.le32(0)), signed_word(amounts.le32(4)),
            signed_word(amounts.le32(8)), signed_word(amounts.le32(12)),
            signed_word(amounts.le32(16)), signed_word(amounts.le32(20)),
            signed_word(amounts.le32(24)), signed_word(amounts.le32(28))};
        position += 32;
    }
    result.remaining_bytes = payload.size() - position;
    return result;
}
} // namespace odh
