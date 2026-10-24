#include "odh/create_object_instance.hpp"

namespace odh {

CreateObjectInstanceRequest decode_create_object_instance(const Instruction& instruction) {
    if (instruction.opcode != 42)
        throw FormatError("CreateObjectInstance decoder requires opcode 42");
    const auto& bytes = instruction.operands;
    const auto guid = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    return {guid, bytes[4], bytes[5], bytes[6], bytes[7]};
}

CreateObjectInstanceResult create_object_instance_in_slot0(
    World& world, std::int16_t new_id, std::int16_t stack_object,
    const CreateObjectInstanceRequest& request, const ObjectRuntimeResources& resources) {
    if (!request.has_known_position() ||
        request.position() != CreateObjectPosition::in_slot0_of_stack_object)
        return {CreateObjectInstanceStatus::unsupported_position, {}};
    if (request.guid == 0 || resources.definition.guid != request.guid)
        return {CreateObjectInstanceStatus::definition_mismatch, {}};
    if (stack_object <= 0) return {CreateObjectInstanceStatus::invalid_stack_object, {}};
    const auto* container = world.find(stack_object);
    if (!container || container->slots.empty())
        return {CreateObjectInstanceStatus::invalid_stack_object, {}};
    if (container->slots[0] != 0)
        return {CreateObjectInstanceStatus::slot_occupied, {}};
    const auto container_position = container->position;

    const auto materialized = materialize_runtime_object(world, new_id, resources);
    if (materialized == ObjectRuntimeCreateStatus::unsupported_profile)
        return {CreateObjectInstanceStatus::unsupported_resources, {}};
    if (materialized != ObjectRuntimeCreateStatus::created)
        return {CreateObjectInstanceStatus::world_rejected, {}};

    // A positioned parent gives the new object the same initial position;
    // an unpositioned parent can still own an unpositioned slot occupant.
    bool placed = false;
    if (container_position) {
        placed = world.set_position(new_id, *container_position) &&
            world.place_in_slot(stack_object, 0, new_id, container_position->direction);
    } else {
        placed = world.attach(stack_object, 0, new_id);
    }
    if (!placed) {
        world.remove(new_id);
        return {CreateObjectInstanceStatus::world_rejected, {}};
    }
    return {CreateObjectInstanceStatus::created, new_id};
}

CreateObjectInstanceResult create_object_instance_out_of_world(
    World& world, std::int16_t new_id, const CreateObjectInstanceRequest& request,
    const ObjectRuntimeResources& resources) {
    if(!request.has_known_position()||request.position()!=CreateObjectPosition::out_of_world)
        return {CreateObjectInstanceStatus::unsupported_position,{}};
    if(!request.guid||resources.definition.guid!=request.guid)
        return {CreateObjectInstanceStatus::definition_mismatch,{}};
    const auto materialized=materialize_runtime_object(world,new_id,resources);
    if(materialized==ObjectRuntimeCreateStatus::unsupported_profile)
        return {CreateObjectInstanceStatus::unsupported_resources,{}};
    if(materialized!=ObjectRuntimeCreateStatus::created)
        return {CreateObjectInstanceStatus::world_rejected,{}};
    // World entities without a position are the host's OUT_OF_WORLD objects.
    return {CreateObjectInstanceStatus::created,new_id};
}

} // namespace odh
