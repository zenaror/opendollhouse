#include "odh/notify.hpp"
namespace odh {
VmPrimitive notify_idle_primitive(World& world,IdleNotifySink sink,
    std::function<void(std::int16_t,const std::string&)> observed,bool require_source_query) {
    return [&world,sink=std::move(sink),observed=std::move(observed),require_source_query]
        (const VmFrame& frame,const Instruction& i,const auto&,const VmDispatch& dispatch)->PrimitiveOutcome {
        if(i.opcode!=49)return {};
        const auto source_matches=[&]{return (!require_source_query&&!dispatch.origin)||
            (dispatch.origin&&dispatch.origin->matches_active_query(frame,i,dispatch));};
        if(!source_matches())return {PrimitiveDisposition::unsupported,"idle notification source query is stale"};
        const auto* target=world.frame_entity(frame,VmEntityRole::stack_object);
        if(frame.context.entity_references_bound&&frame.context.stack_object_generation&&!target)
            return {PrimitiveDisposition::unsupported,"idle notification strong StackObject body is unavailable"};
        const WorldEntityReference identity=target?WorldEntityReference{target->id,target->generation}:WorldEntityReference{};
        const auto indexed=[&]{return identity.id&&world.is_live_reference(identity)&&!world.reference_is_dead(identity);};
        if(target&&!indexed())return {PrimitiveDisposition::unsupported,
            "idle notification retained/dead StackObject thread availability is unknown"};
        std::string status=!target?"missing_object":"no_thread";
        if(indexed()&&sink){
            const auto accepted=sink(identity.id);
            if(!source_matches()||!indexed())return {PrimitiveDisposition::unsupported,
                "idle notification participant changed during scheduler callback"};
            status=accepted?"notified":"no_thread";
        }
        // Observation is diagnostic only. It cannot change the opcode's branch.
        if(observed)try{observed(frame.context.stack_object,status);}catch(...){}
        // Fixed reference returns true even if StackObject or its Thread is null.
        return {PrimitiveDisposition::goto_true,{}};
    };
}
}
