#include "odh/snap.hpp"
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
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
bool zero_offset(const SlotRecord& slot) {
    for(const auto bits:slot.offset_bits)if((bits&0x7fffffffU)!=0)return false;
    return true;
}
double source_round_even(double value) {
    const auto lower=std::floor(value),fraction=value-lower;
    return fraction<0.5?lower:fraction>0.5?lower+1:
        std::fmod(std::abs(lower),2.0)==0?lower:lower+1;
}
constexpr double pi=3.141592653589793238462643383279502884;
std::uint8_t source_snap_notch(float radians) {
    // VMSnap uses a float-rounded 2*pi modulus here, unlike the parser's
    // double modulus used to freeze the continuous direction.
    const auto modulus=static_cast<double>(static_cast<float>(pi)*2.0f);
    const auto positive=std::fmod(std::fmod(static_cast<double>(radians),modulus)+modulus,modulus);
    return static_cast<std::uint8_t>(static_cast<unsigned>(source_round_even(positive/(pi/4)))%8U);
}
bool stair_exact_point(const SlotRecord& route) {
    return route.snap_target_slot==-1 && route.flags==2048U &&
       route.offset_bits==std::array<std::uint32_t,3>{0,0,0} &&
       route.standing==0 && route.sitting==0 && route.ground==0 && route.height==0 &&
       route.facing==-2 && route.resolution==16 && route.max_size==0 && route.unknown_i10==0 &&
       route.gradient_bits==0x3E400000 && route.proximity &&
       *route.proximity==std::array<std::int32_t,3>{16,16,16};
}
bool shoo_sitting_exit(const SlotRecord& route) {
    return route.snap_target_slot==-1 && route.flags==0 &&
       route.offset_bits==std::array<std::uint32_t,3>{0,0xC1800000,0} &&
       route.standing==0 && route.sitting==0 && route.ground==0 && route.height==0 &&
       route.facing==-2 && route.resolution==16 && route.max_size==0 && route.unknown_i10==0 &&
       route.gradient_bits==0x3E400000 && route.proximity &&
       *route.proximity==std::array<std::int32_t,3>{16,16,16};
}
}
Snap::Snap(World& world,std::int16_t object,ObjdBinding binding,const SlotTable& slots,std::uint16_t slot_id,
           PlacementValidator placement_validator,StairPlacementValidator stair_placement_validator,
           bool global_slot_table,DirectionalServices directional_services)
    :world_(world),object_(object),generation_(0),binding_(binding),slots_(slots),
     placement_validator_(std::move(placement_validator)),
     stair_placement_validator_(std::move(stair_placement_validator)),global_slot_table_(global_slot_table),
     directional_services_(std::move(directional_services)) {
    const auto* entity=world_.find(object);
    const bool entity_kind_valid=entity&&(entity->kind==EntityKind::object||
        (global_slot_table_&&entity->kind==EntityKind::avatar));
    const bool slot_binding_valid=global_slot_table_?slot_id==100:binding.slot_table==slot_id;
    if(!entity_kind_valid || !slot_id || !slot_binding_valid)
        throw std::invalid_argument("snap owner or SLOT binding mismatch (object="+std::to_string(object)+
            ", slot="+std::to_string(slot_id)+", bound="+std::to_string(binding.slot_table)+")");
    generation_=entity->generation;
}
bool Snap::bind(const Vm& vm) {if(vm_ && vm_!=&vm)return false;vm_=&vm;return true;}
PrimitiveOutcome Snap::directional_snap(const VmFrame& frame,const Instruction& instruction,
    const VmDispatch& dispatch,const SlotRecord& slot) {
    const auto fail=[](const std::string& why){return PrimitiveOutcome{PrimitiveDisposition::unsupported,why};};
    if(!vm_||dispatch.origin!=vm_||dispatch.check_mode||!vm_->matches_active_primitive(frame,instruction,dispatch))
        return fail("directional Snap requires the exact active opcode46 dispatch");
    if(slots_.version!=10||!slot.facing||*slot.facing<-3||*slot.facing>7||slot.sitting!=0||
       (slot.flags&65536U)||(instruction.operands[4]&2U))
        return fail("directional Snap requires a zero-offset, nonsitting, nonaverage, NoShoo source profile");
    if(!directional_services_.candidate||!directional_services_.place)
        return fail("directional Snap source candidate and final placement services are unavailable");
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller,true);const auto* target=world_.find(object_);
    if(!actor||!target||actor->ui_ghost||target->ui_ghost||!actor->position||!target->position||
       actor->kind!=EntityKind::avatar||target->generation!=generation_||target->position->level>127||
       !std::isfinite(actor->radian_direction))return fail("directional Snap requires exact live actor and source identities");
    const auto actor_id=actor->id;const auto actor_generation=actor->generation;
    const auto actor_position=*actor->position;const auto actor_attachment=actor->attachment;
    const auto actor_angle=actor->radian_direction;
    const float target_angle=target->kind==EntityKind::avatar?target->radian_direction:
        static_cast<float>(static_cast<double>(target->position->direction)*(pi/4));
    if(!std::isfinite(target_angle))return fail("directional Snap target angle is not finite");
    // onPoint is computed BEFORE Facing contributes its bit to Flags. Absolute
    // changes the parser base only when the original low direction mask exists.
    const bool on_point=(slot.flags&255U)==0;
    const auto base=((slot.flags&512U)&&!on_point)?0.0f:target_angle;
    auto flags=slot.flags;
    if(*slot.facing>=0)flags|=std::uint32_t{1}<<static_cast<unsigned>(*slot.facing);
    else if((flags&255U)==0)flags|=1U;
    const auto mask=flags&255U;
    const auto flag_angle=source_round_even(std::log2(static_cast<double>(mask)))*(pi/4);
    auto angle=std::fmod(std::fmod(static_cast<double>(base)+flag_angle,pi*2)+pi*2,pi*2);
    if(angle>pi)angle-=pi*2;
    const auto continuous=static_cast<float>(angle);
    auto candidate=*target->position;candidate.direction=0;
    auto destination=candidate;destination.direction=source_snap_notch(continuous);
    const auto identity=[&](){
        const auto* a=world_.find(actor_id);const auto* t=world_.find(object_);
        return a&&t&&!a->ui_ghost&&!t->ui_ghost&&a->generation==actor_generation&&t->generation==generation_&&
            vm_->matches_active_primitive(frame,instruction,dispatch);
    };
    const auto unchanged_actor=[&](){const auto* a=world_.find(actor_id);
        return identity()&&a->position&&*a->position==actor_position&&a->radian_direction==actor_angle&&
            same_attachment(a->attachment,actor_attachment);
    };
    const auto valid_result=[&](const SourcePlacementResult& result){
        if(result.status==SourcePlacementStatus::accepted||result.status==SourcePlacementStatus::out_of_bounds)
            return result.blocker==0&&result.blocker_generation==0;
        if(result.status==SourcePlacementStatus::unsupported)return true;
        // PositionValid may reject floor/altitude with no blocking entity.
        // The parser ignores that rejection; final SetPosition does not.
        if(result.status==SourcePlacementStatus::blocked_object&&result.blocker==0)
            return result.blocker_generation==0;
        const auto* blocker=world_.find(result.blocker);
        return blocker&&!blocker->ui_ghost&&result.blocker_generation&&
            blocker->generation==result.blocker_generation&&
            ((result.status==SourcePlacementStatus::blocked_avatar&&blocker->kind==EntityKind::avatar)||
             (result.status==SourcePlacementStatus::blocked_object&&blocker->kind==EntityKind::object));
    };
    const auto failure=[&](const SourcePlacementResult& result)->PrimitiveOutcome{
        const bool no_goals=result.status==SourcePlacementStatus::out_of_bounds;
        if(!world_.read(frame,3,54)||(no_goals&&!world_.read(frame,3,52)))
            return fail("directional Snap source failure result banks are unavailable");
        if(no_goals&&!world_.write(frame,3,52,2))return fail("directional Snap PrimitiveResult write rejected");
        if(!world_.write(frame,3,54,result.blocker))return fail("directional Snap PrimitiveResultID write rejected");
        return {PrimitiveDisposition::goto_false,{}};
    };
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    try {
        const auto verified=directional_services_.candidate(frame,instruction,dispatch,actor_id,object_,candidate);
        if(!unchanged_actor()||!valid_result(verified))return fail("directional Snap candidate service changed its source proof or blocker identity");
        if(verified.status==SourcePlacementStatus::unsupported)return fail("directional Snap candidate placement capability is unsupported");
        if((verified.status==SourcePlacementStatus::blocked_object&&verified.blocker!=0)||
           verified.status==SourcePlacementStatus::out_of_bounds)
            return failure(verified);
        // A blocking avatar only lowers the score. With one candidate it is
        // still selected; final default SetPosition makes its own decision.
        const auto placed=directional_services_.place(frame,instruction,dispatch,actor_id,object_,destination);
        if(!identity()||!valid_result(placed))return fail("directional Snap final placement changed its source proof or blocker identity");
        if(placed.status==SourcePlacementStatus::unsupported)return fail("directional Snap final placement capability is unsupported");
        if(placed.status!=SourcePlacementStatus::accepted){
            if(!unchanged_actor())return fail("failed directional Snap did not restore the actor's original transform");
            return failure(placed);
        }
        actor=world_.find(actor_id);
        if(!actor->position||!(*actor->position==destination)||actor->attachment)
            return fail("directional Snap final service accepted without publishing its exact quantized position");
        if(!world_.set_radian_direction(actor_id,continuous))return fail("directional Snap continuous arrival direction rejected");
        return {PrimitiveDisposition::goto_true,{}};
    }catch(const std::exception& error){return fail(std::string("directional Snap placement callback failed: ")+error.what());}
    catch(...){return fail("directional Snap placement callback raised a non-standard exception");}
}
VmPrimitive Snap::primitive() {
    return [this](const VmFrame& frame,const Instruction& i,const auto&,const VmDispatch& dispatch) -> PrimitiveOutcome {
        if(i.opcode!=46) return {};
        const auto fail=[](std::string why){return PrimitiveOutcome{PrimitiveDisposition::unsupported,std::move(why)};};
        if(executing_) return fail("snap placement callback cannot reenter controller");
        if(!vm_ || dispatch.origin!=vm_) return fail("snap controller bound to another VM");
        const auto index=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
        const auto mode=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
        if((mode!=3&&!(global_slot_table_&&mode==4)) ||
           (i.operands[4]!=0 && i.operands[4]!=2 && i.operands[4]!=4 && i.operands[4]!=6) ||
           i.operands[5] || i.operands[6] || i.operands[7])
            return fail("snap operand profile not implemented");
        if(frame.context.entity_references_bound&&!(vm_context_reference(frame.context,VmEntityRole::stack_object)==WorldEntityReference{object_,generation_}))
            return fail("Snap StackObject reference differs from its exact live resource owner");
        const auto* object=world_.find(object_);const auto* actor=world_.frame_entity(frame,VmEntityRole::caller,true);
        if(!object || object->generation!=generation_ || frame.context.stack_object!=object_ ||
           !object->position || object->attachment || !actor || !actor->position || actor->kind!=EntityKind::avatar)
            return fail("snap context or positioned entity binding missing");
        const auto* route=slot_of_type(slots_,3,index);
        if(!route) return {PrimitiveDisposition::goto_false,{}};
        if(route->snap_target_slot==-1&&(route->flags&4096U)&&zero_offset(*route))
            return directional_snap(frame,i,dispatch,*route);
        if(route->snap_target_slot==-1) {
            if(slots_.version==10 && index==0 && binding_.slot_table==128 &&
               i.operands[4]==2 && stair_exact_point(*route)) {
                if(binding_.master_id==0)
                    return fail("stair exact-point Snap requires a nonzero saved multi-tile master ID");
                if(!stair_placement_validator_)
                    return fail("stair exact-point Snap has no saved stair placement validator");
                if(directional_services_.stair_authorized&&
                   !directional_services_.stair_authorized(frame,i,dispatch))
                    return fail("stair exact-point Snap has no authenticated live portal invocation");
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
            if(i.operands[4]==2&&slots_.version==10&&binding_.slot_table==128&&index==1&&
               shoo_sitting_exit(*route)) {
                if(object->position->direction&1) return fail("sitting-exit Snap diagonal object geometry not implemented");
                if(actor->position->level!=object->position->level)
                    return fail("sitting-exit Snap floor level unsupported");
                if(actor->attachment && actor->attachment->container!=object_)
                    return {PrimitiveDisposition::goto_false,{}};
                std::int32_t dx=0,dy=-16;
                for(std::uint8_t turn=0;turn<object->position->direction/2;++turn) {
                    const auto old_x=dx;dx=-dy;dy=old_x;
                }
                const auto object_position=*object->position,actor_position=*actor->position;
                auto destination=object_position;destination.x+=dx;destination.y+=dy;
                destination.direction=actor_position.direction;
                if(destination.x<0 || destination.y<0 || destination.x>32767 || destination.y>32767)
                    return {PrimitiveDisposition::goto_false,{}};
                if(!placement_validator_)return fail("sitting-exit Snap placement validator missing");
                const auto actor_id=actor->id;
                const auto actor_generation=actor->generation;
                struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
                if(!placement_validator_(actor_id,destination))return {PrimitiveDisposition::goto_false,{}};
                object=world_.find(object_);actor=world_.find(actor_id);
                if(!object || object->generation!=generation_ || !object->position || object->attachment ||
                   !(*object->position==object_position) || !actor || actor->generation!=actor_generation ||
                   !actor->position || !(*actor->position==actor_position) ||
                   (actor->attachment && actor->attachment->container!=object_))
                    return {PrimitiveDisposition::goto_false,{}};
                return {world_.place_on_floor(actor_id,destination)?PrimitiveDisposition::goto_true:
                    PrimitiveDisposition::goto_false,{}};
            }
            if(i.operands[4]==2&&!floor_exit(*route))
                return fail("Snap Shoo flag outside the supported sitting or floor-exit profiles");
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
