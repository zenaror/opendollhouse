#include "odh/snap.hpp"
#include <array>
#include <stdexcept>
#include <utility>
namespace odh {
namespace {
struct FloorExit {std::int32_t x,y;std::uint8_t direction;};
std::optional<FloorExit> floor_exit(const SlotRecord& route) {
    if(route.snap_target_slot!=-1 || route.standing || route.sitting || route.ground ||
       route.height!=0 || route.facing!=-2 || route.resolution!=16 || route.max_size!=0 ||
       route.unknown_i10!=0 || route.gradient_bits!=0x3E400000 || !route.proximity ||
       *route.proximity!=std::array<std::int32_t,3>{16,16,16}) return {};
    // Exact finite IEEE-754 +/-16 values only. No rounding or float conversion
    // is needed for the four native chair offsets in 1/16-tile World units.
    constexpr std::uint32_t positive=0x41800000,negative=0xC1800000;
    if(route.flags==4097 && route.offset_bits==std::array<std::uint32_t,3>{0,negative,0}) return FloorExit{0,-16,0};
    if(route.flags==4100 && route.offset_bits==std::array<std::uint32_t,3>{positive,0,0}) return FloorExit{16,0,2};
    if(route.flags==4112 && route.offset_bits==std::array<std::uint32_t,3>{0,positive,0}) return FloorExit{0,16,4};
    if(route.flags==4160 && route.offset_bits==std::array<std::uint32_t,3>{negative,0,0}) return FloorExit{-16,0,6};
    return {};
}
bool same_attachment(const std::optional<Attachment>& a,const std::optional<Attachment>& b) {
    return bool(a)==bool(b) && (!a || (a->container==b->container && a->slot==b->slot && a->positioned==b->positioned));
}
bool stair_exact_point(const SlotRecord& route) {
    return route.snap_target_slot==-1 && route.flags==2048U &&
       route.offset_bits==std::array<std::uint32_t,3>{0,0,0} &&
       route.standing==0 && route.sitting==0 && route.ground==0 && route.height==0 &&
       route.facing==-2 && route.resolution==16 && route.max_size==0 && route.unknown_i10==0 &&
       route.gradient_bits==0x3E400000 && route.proximity &&
       *route.proximity==std::array<std::int32_t,3>{16,16,16};
}
}
Snap::Snap(World& world,std::int16_t object,ObjdBinding binding,const SlotTable& slots,std::uint16_t slot_id,
           PlacementValidator placement_validator,StairPlacementValidator stair_placement_validator)
    :world_(world),object_(object),generation_(0),binding_(binding),slots_(slots),
     placement_validator_(std::move(placement_validator)),
     stair_placement_validator_(std::move(stair_placement_validator)) {
    const auto* entity=world_.find(object);
    if(!entity || entity->kind!=EntityKind::object || !slot_id || binding.slot_table!=slot_id)
        throw std::invalid_argument("snap owner or SLOT binding mismatch (object="+std::to_string(object)+
            ", slot="+std::to_string(slot_id)+", bound="+std::to_string(binding.slot_table)+")");
    generation_=entity->generation;
}
bool Snap::bind(const Vm& vm) {if(vm_ && vm_!=&vm)return false;vm_=&vm;return true;}
VmPrimitive Snap::primitive() {
    return [this](const VmFrame& frame,const Instruction& i,const auto&,const VmDispatch& dispatch) -> PrimitiveOutcome {
        if(i.opcode!=46) return {};
        const auto fail=[](const char* why){return PrimitiveOutcome{PrimitiveDisposition::unsupported,why};};
        if(executing_) return fail("snap placement callback cannot reenter controller");
        if(!vm_ || dispatch.origin!=vm_) return fail("snap controller bound to another VM");
        const auto index=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
        const auto mode=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
        if(mode!=3 || (i.operands[4]!=0 && i.operands[4]!=2 && i.operands[4]!=4) || i.operands[5] || i.operands[6] || i.operands[7])
            return fail("snap operand profile not implemented");
        const auto* object=world_.find(object_);const auto* actor=world_.find(frame.context.caller);
        if(!object || object->generation!=generation_ || frame.context.stack_object!=object_ ||
           !object->position || object->attachment || !actor || !actor->position || actor->kind!=EntityKind::avatar)
            return fail("snap context or positioned entity binding missing");
        const auto* route=slot_of_type(slots_,3,index);
        if(!route) return {PrimitiveDisposition::goto_false,{}};
        if(route->snap_target_slot==-1) {
            if(slots_.version==10 && index==0 && binding_.slot_table==128 &&
               i.operands[4]==2 && stair_exact_point(*route)) {
                if(binding_.master_id!=4 || !stair_placement_validator_)
                    return fail("stair exact-point Snap identity or placement profile unsupported");
                if(actor->attachment || actor->position->level<1 || actor->position->level>2 ||
                   object->position->level<1 || object->position->level>2 ||
                   std::abs(static_cast<int>(actor->position->level)-static_cast<int>(object->position->level))!=1)
                    return fail("stair exact-point Snap requires unattached actor and adjacent floors");
                const auto actor_id=actor->id;
                const auto actor_generation=actor->generation;
                const auto actor_position=*actor->position,object_position=*object->position;
                const auto classify=[&](EntityPosition candidate)->std::optional<PrimitiveOutcome> {
                    const auto result=stair_placement_validator_(actor_id,object_,candidate);
                    if(result==StairPlacementStatus::accepted)return {};
                    if(result==StairPlacementStatus::blocked)
                        return PrimitiveOutcome{PrimitiveDisposition::goto_false,{}};
                    return fail("stair exact-point Snap placement validation unsupported");
                };
                auto north=object_position;north.direction=0;
                struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
                if(auto result=classify(north))return *result;
                auto destination=object_position;destination.direction=actor_position.direction;
                if(auto result=classify(destination))return *result;
                object=world_.find(object_);actor=world_.find(actor_id);
                if(!object || object->generation!=generation_ || !object->position || object->attachment ||
                   !(*object->position==object_position) || !actor || actor->generation!=actor_generation ||
                   !actor->position || !(*actor->position==actor_position) || actor->attachment)
                    return {PrimitiveDisposition::goto_false,{}};
                return {world_.place_on_floor(actor_id,destination)?PrimitiveDisposition::goto_true:
                    PrimitiveDisposition::goto_false,{}};
            }
            if(i.operands[4]==2)return fail("Snap Shoo flag outside the exact stair profile");
            const auto exit=floor_exit(*route);
            if(slots_.version!=10 || !exit) return fail("snap floor SLOT profile not implemented");
            if(!placement_validator_) return fail("snap floor placement validator missing");
            if(object->position->direction&1) return fail("snap floor diagonal object geometry not implemented");
            if(actor->position->level!=object->position->level) return fail("snap floor level unsupported");
            if(actor->attachment && actor->attachment->container!=object_)
                return {PrimitiveDisposition::goto_false,{}};
            auto dx=exit->x,dy=exit->y;
            // Clockwise rotation is exact for cardinal object directions.
            for(std::uint8_t turn=0;turn<object->position->direction/2;++turn) {
                const auto old_x=dx;dx=-dy;dy=old_x;
            }
            const auto object_position=*object->position,actor_position=*actor->position;
            auto destination=object_position;destination.x+=dx;destination.y+=dy;
            destination.direction=static_cast<std::uint8_t>((object_position.direction+exit->direction)%8);
            if(destination.x<0 || destination.y<0 || destination.x>32767 || destination.y>32767)
                return {PrimitiveDisposition::goto_false,{}};
            const auto actor_id=actor->id;const auto actor_generation=actor->generation;
            const auto attachment=actor->attachment;
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            // Observers run before any state mutation. Reacquire entities and
            // check identity/position afterwards so a callback cannot leave us
            // using dangling pointers or place against a changed binding.
            if(!placement_validator_(actor_id,destination)) return {PrimitiveDisposition::goto_false,{}};
            object=world_.find(object_);actor=world_.find(actor_id);
            if(!object || object->generation!=generation_ || !object->position || object->attachment ||
               !(*object->position==object_position) || !actor || actor->generation!=actor_generation ||
               !actor->position || !(*actor->position==actor_position) || !same_attachment(actor->attachment,attachment))
                return {PrimitiveDisposition::goto_false,{}};
            return {world_.place_on_floor(actor_id,destination) ? PrimitiveDisposition::goto_true : PrimitiveDisposition::goto_false,{}};
        }
        if(slots_.version!=10 || route->offset_bits!=std::array<std::uint32_t,3>{0,0,0} ||
           (route->flags!=0 && route->flags!=4096) || route->standing || route->sitting || route->ground || route->snap_target_slot<0)
            return fail("snap route SLOT profile not implemented");
        const auto number=static_cast<std::size_t>(route->snap_target_slot);
        const auto* storage=slot_of_type(slots_,0,number);
        if(!storage) return {PrimitiveDisposition::goto_false,{}};
        if(storage->offset_bits!=std::array<std::uint32_t,3>{0,0,0} || storage->height!=7)
            return fail("snap storage offset/height profile not implemented");
        if(binding_.slot_table==0 || actor->position->level!=object->position->level)
            return fail("snap binding or level unsupported");
        std::optional<std::uint8_t> direction;
        if(route->flags==4096) {
            // VMSlotParser SnapToDirection with no compass bits: negative
            // Facing uses relative NORTH; nonnegative Facing is an offset.
            if(!route->facing || *route->facing<-3 || *route->facing>7) return fail("snap facing outside eight-direction profile");
            const auto relative=*route->facing<0?0:*route->facing;
            direction=static_cast<std::uint8_t>((object->position->direction+relative)%8);
        }
        return {world_.place_in_slot(object_,number,actor->id,direction) ? PrimitiveDisposition::goto_true : PrimitiveDisposition::goto_false,{}};
    };
}
}
