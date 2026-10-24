#pragma once

#include "odh/formats.hpp"

namespace odh {

enum class CreateObjectPosition : std::uint8_t {
    in_front_of_me = 0,
    on_top_of_me = 1,
    in_my_hand = 2,
    in_front_of_stack_object = 3,
    in_slot0_of_stack_object = 4,
    underneath_me = 5,
    out_of_world = 6,
    below_object_in_stack_param0 = 7,
    below_object_in_local = 8,
    next_to_me_in_direction_of_local = 9
};

struct CreateObjectInstanceRequest {
    std::uint32_t guid = 0;
    std::uint8_t raw_position = 0;
    std::uint8_t flags = 0;
    std::uint8_t local_to_use = 0;
    std::uint8_t interaction_callback = 0;

    CreateObjectPosition position() const {
        return static_cast<CreateObjectPosition>(raw_position);
    }
    bool has_known_position() const { return raw_position <= 9; }
    bool no_duplicate() const { return (flags & 0x01U) != 0; }
    bool pass_object_ids() const { return (flags & 0x02U) != 0; }
    bool use_neighbor() const { return (flags & 0x04U) != 0; }
    bool fail_if_non_empty() const { return (flags & 0x08U) != 0; }
    bool pass_temp0() const { return (flags & 0x10U) != 0; }
    bool face_stack_object_direction() const { return (flags & 0x20U) != 0; }
    bool return_immediately() const { return (flags & 0x40U) != 0; }
    bool persist_in_database() const { return (flags & 0x80U) != 0; }
};

// Decode opcode 42's fixed eight-byte operand without deciding whether the
// selected position, options, or object definition are supported by the host.
CreateObjectInstanceRequest decode_create_object_instance(const Instruction&);

} // namespace odh
