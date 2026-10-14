#pragma once

#include "odh/objm_prefix.hpp"
#include <optional>

namespace odh {

struct ObjmCommonLimits {
    std::uint64_t block_bytes = 8U * 1024U * 1024U;
    std::uint32_t stack_frames = 1024;
    std::uint32_t relationships = 4096;
    std::uint32_t relationship_values = 4096; // Per relationship.
    std::uint32_t slots = 4096; // Includes the first linked slot.
    std::uint32_t dynamic_sprite_flags = 4096;
    // All decoded fields in this section, including counts and byte fields.
    // Prefix fields are excluded; callers can subtract a shared budget first.
    std::uint64_t decoded_words = 65536;
};
struct ObjmStackFrame {
    std::int16_t stack_object_id, tree_id, node_id;
    // Encoded signed byte fields cast to uint8, as in the pinned reference.
    std::uint8_t local_count, parameter_count;
    std::vector<std::int16_t> parameters, locals;
    std::int32_t primitive_state;
    std::int16_t code_owner_obj_type;
};
struct ObjmRelationshipEntry {
    std::int32_t is_present, target_id, value_count;
    std::vector<std::int32_t> values;
};
struct ObjmSavedSlot {
    std::int16_t unknown, object_id;
};
struct ObjmCommonState {
    std::int32_t stack_count, stack_flags, relationship_flag, relationship_count;
    std::vector<ObjmStackFrame> stack;
    std::vector<ObjmRelationshipEntry> relationships;
    std::int16_t slot_count, dynamic_sprite_flag_count;
    // Missing when slot_count is zero; slots holds only subsequent entries.
    std::optional<ObjmSavedSlot> linked_slot;
    std::vector<ObjmSavedSlot> slots;
    std::vector<std::int16_t> dynamic_sprite_flags;
    std::uint32_t block_offset;
    bool odd_offset;
    std::uint64_t start_bits, consumed_bits, remaining_bits, decoded_words;
};

// Resume the matching prefix's exact bit cursor. Owns all returned data and
// stops immediately before the OBJD/objt-dependent multitile branch. No
// entity restoration or resource-dependent tail interpretation is performed.
// This bounded profile requires a negative relationship flag and nonzero
// IsPresent, matching FreeSO OBJM.cs at the pinned reference revision.
// Every field must exist; missing bits never become implicit EOF zeros.
ObjmCommonState read_objm_common(const ObjmBlock&, const ObjmPrefix&,
                                const ObjmCommonLimits& = {});

} // namespace odh
