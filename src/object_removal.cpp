#include "odh/object_removal.hpp"
#include <exception>

namespace odh {
bool publish_native_object_removal_policy(World& world,const ObjectRemovalRequest& request){
    return world.set_suppress_referenced_reset({request.target,request.generation},(request.flags&2U)!=0);
}
bool mark_native_object_removal_positions(World& world,const std::vector<WorldEntityReference>& members){
    if(members.empty())return false;
    for(std::size_t n=0;n<members.size();++n){
        if(!world.is_live_reference(members[n]))return false;
        for(std::size_t previous=0;previous<n;++previous)if(members[n]==members[previous])return false;
    }
    for(const auto member:members)if(!world.set_skip_native_recovery_position(member,true))return false;
    return true;
}
VmPrimitive object_removal_primitive(World& world,ObjectRemovalSchedule schedule) {
    return [&world,schedule=std::move(schedule)](const VmFrame& frame,const Instruction& instruction,
        const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome {
        if(instruction.opcode!=18)return {};
        if(!dispatch.origin||!dispatch.origin->matches_active_query(frame,instruction,dispatch))
            return {PrimitiveDisposition::unsupported,"object removal requires its actual VM dispatch"};
        const auto selector=static_cast<std::uint16_t>(instruction.operands[0]|
            (static_cast<unsigned>(instruction.operands[1])<<8));
        const auto* source_caller=world.frame_entity(frame,VmEntityRole::caller);
        const auto* entity=selector==0?source_caller:world.frame_entity(frame,VmEntityRole::stack_object);
        if(frame.context.entity_references_bound&&
           ((frame.context.caller_generation&&!source_caller)||
            (selector!=0&&frame.context.stack_object_generation&&!entity)))
            return {PrimitiveDisposition::unsupported,"object removal exact entity reference is unavailable"};
        // Reference execution compares entity pointers, including null==null.
        // A retained old generation must never select its numeric replacement.
        const bool caller=entity==source_caller;
        const auto accepted=caller ? PrimitiveDisposition::goto_true_next_tick : PrimitiveDisposition::goto_true;
        if(!entity||entity->dead)return {accepted,{}};
        if(!schedule)return {PrimitiveDisposition::unsupported,"object removal scheduler is absent"};
        const ObjectRemovalRequest request{entity->id,entity->generation,instruction.operands[2],caller};
        try {
            const auto failure=schedule(request,dispatch);
            if(failure)return {PrimitiveDisposition::unsupported,*failure};
        } catch(const std::exception& error) {
            return {PrimitiveDisposition::unsupported,std::string("object removal scheduler raised: ")+error.what()};
        } catch(...) {
            return {PrimitiveDisposition::unsupported,"object removal scheduler raised a non-standard exception"};
        }
        return {accepted,{}};
    };
}
} // namespace odh
