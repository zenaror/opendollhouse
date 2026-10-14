#include "odh/objm_prefix.hpp"

#include <limits>

namespace odh {
namespace {
class PrefixFields {
public:
    explicit PrefixFields(const std::vector<std::uint8_t>& bytes)
        : bytes_(bytes), total_bits_(static_cast<std::uint64_t>(bytes.size()) * 8) {}
    std::int16_t word() {
        constexpr std::array<unsigned, 4> widths{5, 8, 13, 16};
        return static_cast<std::int16_t>(field(widths));
    }
    std::int32_t integer() {
        constexpr std::array<unsigned, 4> widths{6, 11, 21, 32};
        return field(widths);
    }
    std::uint64_t position() const { return position_; }
    std::uint64_t remaining() const { return total_bits_ - position_; }
private:
    std::uint32_t bits(unsigned count) {
        if (count > remaining()) throw FormatError("truncated OBJM instance prefix");
        std::uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto byte = bytes_[static_cast<std::size_t>(position_ / 8)];
            value = (value << 1) | ((byte >> (7 - (position_ & 7U))) & 1U);
            ++position_;
        }
        return value;
    }
    std::int32_t field(const std::array<unsigned, 4>& widths) {
        if (!bits(1)) return 0;
        const auto width = widths[bits(2)];
        const auto raw = bits(width);
        // Subtract in signed64 so width32 and INT32_MIN are defined in C++17.
        const auto value = (raw & (std::uint32_t{1} << (width - 1)))
            ? static_cast<std::int64_t>(raw) - (std::int64_t{1} << width)
            : static_cast<std::int64_t>(raw);
        return static_cast<std::int32_t>(value);
    }
    const std::vector<std::uint8_t>& bytes_;
    std::uint64_t total_bits_, position_ = 0;
};
}

ObjmPrefix read_objm_prefix(const ObjmBlock& block, const ObjmPrefixLimits& limits) {
    if (block.bytes.size() > limits.block_bytes ||
        block.bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8)
        throw FormatError("OBJM instance block exceeds byte limit");
    PrefixFields fields(block.bytes);
    ObjmPrefix result{};
    result.block_offset = block.offset;
    result.odd_offset = (block.offset & 1U) != 0;
    result.footprint = {fields.integer(), fields.integer(), fields.integer(), fields.integer()};
    result.x = fields.integer();
    result.y = fields.integer();
    result.level = fields.integer();
    result.unknown_data = fields.word();
    const auto count = fields.word();
    if (count < 0) throw FormatError("negative OBJM instance attribute count");
    const auto attribute_count = static_cast<std::uint32_t>(count);
    if (attribute_count > limits.attributes)
        throw FormatError("OBJM instance attribute count exceeds limit");
    // Every word needs at least one bit, including fixed banks after attrs.
    constexpr std::uint64_t fixed_words = 8 + 68 + 5;
    if (static_cast<std::uint64_t>(attribute_count) + fixed_words > fields.remaining())
        throw FormatError("truncated OBJM instance attribute/fixed banks");
    result.attributes.reserve(attribute_count);
    for (std::uint32_t i = 0; i < attribute_count; ++i)
        result.attributes.push_back(fields.word());
    for (auto& word : result.temps) word = fields.word();
    for (auto& word : result.object_data) word = fields.word();
    for (auto& word : result.extras) word = fields.word();
    result.consumed_bits = fields.position();
    result.remaining_bits = fields.remaining();
    return result;
}
} // namespace odh
