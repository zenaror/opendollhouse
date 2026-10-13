#include "odh/formats.hpp"

#include <limits>

namespace odh {
namespace {
// Compressed fields are MSB-first: zero bit, or one + 2-bit width selector +
// signed payload. Width tables differ for 16- and 32-bit output fields.
class Fields {
public:
    Fields(const View& view, std::uint64_t start, bool compressed)
        : view_(view), bit_position_(start * 8), compressed_(compressed) {
        view_.subview(start, 0);
    }
    std::uint16_t u16() {
        if (compressed_) return static_cast<std::uint16_t>(field({5, 8, 13, 16}));
        const auto value = view_.le16(bit_position_ / 8);
        bit_position_ += 16;
        return value;
    }
    std::uint32_t u32() {
        if (compressed_) return field({6, 11, 21, 32});
        const auto value = view_.le32(bit_position_ / 8);
        bit_position_ += 32;
        return value;
    }
    std::uint64_t remaining() const { return view_.size() * 8 - bit_position_; }
private:
    std::uint32_t bits(unsigned count) {
        if (count > remaining()) throw FormatError("truncated TTAB compressed field");
        std::uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i) {
            if ((bit_position_ & 7) == 0) cached_byte_ = view_.u8(bit_position_ / 8);
            value = (value << 1) | ((cached_byte_ >> (7 - (bit_position_ & 7))) & 1);
            ++bit_position_;
        }
        return value;
    }
    std::uint32_t field(const std::array<unsigned, 4>& widths) {
        if (bits(1) == 0) return 0;
        const auto width = widths[bits(2)];
        const auto value = bits(width);
        const auto signed_value = (value & (std::uint32_t{1} << (width - 1)))
            ? static_cast<std::int64_t>(value) - (std::int64_t{1} << width)
            : static_cast<std::int64_t>(value);
        return static_cast<std::uint32_t>(signed_value);
    }
    const View& view_;
    std::uint64_t bit_position_;
    bool compressed_;
    std::uint8_t cached_byte_ = 0;
};
std::int16_t signed16(std::uint16_t value) {
    return static_cast<std::int16_t>(value < 0x8000 ? static_cast<std::int32_t>(value)
                                                  : static_cast<std::int32_t>(value) - 0x10000);
}
std::int32_t signed32(std::uint32_t value) {
    return static_cast<std::int32_t>(value < 0x80000000U ? static_cast<std::int64_t>(value)
                      : static_cast<std::int64_t>(value) - 0x100000000LL);
}
} // namespace

Ttab read_ttab(const View& payload, const Limits& limits) {
    if (payload.size() > std::numeric_limits<std::uint64_t>::max() / 8)
        throw FormatError("TTAB size exceeds bit offset range");
    Ttab table{TtabStatus::absent_payload, 0, std::nullopt, false, payload.size() * 8, {}};
    if (payload.size() == 0) return table;
    table.declared_count = payload.le16(0);
    if (payload.size() >= 4) table.version = payload.le16(2);
    if (table.declared_count == 0) {
        table.status = TtabStatus::empty;
        table.remaining_bits -= table.version ? 32 : 16;
        return table;
    }
    if (!table.version) throw FormatError("truncated TTAB version");
    if (*table.version == 2 || *table.version == 3) {
        table.status = TtabStatus::legacy_unsupported;
        table.remaining_bits -= 32;
        return table;
    }
    if (*table.version < 4 || *table.version > 9)
        throw FormatError("unsupported TTAB version (TS1 profile 4..9)");
    std::uint64_t start = 4;
    if (*table.version == 9) {
        const auto code = payload.u8(start++);
        if (code > 1) throw FormatError("unsupported TTAB compression code");
        table.compressed = code == 1;
    }
    Fields fields(payload, start, table.compressed);
    const auto minimum_bits = table.compressed ? 9U : (*table.version > 6 ? 256U : 224U);
    if (table.declared_count > limits.ttab_interactions ||
        table.declared_count > fields.remaining() / minimum_bits)
        throw FormatError("TTAB interaction count exceeds payload or configured limit");
    table.interactions.reserve(table.declared_count);
    for (std::uint32_t i = 0; i < table.declared_count; ++i) {
        Interaction interaction{};
        interaction.action = fields.u16();
        interaction.test = fields.u16();
        const auto motive_count = fields.u32();
        interaction.flags = fields.u32();
        interaction.label_index = fields.u32();
        if (*table.version > 6) interaction.attenuation_code = fields.u32();
        interaction.attenuation_bits = fields.u32();
        interaction.autonomy_threshold = fields.u32();
        interaction.joining_index = signed32(fields.u32());
        const auto motive_bits = table.compressed ? 3U : (*table.version > 6 ? 48U : 16U);
        if (motive_count > limits.ttab_motives || motive_count > fields.remaining() / motive_bits)
            throw FormatError("TTAB motive count exceeds payload or configured limit");
        interaction.motives.reserve(motive_count);
        for (std::uint32_t j = 0; j < motive_count; ++j) {
            MotiveEntry motive{};
            if (*table.version > 6) motive.minimum = signed16(fields.u16());
            motive.delta = signed16(fields.u16());
            if (*table.version > 6) motive.personality_modifier = fields.u16();
            interaction.motives.push_back(motive);
        }
        table.interactions.push_back(std::move(interaction));
    }
    table.status = TtabStatus::decoded;
    table.remaining_bits = fields.remaining();
    return table;
}
} // namespace odh
