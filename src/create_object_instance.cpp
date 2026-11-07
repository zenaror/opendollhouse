#include "odh/create_object_instance.hpp"

namespace odh {

namespace {
std::int16_t source_short(std::int32_t value) {
    const auto bits=static_cast<std::uint16_t>(value);
    return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
}
CreateObjectPlacementPlan capture_create_object_placement(const World& world,const VmFrame& frame,
        const CreateObjectInstanceRequest& request) {
    CreateObjectPlacementPlan plan;plan.position=request.position();
    if(!request.has_known_position()){plan.status=CreateObjectPlacementStatus::unsupported_position;return plan;}
    const auto* caller=world.frame_entity(frame,VmEntityRole::caller,true);
    if(!caller){plan.status=CreateObjectPlacementStatus::invalid_caller;return plan;}
    plan.caller={caller->id,caller->generation};
    if(request.position()==CreateObjectPosition::out_of_world){plan.status=CreateObjectPlacementStatus::resolved;return plan;}
    const Entity* reference=caller;
    if(request.position()==CreateObjectPosition::in_front_of_stack_object||
       request.position()==CreateObjectPosition::in_slot0_of_stack_object)reference=world.frame_entity(frame,VmEntityRole::stack_object,true);
    else if(request.position()==CreateObjectPosition::below_object_in_stack_param0){
        if(frame.arguments.empty()){plan.status=CreateObjectPlacementStatus::invalid_frame;return plan;}
        reference=world.find(frame.arguments[0]);
    }else if(request.position()==CreateObjectPosition::below_object_in_local){
        if(request.local_to_use>=frame.locals.size()){plan.status=CreateObjectPlacementStatus::invalid_frame;return plan;}
        reference=world.find(frame.locals[request.local_to_use]);
    }else if(request.position()==CreateObjectPosition::next_to_me_in_direction_of_local&&request.local_to_use>=frame.locals.size()){
        plan.status=CreateObjectPlacementStatus::invalid_frame;return plan;
    }
    if(!reference){plan.status=CreateObjectPlacementStatus::invalid_reference;return plan;}
    plan.reference=CreateObjectPlacementIdentity{reference->id,reference->generation};
    std::optional<std::int16_t> direction;
    // Direction is a property even when Position is OUT_OF_WORLD. Positioned
    // entities retain its eight-notch native representation on EntityPosition.
    if(reference->position)direction=reference->position->direction;
    else {const auto field=reference->object_words.find(1);if(field!=reference->object_words.end())direction=field->second;}
    if(!direction||*direction<0||*direction>7){plan.status=CreateObjectPlacementStatus::unknown_direction;return plan;}
    plan.direction=static_cast<std::uint8_t>(*direction);
    if(request.position()==CreateObjectPosition::in_my_hand||request.position()==CreateObjectPosition::in_slot0_of_stack_object){
        plan.container=plan.reference;plan.container_slot=0;
        plan.status=CreateObjectPlacementStatus::resolved;return plan;
    }
    EntityPosition point=reference->position.value_or(EntityPosition{-32768,-32768,1,plan.direction});
    if(point.x<INT16_MIN||point.x>INT16_MAX||point.y<INT16_MIN||point.y>INT16_MAX||
       point.level<INT8_MIN||point.level>INT8_MAX){plan.status=CreateObjectPlacementStatus::invalid_position;return plan;}
    auto offset_direction=static_cast<std::int16_t>(-1);
    if(request.position()==CreateObjectPosition::in_front_of_me||request.position()==CreateObjectPosition::in_front_of_stack_object)
        offset_direction=*direction;
    else if(request.position()==CreateObjectPosition::next_to_me_in_direction_of_local){
        const auto value=world.read(frame,3,1);
        if(!value||*value<0||*value>7){plan.status=CreateObjectPlacementStatus::unknown_direction;return plan;}
        const auto sum=source_short(static_cast<std::int32_t>(frame.locals[request.local_to_use])+(*value/2)*2);
        offset_direction=static_cast<std::int16_t>(sum%8); // C# signed remainder, not positive modulo.
        plan.direction=0;
        if(offset_direction==0||offset_direction==2||offset_direction==4||offset_direction==6)
            plan.direction=static_cast<std::uint8_t>(offset_direction);
    }
    switch(offset_direction){
        case 0:point.y=source_short(point.y-16);break;
        case 2:point.x=source_short(point.x+16);break;
        case 4:point.y=source_short(point.y+16);break;
        case 6:point.x=source_short(point.x-16);break;
        default:break; // Diagonal/front and negative/odd mode9 leave position unchanged.
    }
    point.direction=plan.direction;
    if(point.x!=-32768||point.y!=-32768||point.level!=1)plan.destination=point;
    plan.retry_underneath=request.position()==CreateObjectPosition::underneath_me;
    plan.status=CreateObjectPlacementStatus::resolved;return plan;
}
std::optional<bool> create_object_instance_has_duplicate(const World& world,
        const CreateObjectInstanceRequest& request,const CreateObjectPlacementPlan& plan) {
    if(plan.status!=CreateObjectPlacementStatus::resolved||plan.position!=request.position())return {};
    if(!request.no_duplicate())return false;
    if(!plan.destination)return false;
    return create_object_instance_has_duplicate(world,request,plan.destination);
}

std::optional<bool> create_object_instance_has_duplicate(const World& world,
    const CreateObjectInstanceRequest& request,std::optional<EntityPosition> destination) {
    if(!request.no_duplicate()||request.position()==CreateObjectPosition::in_my_hand||
       request.position()==CreateObjectPosition::in_slot0_of_stack_object||
       request.position()==CreateObjectPosition::out_of_world)return false;
    if(!destination)return std::nullopt;
    if(destination->level<1)return false; // Source RoomInfo never registers OUT_OF_WORLD.
    const auto tile=[](std::int32_t value){return value>=0?value/16:((value+1)/16)-1;};
    bool unknown=false;
    std::int16_t current=0;
    while(const auto next=world.next_entity(current)) {
        current=*next;const auto* entity=world.find(current);
        if(!entity->position||entity->position->level!=destination->level||
           tile(entity->position->x)!=tile(destination->x)||tile(entity->position->y)!=tile(destination->y))continue;
        if(!entity->definition){unknown=true;continue;}
        if(entity->definition->guid==request.guid)return true;
    }
    return unknown?std::nullopt:std::optional<bool>{false};
}

CreateObjectInstanceResult place_created_object_in_hand(World& world,std::int16_t object,
    std::int16_t caller,const CreateObjectInstanceRequest& request,std::uint8_t direction) {
    if(request.position()!=CreateObjectPosition::in_my_hand)
        return {CreateObjectInstanceStatus::unsupported_position,{}};
    if(request.flags&~0x01U)return {CreateObjectInstanceStatus::unsupported_flags,{}};
    const auto* actor=world.find(caller);const auto* item=world.find(object);
    if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||actor->slots.empty()||direction>7)
        return {CreateObjectInstanceStatus::invalid_caller,{}};
    if(!item||item->kind!=EntityKind::object||item->ui_ghost||item->attachment||!item->definition||
       !request.guid||item->definition->guid!=request.guid)
        return {CreateObjectInstanceStatus::invalid_created_object,{}};
    if(actor->slots[0])return {CreateObjectInstanceStatus::slot_occupied,{}};
    const auto position=actor->position;
    if(position) {
        auto placed=*position;placed.direction=direction;
        if(!world.set_position(object,placed)||!world.place_in_slot(caller,0,object,direction))
            return {CreateObjectInstanceStatus::world_rejected,{}};
    } else {
        if(item->position)return {CreateObjectInstanceStatus::unsupported_position,{}};
        if(!world.declare_object_word(object,1,direction)||!world.attach(caller,0,object))
            return {CreateObjectInstanceStatus::world_rejected,{}};
    }
    return {CreateObjectInstanceStatus::created,object};
}

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
