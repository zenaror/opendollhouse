#include "odh/objm.hpp"

#include <limits>

namespace odh {
namespace {
// IffFieldEncode consumes MSB-first fields; it does not apply delta coding.
// Keeping an absolute bit cursor avoids the reference's byte prefetch. Its
// Interrupt rewinds a prefetched byte only at bitPos == 0; the equivalent
// checked byte cursor here is ceil(bit_position / 8), even at exact boundaries.
class MapFields {
public:
    explicit MapFields(const View& view) : view_(view) {}
    std::uint16_t word() {
        if (bits(1) == 0) return 0;
        constexpr std::array<unsigned, 4> widths{5, 8, 13, 16};
        const auto width = widths[bits(2)];
        const auto value = bits(width);
        const auto signed_value = (value & (std::uint32_t{1} << (width - 1)))
            ? static_cast<std::int32_t>(value) - (std::int32_t{1} << width)
            : static_cast<std::int32_t>(value);
        return static_cast<std::uint16_t>(signed_value);
    }
    std::uint64_t interrupt() const { return (bit_position_ + 7) / 8; }
private:
    std::uint32_t bits(unsigned count) {
        if (count > view_.size() * 8 - bit_position_)
            throw FormatError("truncated OBJM compressed ID table");
        std::uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i) {
            if ((bit_position_ & 7U) == 0) cached_byte_ = view_.u8(bit_position_ / 8);
            value = (value << 1) | ((cached_byte_ >> (7 - (bit_position_ & 7U))) & 1U);
            ++bit_position_;
        }
        return value;
    }
    const View& view_;
    std::uint64_t bit_position_ = 13 * 8; // Immediately after compression byte.
    std::uint8_t cached_byte_ = 0;
};
}

Objm read_objm(const View& payload, const ObjmLimits& limits) {
    if (payload.size() > limits.payload_bytes ||
        payload.size() > (std::numeric_limits<std::uint64_t>::max() - 7) / 8)
        throw FormatError("OBJM payload exceeds byte limit");
    const auto header = payload.subview(0, 13);
    Objm result{};
    result.reserved = header.le32(0);
    result.version = header.le32(4);
    if (header.le32(8) != 0x4f626a4dU)
        throw FormatError("unsupported OBJM signature (MjbO container)");
    result.compression = header.u8(12);
    if (result.compression != 1)
        throw FormatError("unsupported OBJM compression (expected 1)");

    MapFields fields(payload);
    for (;;) {
        const auto id = fields.word();
        if (id == 0) break;
        if (result.map_records.size() >= limits.object_count)
            throw FormatError("OBJM object count exceeds limit");
        const auto type = fields.word();
        if (!result.id_to_objt.emplace(id, type).second)
            throw FormatError("duplicate OBJM object ID in bounded container profile");
        result.map_records.push_back({id, type});
    }

    auto position = fields.interrupt();
    const auto count = result.id_to_objt.size();
    // Check the mandatory skip words before reserving, regardless of caller's
    // object_count. No allocation uses an on-disk count or a skipOffset value.
    if (count > (payload.size() - position) / 4)
        throw FormatError("truncated OBJM block skip words");
    result.blocks.reserve(count);
    std::uint64_t owned_bytes = 0;
    constexpr std::uint64_t offset_base = 12;
    for (std::size_t i = 0; i < count; ++i) {
        const auto skip = payload.le32(position);
        position += 4;
        if (skip > 0x7fffffffU)
            throw FormatError("negative OBJM block skip offset");
        const auto end = offset_base + static_cast<std::uint64_t>(skip);
        if (end < position || end > payload.size())
            throw FormatError("OBJM block skip offset backwards or outside payload");
        const auto length = end - position;
        if (length > limits.owned_block_bytes - owned_bytes)
            throw FormatError("OBJM owned block bytes exceed limit");
        ObjmBlock block{static_cast<std::uint32_t>(position - offset_base), {}};
        if (length > block.bytes.max_size())
            throw FormatError("OBJM block exceeds vector capacity");
        block.bytes.resize(static_cast<std::size_t>(length));
        if (length) payload.read(position, block.bytes.data(), block.bytes.size());
        owned_bytes += length;
        result.blocks.push_back(std::move(block));
        position = end;
    }
    result.remaining_bytes = payload.size() - position;
    return result;
}
} // namespace odh
