#include "odh/objm_common.hpp"

#include <limits>

namespace odh {
namespace {
class CommonFields {
public:
    CommonFields(const std::vector<std::uint8_t>& bytes, std::uint64_t position,
                 std::uint64_t budget)
        : bytes_(bytes), total_bits_(static_cast<std::uint64_t>(bytes.size()) * 8),
          position_(position), budget_(budget) {}
    std::int16_t word() { return static_cast<std::int16_t>(field({5,8,13,16})); }
    std::int32_t integer() { return field({6,11,21,32}); }
    std::uint8_t byte() { return static_cast<std::uint8_t>(field({2,4,6,8})); }
    std::uint64_t position() const { return position_; }
    std::uint64_t remaining() const { return total_bits_ - position_; }
    std::uint64_t decoded() const { return decoded_; }
    void require_fields(std::uint64_t count) const {
        if (count > budget_ - decoded_)
            throw FormatError("OBJM common decoded word budget exceeded");
        // Every direct field needs at least its compact-zero bit.
        if (count > remaining()) throw FormatError("truncated OBJM common fields");
    }
private:
    std::uint32_t bits(unsigned count) {
        if (count > remaining()) throw FormatError("truncated OBJM common field");
        std::uint32_t result = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto byte = bytes_[static_cast<std::size_t>(position_ / 8)];
            result = (result << 1) | ((byte >> (7 - (position_ & 7U))) & 1U);
            ++position_;
        }
        return result;
    }
    std::int32_t field(const std::array<unsigned,4>& widths) {
        require_fields(1);
        ++decoded_;
        if (!bits(1)) return 0;
        const auto width = widths[bits(2)];
        const auto raw = bits(width);
        const auto value = (raw & (std::uint32_t{1} << (width - 1)))
            ? static_cast<std::int64_t>(raw) - (std::int64_t{1} << width)
            : static_cast<std::int64_t>(raw);
        return static_cast<std::int32_t>(value);
    }
    const std::vector<std::uint8_t>& bytes_;
    std::uint64_t total_bits_, position_, budget_, decoded_ = 0;
};
std::uint32_t count(std::int32_t raw, std::uint32_t limit, const char* name) {
    if (raw < 0) throw FormatError(std::string("negative OBJM common ") + name + " count");
    const auto value = static_cast<std::uint32_t>(raw);
    if (value > limit)
        throw FormatError(std::string("OBJM common ") + name + " count exceeds limit");
    return value;
}
}

ObjmCommonState read_objm_common(const ObjmBlock& block, const ObjmPrefix& prefix,
                                const ObjmCommonLimits& limits) {
    if (block.bytes.size() > limits.block_bytes ||
        block.bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8)
        throw FormatError("OBJM common block exceeds byte limit");
    const auto total_bits = static_cast<std::uint64_t>(block.bytes.size()) * 8;
    if (prefix.block_offset != block.offset || prefix.odd_offset != bool(block.offset & 1U) ||
        prefix.consumed_bits > total_bits ||
        prefix.remaining_bits != total_bits - prefix.consumed_bits)
        throw FormatError("OBJM common prefix cursor does not match block");
    CommonFields fields(block.bytes, prefix.consumed_bits, limits.decoded_words);
    ObjmCommonState result{};
    result.block_offset = block.offset;
    result.odd_offset = prefix.odd_offset;
    result.start_bits = prefix.consumed_bits;
    result.stack_count = fields.integer();
    const auto frames = count(result.stack_count, limits.stack_frames, "stack frame");
    result.stack_flags = fields.integer();
    // Seven scalar fields per frame plus four remaining section headers.
    fields.require_fields(std::uint64_t{frames} * 7 + 4);
    result.stack.reserve(frames);
    for (std::uint32_t i = 0; i < frames; ++i) {
        ObjmStackFrame frame{};
        frame.stack_object_id = fields.word();
        frame.tree_id = fields.word();
        frame.node_id = fields.word();
        frame.local_count = fields.byte();
        frame.parameter_count = fields.byte();
        const auto words = static_cast<std::uint64_t>(frame.local_count) + frame.parameter_count;
        fields.require_fields(words + 2 + std::uint64_t{frames - i - 1} * 7 + 4);
        frame.parameters.reserve(frame.parameter_count);
        frame.locals.reserve(frame.local_count);
        for (unsigned j = 0; j < frame.parameter_count; ++j) frame.parameters.push_back(fields.word());
        for (unsigned j = 0; j < frame.local_count; ++j) frame.locals.push_back(fields.word());
        frame.primitive_state = fields.integer();
        frame.code_owner_obj_type = fields.word();
        result.stack.push_back(std::move(frame));
    }
    result.relationship_flag = fields.integer();
    if (result.relationship_flag >= 0)
        throw FormatError("unsupported OBJM common nonnegative relationship flag");
    result.relationship_count = fields.integer();
    const auto relationships = count(result.relationship_count, limits.relationships, "relationship");
    fields.require_fields(std::uint64_t{relationships} * 3 + 2);
    result.relationships.reserve(relationships);
    for (std::uint32_t i = 0; i < relationships; ++i) {
        ObjmRelationshipEntry relation{};
        relation.is_present = fields.integer();
        if (relation.is_present == 0)
            throw FormatError("unsupported OBJM common absent relationship");
        relation.target_id = fields.integer();
        relation.value_count = fields.integer();
        const auto values = count(relation.value_count, limits.relationship_values, "relationship value");
        fields.require_fields(std::uint64_t{values} + std::uint64_t{relationships - i - 1} * 3 + 2);
        relation.values.reserve(values);
        for (std::uint32_t j = 0; j < values; ++j) relation.values.push_back(fields.integer());
        result.relationships.push_back(std::move(relation));
    }
    result.slot_count = fields.word();
    const auto slots = count(result.slot_count, limits.slots, "slot");
    fields.require_fields(std::uint64_t{slots} * 2 + 1);
    result.slots.reserve(slots == 0 ? 0 : slots - 1);
    for (std::uint32_t i = 0; i < slots; ++i) {
        const ObjmSavedSlot slot{fields.word(), fields.word()};
        if (i == 0) result.linked_slot = slot;
        else result.slots.push_back(slot);
    }
    result.dynamic_sprite_flag_count = fields.word();
    const auto sprites = count(result.dynamic_sprite_flag_count, limits.dynamic_sprite_flags, "dynamic sprite flag");
    fields.require_fields(sprites);
    result.dynamic_sprite_flags.reserve(sprites);
    for (std::uint32_t i = 0; i < sprites; ++i) result.dynamic_sprite_flags.push_back(fields.word());
    result.consumed_bits = fields.position();
    result.remaining_bits = fields.remaining();
    result.decoded_words = fields.decoded();
    return result;
}
} // namespace odh
