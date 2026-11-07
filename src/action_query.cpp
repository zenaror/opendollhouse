// SPDX-License-Identifier: MPL-2.0
// VMTestSimInteractingWith, pinned FreeSO7e76c1960252ddd033d8937d5edb47d326f5b69c.
#include "odh/action_query.hpp"
namespace odh {
VmPrimitive test_sim_interacting_with_primitive(const World& world,ActionQueryThreadResolver resolver){
    return [&world,resolver=std::move(resolver)](const VmFrame& frame,const Instruction& instruction,
            const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome{
        const auto bad=[](std::string why){return PrimitiveOutcome{PrimitiveDisposition::unsupported,
            "TestSimInteractingWith: "+std::move(why)};};
        if(instruction.opcode!=37||!dispatch.origin||!dispatch.origin->matches_active_query(frame,instruction,dispatch))
            return bad("authenticated executing query dispatch required");
        const auto* caller=world.frame_entity(frame,VmEntityRole::caller,true);
        if(!caller||!resolver)return bad("live Caller thread resolver unavailable");
        const auto generation=caller->generation;
        try{
            const auto context=resolver(caller->id,generation);
            caller=world.frame_entity(frame,VmEntityRole::caller,true);
            if(!caller||caller->generation!=generation||!context||!context->thread||!context->interactions||
               !dispatch.origin->matches_active_query(frame,instruction,dispatch))
                return bad("persistent Caller thread/queue identity unavailable or changed");
            const auto answer=frame.context.entity_references_bound?
                context->interactions->interacting_with(world,*context->thread,frame.context.caller,
                    vm_context_reference(frame.context,VmEntityRole::stack_object)):
                context->interactions->interacting_with(world,*context->thread,frame.context.caller,frame.context.stack_object);
            if(!answer)return bad("Caller action ancestry or queue identity is unproven");
            return {*answer?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
        }catch(const std::exception& error){return bad(error.what());}
    };
}
}
