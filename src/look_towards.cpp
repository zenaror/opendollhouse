#include "odh/look_towards.hpp"

namespace odh {

VmPrimitive look_towards_primitive(World& world) {
    return [&world](const VmFrame& frame,const Instruction& instruction,const auto&,const VmDispatch& dispatch)
        ->PrimitiveOutcome {
        if(instruction.opcode!=22)return {};
        const auto mode=instruction.operands[0];
        auto* avatar=world.frame_entity(frame,VmEntityRole::caller,true);
        if(!avatar||avatar->kind!=EntityKind::avatar)
            return {PrimitiveDisposition::unsupported,"LookTowards caller is not an avatar"};
        // The pinned reference explicitly succeeds without a body route in
        // camera mode; the other body modes belong to the Routing service.
        if(mode==1)return {PrimitiveDisposition::goto_true,{}};
        if(mode>=2&&mode<=5&&dispatch.check_mode)return {PrimitiveDisposition::goto_false,{}};
        if(mode!=0)return {PrimitiveDisposition::unsupported,"LookTowards body mode requires the owning route service"};
        if(!avatar||avatar->kind!=EntityKind::avatar||avatar->person_data.size()<=45)
            return {PrimitiveDisposition::unsupported,"LookTowards head target requires avatar PersonData through45"};
        const auto target=frame.context.stack_object;
        // FreeSO 7e76c196 TS1 mode 0 records a persistent head-seek target and
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
