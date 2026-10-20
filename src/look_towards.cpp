#include "odh/look_towards.hpp"

namespace odh {

VmPrimitive look_towards_primitive(World& world) {
    return [&world](const VmFrame& frame,const Instruction& instruction,const auto&,const VmDispatch&)
        ->PrimitiveOutcome {
        if(instruction.opcode!=22)return {};
        const auto mode=instruction.operands[0];
        if(mode!=0)return {PrimitiveDisposition::unsupported,"LookTowards mode outside head-target profile"};
        auto* avatar=world.find(frame.context.caller);
        if(!avatar||avatar->kind!=EntityKind::avatar||avatar->person_data.size()<=45)
            return {PrimitiveDisposition::unsupported,"LookTowards head target requires avatar PersonData through45"};
        const auto target=frame.context.stack_object;
        // FreeSO 4c6b3e8 TS1 mode 0 records a persistent head-seek target and
        // policy; this is not yet confirmed against the original executable,
        // and the renderer/simulation does not animate the head toward it.
        if(!world.set_person_data(avatar->id,41,target)||
           !world.set_person_data(avatar->id,42,1)||
           !world.set_person_data(avatar->id,43,0)||
           !world.set_person_data(avatar->id,44,1)||
           !world.set_person_data(avatar->id,45,0))
            return {PrimitiveDisposition::unsupported,"LookTowards head-target write rejected"};
        return {PrimitiveDisposition::goto_true,{}};
    };
}

}
