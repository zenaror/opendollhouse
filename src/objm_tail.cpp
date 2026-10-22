#include "odh/objm_tail.hpp"

#include <limits>

namespace odh {
namespace {
class TailFields {
public:
    TailFields(const ObjmBlock& block, std::uint64_t position, const ObjmTailLimits& limits)
        : bytes_(block.bytes), total_(static_cast<std::uint64_t>(bytes_.size()) * 8),
          position_(position), limits_(limits),
          odd_(!(block.offset & 1U) != bool((position / 8) & 1U)) {}
    std::int16_t word() { return static_cast<std::int16_t>(field({5,8,13,16})); }
    std::int32_t integer() { return field({6,11,21,32}); }
    std::uint32_t float_bits() { return static_cast<std::uint32_t>(integer()); }
    std::uint8_t byte() { return static_cast<std::uint8_t>(field({2,4,6,8})); }
    std::uint64_t position() const { return position_; }
    std::uint64_t remaining() const { return total_ - position_; }
    std::uint64_t decoded() const { return decoded_; }
    std::uint64_t string_bytes() const { return string_bytes_; }
    void require_fields(std::uint64_t count) const {
        if (count > limits_.decoded_words - decoded_)
            throw FormatError("OBJM tail decoded word budget exceeded");
        if (count > remaining()) throw FormatError("truncated OBJM tail fields");
    }
    ObjmSavedString string(bool next_field) {
        require_fields(1);
        ++decoded_;
        // A regular field prefetches its current byte. ReadString rewinds only
        // when bitPos==0; partial-byte bits are discarded. Consecutive false
        // strings have bitPos==8 and no prefetch, captured by string_mode_.
        if (!string_mode_) {
            if (position_ % 8 == 0) odd_ = !odd_;
            else position_ += 8 - position_ % 8;
        }
        const auto start = position_ / 8;
        auto end = start;
        while (true) {
            if (end >= bytes_.size()) throw FormatError("truncated OBJM tail string NUL");
            if (bytes_[static_cast<std::size_t>(end)] == 0) break;
            const auto length = end - start + 1;
            if (length > limits_.string_length)
                throw FormatError("OBJM tail string length exceeds limit");
            if (length > limits_.string_bytes - string_bytes_)
                throw FormatError("OBJM tail string byte budget exceeded");
            ++end;
        }
        const auto length = end - start;
        ++end; // NUL.
        std::optional<std::uint8_t> padding;
        if ((length % 2 == 0) == !odd_) {
            if (end >= bytes_.size()) throw FormatError("truncated OBJM tail string padding");
            padding = bytes_[static_cast<std::size_t>(end++)];
        }
        // Bounds and cumulative budget checked before constructing owned text.
        ObjmSavedString result{
            std::string(bytes_.begin() + static_cast<std::ptrdiff_t>(start),
                        bytes_.begin() + static_cast<std::ptrdiff_t>(start + length)),
            padding, start};
        string_bytes_ += length;
        position_ = end * 8;
        string_mode_ = !next_field;
        odd_ = next_field; // Reference false reset, or true reload's odd=true.
        return result;
    }
    std::vector<std::uint8_t> suffix() const {
        const auto size = remaining() / 8 + (remaining() % 8 != 0);
        std::vector<std::uint8_t> result(static_cast<std::size_t>(size), 0);
        for (std::uint64_t i = 0; i < remaining(); ++i) {
            const auto source = position_ + i;
            if ((bytes_[static_cast<std::size_t>(source / 8)] >> (7 - source % 8)) & 1U)
                result[static_cast<std::size_t>(i / 8)] |= static_cast<std::uint8_t>(1U << (7 - i % 8));
        }
        return result;
    }
private:
    std::uint32_t bits(unsigned count) {
        if (count > remaining()) throw FormatError("truncated OBJM tail field");
        std::uint32_t result = 0;
        for (unsigned i = 0; i < count; ++i) {
            result = (result << 1) |
                ((bytes_[static_cast<std::size_t>(position_ / 8)] >> (7 - position_ % 8)) & 1U);
            ++position_;
            if (position_ % 8 == 0) odd_ = !odd_;
        }
        return result;
    }
    std::int32_t field(const std::array<unsigned,4>& widths) {
        if (string_mode_) throw FormatError("OBJM tail field without string reload");
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
    std::uint64_t total_, position_;
    const ObjmTailLimits& limits_;
    std::uint64_t decoded_ = 0, string_bytes_ = 0;
    bool odd_, string_mode_ = false;
};
std::uint32_t count(std::int32_t raw, std::uint32_t limit, const char* name) {
    if (raw < 0) throw FormatError(std::string("negative OBJM tail ") + name + " count");
    const auto value = static_cast<std::uint32_t>(raw);
    if (value > limit) throw FormatError(std::string("OBJM tail ") + name + " count exceeds limit");
    return value;
}
ObjmSavedInteraction interaction(TailFields& f) {
    f.require_fields(14);
    ObjmSavedInteraction result{};
    result.unknown_zero = f.integer();
    if (result.unknown_zero != 0)
        throw FormatError("unsupported OBJM tail nonzero interaction initial field");
    result.uid = f.integer();
    result.caller_id = f.word(); result.target_id = f.word(); result.icon = f.word();
    result.tta_index = f.integer();
    for (auto& value : result.args) value = f.word();
    result.priority = f.integer(); result.action_tree_id = f.word();
    result.attenuation_bits = f.float_bits(); result.flags = f.integer();
    return result;
}
ObjmPersonState person(TailFields& f, std::uint32_t version, const ObjmTailLimits& limits) {
    ObjmPersonState p{};
    p.animation_event_count = f.integer(); p.engaged = f.integer();
    p.body = f.string(false); p.body_texture = f.string(false);
    p.unknown1 = f.string(false); p.unknown2 = f.string(false);
    p.left_hand = f.string(false); p.left_hand_texture = f.string(false);
    p.right_hand = f.string(false); p.right_hand_texture = f.string(false);
    p.head = f.string(false); p.head_texture = f.string(true);
    p.accessory_count = f.integer();
    const auto accessories = count(p.accessory_count, limits.accessories, "accessory");
    f.require_fields(std::uint64_t{accessories} * 2 + 3);
    p.accessories.reserve(accessories);
    for (std::uint32_t i = 0; i < accessories; ++i)
        p.accessories.push_back({f.string(false), f.string(false)});
    p.animation = f.string(false); p.carry_animation = f.string(false);
    p.base_animation = f.string(true);
    const auto person_words = version > 0x45 ? 256U : 80U;
    // Routing state,41 floats,PersonData,frame count,two interactions,three
    // array counts and the final instance UnknownInt are required before reserve.
    f.require_fields(1 + 41 + person_words + 1 + 28 + 3 + 1);
    p.routing_state = f.integer();
    for (auto& value : p.first_float_bits) value = f.float_bits();
    for (auto& value : p.motive_data_old_bits) value = f.float_bits();
    for (auto& value : p.motive_data_bits) value = f.float_bits();
    p.person_data.reserve(person_words);
    for (unsigned i = 0; i < person_words; ++i) p.person_data.push_back(f.word());
    p.routing_frame_count = f.integer();
    p.active_interaction = interaction(f); p.last_interaction = interaction(f);
    p.interaction_queue_count = f.integer();
    const auto queue = count(p.interaction_queue_count, limits.interactions, "interaction queue");
    f.require_fields(std::uint64_t{queue} * 14 + 3);
    p.interaction_queue.reserve(queue);
    for (std::uint32_t i = 0; i < queue; ++i) p.interaction_queue.push_back(interaction(f));
    p.object_use_count = f.integer();
    const auto uses = count(p.object_use_count, limits.object_uses, "object use");
    f.require_fields(std::uint64_t{uses} * 3 + 2);
    p.object_uses.reserve(uses);
    for (std::uint32_t i = 0; i < uses; ++i)
        p.object_uses.push_back({f.word(), f.integer(), f.byte()});
    p.motive_delta_count = f.integer();
    const auto deltas = count(p.motive_delta_count, limits.motive_deltas, "motive delta");
    f.require_fields(std::uint64_t{deltas} * 3 + 1);
    p.motive_deltas.reserve(deltas);
    for (std::uint32_t i = 0; i < deltas; ++i)
        p.motive_deltas.push_back({f.integer(), f.float_bits(), f.float_bits()});
    return p;
}
} // namespace

namespace {
ObjmTailState read_objm_tail_fields(const ObjmBlock& block, const ObjmCommonState& common,
                            std::uint32_t version, const ObjtEntry& entry,
                            bool is_multitile, const ObjmTailLimits& limits) {
    if (block.bytes.size() > limits.block_bytes ||
        block.bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8)
        throw FormatError("OBJM tail block exceeds byte limit");
    const auto total = static_cast<std::uint64_t>(block.bytes.size()) * 8;
    if (common.block_offset != block.offset || common.odd_offset != bool(block.offset & 1U) ||
        common.start_bits > common.consumed_bits || common.consumed_bits > total ||
        common.remaining_bits != total - common.consumed_bits)
        throw FormatError("OBJM tail common cursor does not match block");
    if (entry.guid == 0 || !entry.metadata)
        throw FormatError("OBJM tail resource metadata does not match OBJT");
    TailFields fields(block, common.consumed_bits, limits);
    ObjmTailState result{};
    result.block_offset = block.offset; result.odd_offset = common.odd_offset;
    result.start_bits = common.consumed_bits;
    if (is_multitile) {
        fields.require_fields(8); // Seven multitile fields plus final UnknownInt.
        result.multitile = ObjmMultitileState{
            fields.integer(), fields.integer(), fields.integer(),
            fields.integer(), fields.integer(), fields.integer(), fields.word()};
    }
    if (entry.metadata->objd_type == 8) {
        result.portal_count = fields.integer();
        const auto portals = count(*result.portal_count, limits.portal_values, "portal value");
        fields.require_fields(std::uint64_t{portals} + 1);
        result.portal_float_bits.emplace(); result.portal_float_bits->reserve(portals);
        for (std::uint32_t i = 0; i < portals; ++i)
            result.portal_float_bits->push_back(fields.float_bits());
    }
    if (entry.metadata->objd_type == 2) result.person = person(fields, version, limits);
    result.unknown_int = fields.integer();
    result.consumed_bits = fields.position(); result.remaining_bits = fields.remaining();
    result.decoded_words = fields.decoded(); result.string_bytes = fields.string_bytes();
    result.remaining_data = fields.suffix();
    return result;
}
} // namespace

ObjmTailState read_objm_tail(const ObjmBlock& block, const ObjmCommonState& common,
                            std::uint32_t version, const ObjectDefinition& definition,
                            const ObjtEntry& entry, const ObjmTailLimits& limits) {
    if (entry.guid == 0 || entry.guid != definition.guid || !entry.metadata)
        throw FormatError("OBJM tail resource metadata does not match OBJT");
    if ((definition.version != 136 && definition.version != 138) ||
        definition.is_multitile != (definition.master_id != 0))
        throw FormatError("OBJM tail invalid OBJD metadata profile");
    return read_objm_tail_fields(block, common, version, entry,
                                 definition.master_id != 0, limits);
}

ObjmTailState read_objm_person_tail_without_definition(const ObjmBlock& block,
                            const ObjmCommonState& common, std::uint32_t version,
                            const ObjtEntry& entry, bool is_multitile,
                            const ObjmTailLimits& limits) {
    if (entry.guid == 0 || !entry.metadata || entry.metadata->objd_type != 2)
        throw FormatError("OBJM tail without OBJD requires Person OBJT metadata");
    return read_objm_tail_fields(block, common, version, entry, is_multitile, limits);
}
} // namespace odh
