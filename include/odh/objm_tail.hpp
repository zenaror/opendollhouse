#pragma once

#include "odh/objm_common.hpp"
#include "odh/object_definition.hpp"
#include "odh/object_types.hpp"

namespace odh {

struct ObjmTailLimits {
    std::uint64_t block_bytes = 8U * 1024U * 1024U;
    std::uint64_t decoded_words = 65536; // Scalars/counts and string fields; excludes prefix/common.
    std::uint64_t string_bytes = 256U * 1024U; // Cumulative content, excluding NUL/padding.
    std::uint32_t string_length = 4096;
    std::uint32_t portal_values = 4096;
    std::uint32_t accessories = 1024;
    std::uint32_t interactions = 4096;
    std::uint32_t object_uses = 4096;
    std::uint32_t motive_deltas = 4096;
};
struct ObjmSavedString {
    std::string bytes; // Byte-preserving; no encoding conversion or runtime parsing.
    std::optional<std::uint8_t> padding; // Raw optional byte following NUL.
    std::uint64_t start_byte;
};
struct ObjmMultitileState {
    std::int32_t offset_x, offset_y, offset_level, group_x, group_y, group_level;
    std::int16_t parent_id;
};
struct ObjmSavedInteraction {
    std::int32_t unknown_zero, uid;
    std::int16_t caller_id, target_id, icon;
    std::int32_t tta_index;
    std::array<std::int16_t,4> args;
    std::int32_t priority;
    std::int16_t action_tree_id;
    std::uint32_t attenuation_bits;
    std::int32_t flags;
};
struct ObjmSavedAccessory { ObjmSavedString name, binding; };
struct ObjmSavedObjectUse {
    std::int16_t target_id;
    std::int32_t stack_length;
    std::uint8_t unknown2;
};
struct ObjmSavedMotiveDelta {
    std::int32_t motive;
    std::uint32_t tick_delta_bits, stop_at_bits;
};
struct ObjmPersonState {
    std::int32_t animation_event_count, engaged;
    ObjmSavedString body, body_texture, unknown1, unknown2;
    ObjmSavedString left_hand, left_hand_texture, right_hand, right_hand_texture;
    ObjmSavedString head, head_texture;
    std::int32_t accessory_count;
    std::vector<ObjmSavedAccessory> accessories;
    ObjmSavedString animation, carry_animation, base_animation;
    std::int32_t routing_state;
    // IEEE binary32 bit patterns, including NaNs, signed zero, and infinities.
    std::array<std::uint32_t,9> first_float_bits;
    std::array<std::uint32_t,16> motive_data_old_bits, motive_data_bits;
    std::vector<std::int16_t> person_data; // 80 for version <=0x45; 256 afterwards.
    std::int32_t routing_frame_count;
    ObjmSavedInteraction active_interaction, last_interaction;
    std::int32_t interaction_queue_count, object_use_count, motive_delta_count;
    std::vector<ObjmSavedInteraction> interaction_queue;
    std::vector<ObjmSavedObjectUse> object_uses;
    std::vector<ObjmSavedMotiveDelta> motive_deltas;
};
struct ObjmTailState {
    std::optional<ObjmMultitileState> multitile;
    std::optional<std::int32_t> portal_count;
    std::optional<std::vector<std::uint32_t>> portal_float_bits;
    std::optional<ObjmPersonState> person;
    std::int32_t unknown_int;
    std::uint32_t block_offset;
    bool odd_offset;
    std::uint64_t start_bits, consumed_bits, remaining_bits, decoded_words, string_bytes;
    // Opaque suffix after UnknownInt, packed MSB-first starting at bit zero.
    // Size is ceil(remaining_bits/8); unused low bits of its last byte are zero.
    std::vector<std::uint8_t> remaining_data;
};

// Resume exactly common.consumed_bits, using caller-proven unique OBJD and the
// matching serialized OBJT entry. Requires GUID equality and OBJT metadata.
// MasterID !=0 selects multitile; OBJT.OBJDType selects portal8/person2.
// Full known pinned FreeSO instance schema is read, with an owned opaque suffix;
// this does not construct entities, apply saved state, or prove original behavior.
// Strings follow IffFieldEncode's byte interruption, optional pad, and reset
// policy. Missing bits/NUL/pad are errors rather than reference EOF zero-fill.
ObjmTailState read_objm_tail(const ObjmBlock&, const ObjmCommonState&, std::uint32_t version,
                            const ObjectDefinition&, const ObjtEntry&,
                            const ObjmTailLimits& = {});

} // namespace odh
