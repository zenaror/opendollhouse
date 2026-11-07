#pragma once

#include "odh/formats.hpp"
#include "odh/object_runtime_resources.hpp"

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

enum class CreateObjectInstanceStatus {
    created,
    unsupported_position,
    invalid_stack_object,
    definition_mismatch,
    unsupported_resources,
    slot_occupied,
    world_rejected,
    unsupported_flags,
    invalid_caller,
    invalid_created_object
};

struct CreateObjectInstanceResult {
    CreateObjectInstanceStatus status = CreateObjectInstanceStatus::world_rejected;
    std::optional<std::int16_t> object_id;
};

enum class CreateObjectPlacementStatus {
    resolved, unsupported_position, invalid_caller, invalid_reference,
    invalid_frame, unknown_direction, invalid_position
};
struct CreateObjectPlacementIdentity {
    std::int16_t id=0;std::uint64_t generation=0;
    bool operator==(const CreateObjectPlacementIdentity& other) const {
        return id==other.id&&generation==other.generation;
    }
};
struct CreateObjectPlacementPlan {
    CreateObjectPlacementStatus status=CreateObjectPlacementStatus::invalid_frame;
    CreateObjectPosition position=CreateObjectPosition::out_of_world;
    CreateObjectPlacementIdentity caller;
    std::optional<CreateObjectPlacementIdentity> reference,container;
    std::optional<std::size_t> container_slot;
    // Absence is the KNOWN source OUT_OF_WORLD sentinel, not unknown geometry.
    // Negative/wrapped coordinates remain real source candidates for the host
    // placement service to reject; this capture never invents a valid lot point.
    std::optional<EntityPosition> destination;
    std::uint8_t direction=0;
    bool retry_underneath=false;
};
// Pure initial positioning stage of pinned VMCreateObjectInstance.Execute.
// No construction, placement checks, register/World mutation, IDs, Init or RNG.
// Captures references before Init; final slot/underneath behavior belongs to host.
CreateObjectPlacementPlan capture_create_object_placement(const World&,const VmFrame&,
    const CreateObjectInstanceRequest&);

// Materialize the supported InSlot0OfStackObject profile. The caller supplies
// resources selected by an exact, unique GUID match and a fresh positive ID.
// This does not bind or execute the object's BHAVs.
CreateObjectInstanceResult create_object_instance_in_slot0(
    World&, std::int16_t new_id, std::int16_t stack_object,
    const CreateObjectInstanceRequest&, const ObjectRuntimeResources&);

// Materialize the supported OutOfWorld profile without assigning a lot
// position or container. The caller still owns startup, Main scheduling and
// the VM StackObject return.
CreateObjectInstanceResult create_object_instance_out_of_world(
    World&, std::int16_t new_id, const CreateObjectInstanceRequest&,
    const ObjectRuntimeResources&);

// Read-only NoDuplicate admission, before ID allocation/Init/RNG. The source
// explicitly bypasses both slot modes. Other modes query the destination TILE
// (not subtile/GUID globally), excluding ghosts and out-of-world entities.
// nullopt means the destination or a candidate's definition is unknown, so
// absence is unproven. OutOfWorld has no registered destination bucket.
std::optional<bool> create_object_instance_has_duplicate(const World&,
    const CreateObjectInstanceRequest&,std::optional<EntityPosition> destination);
// Resolved OUT_OF_WORLD is known to have no spatial bucket; unresolved plans
// remain unknown and cannot authorize construction under NoDuplicate.
std::optional<bool> create_object_instance_has_duplicate(const World&,
    const CreateObjectInstanceRequest&,const CreateObjectPlacementPlan&);


// After construction, Init/EP8 and preparation of Main, place an initialized
// ordinary object in CALLER avatar slot0. NoDuplicate is ignored here exactly
// as in pinned7e76; other flags require host semantics and are rejected.
// Direction was captured by the host before Init; current caller coordinates
// are read at placement. The host owns carry playback and Main scheduling.
// Private-candidate operation: exceptions require discarding the candidate.
CreateObjectInstanceResult place_created_object_in_hand(World&,std::int16_t object,
    std::int16_t caller,const CreateObjectInstanceRequest&,std::uint8_t captured_direction);

} // namespace odh
