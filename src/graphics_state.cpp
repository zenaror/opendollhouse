#include "odh/graphics_state.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace odh {
namespace {
void validate(const Entity& entity) {
    if(entity.id<=0 || !entity.generation || !entity.position ||
       entity.position->level<=0 || entity.position->direction>7 ||
       (entity.kind!=EntityKind::object && entity.kind!=EntityKind::avatar))
        throw FormatError("graphics entity has missing or invalid state");
}
std::array<float,3> free_position(EntityKind kind,const EntityPosition& p) {
    const auto offset=kind==EntityKind::object?0.5f:0.0f;
    std::array<float,3> result{static_cast<float>(p.x)/16.0f-offset,static_cast<float>(p.y)/16.0f-offset,static_cast<float>(p.level-1)*2.95f};
    for(const auto n:result)if(!std::isfinite(n))throw FormatError("graphics position is not finite");
    return result;
}
float coordinate(std::uint32_t bits) {
    float n;static_assert(sizeof(n)==sizeof(bits)&&std::numeric_limits<float>::is_iec559);std::memcpy(&n,&bits,sizeof(n));
    if(!std::isfinite(n))throw FormatError("graphics SLOT offset is not finite");
    return n/16.0f;
}
}
GraphicsEntity graphics_entity(const World& world,std::int16_t id,GraphicsSlotResolver resolver) {
    const auto* child=world.find(id);
    if(!child)throw FormatError("graphics entity does not exist");
    validate(*child);
    GraphicsEntity result{child->id,child->generation,child->kind,
                          free_position(child->kind,*child->position),child->position->direction,0};
    if(!child->attachment)return result;
    const auto attachment=*child->attachment;
    const auto child_position=*child->position;
    if(child->kind==EntityKind::avatar && !attachment.positioned && !child->saved_object_banks)
        throw FormatError("graphics containment profile is unsupported");
    const auto* parent=world.find(attachment.container);
    if(!parent)throw FormatError("graphics container does not exist");
    validate(*parent);
    if(parent->kind!=EntityKind::object || parent->attachment ||
       attachment.slot>=parent->slots.size() || parent->slots[attachment.slot]!=id)
        throw FormatError("graphics container relation is unsupported or inconsistent");
    if(!resolver)throw FormatError("graphics container SLOT resolver is missing");
    const auto parent_generation=parent->generation;
    const auto parent_position=*parent->position;
    const auto parent_id=parent->id;
    const auto* table=resolver(*parent);
    // Resolver callbacks can invalidate entities even though this API is read-only.
    // Reacquire both identities before accessing any earlier entity pointer.
    child=world.find(id);parent=world.find(parent_id);
    if(!child || !parent || child->generation!=result.generation ||
       parent->generation!=parent_generation || child->kind!=result.kind ||
       parent->kind!=EntityKind::object || !child->position || !parent->position ||
       !(*child->position==child_position) || !(*parent->position==parent_position) ||
       !child->attachment || child->attachment->container!=parent_id ||
       child->attachment->slot!=attachment.slot ||
       (child->kind==EntityKind::avatar && !child->attachment->positioned && !child->saved_object_banks) ||
       parent->attachment || attachment.slot>=parent->slots.size() || parent->slots[attachment.slot]!=id)
        throw FormatError("graphics SLOT resolver invalidated entity state");
    if(!table)throw FormatError("graphics container SLOT table is missing");
    if(table->version<4 || table->version>10 || table->records.size()>4096)
        throw FormatError("graphics container SLOT table is unsupported");
    const auto* slot=slot_of_type(*table,0,attachment.slot);
    if(!slot)throw FormatError("graphics storage SLOT ordinal is missing");
    auto x=coordinate(slot->offset_bits[0]),y=coordinate(slot->offset_bits[1]);
    switch(parent_position.direction) {
        case 2:{const auto old_x=x;x=-y;y=old_x;break;}
        case 4:x=-x;y=-y;break;
        case 6:{const auto old_x=x;x=y;y=-old_x;break;}
        default:break; // ObjectComponent uses north rotation for diagonals.
    }
    result.tile_position=free_position(EntityKind::object,parent_position);
    const float avatar_offset=child->kind==EntityKind::avatar?0.5f:0.0f;
    result.tile_position[0]+=avatar_offset+x;result.tile_position[1]+=avatar_offset+y;
    if(child->kind==EntityKind::object) {
        const auto height=slot->height.value_or(0);
        if(height<0 || height>9)throw FormatError("graphics SLOT height unsupported");
        constexpr std::array<float,9> heights{0,2.5f,4,4,0,0,7,4,0};
        const float z=(height==0 || height==5)?coordinate(slot->offset_bits[2])*16.0f/5.0f:
            heights[static_cast<std::size_t>(height-1)]/5.0f;
        result.tile_position[2]+=z;
    }
    for(const auto n:result.tile_position)if(!std::isfinite(n))
        throw FormatError("graphics SLOT position arithmetic overflow");
    result.container=parent_id;
    return result;
}
} // namespace odh
