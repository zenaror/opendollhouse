#include "odh/look_towards.hpp"

namespace odh {

VmPrimitive look_towards_primitive(World& world) {
    return look_towards_primitive(static_cast<const World&>(world),world.vm_memory());
}
VmPrimitive look_towards_primitive(const World& world,VmMemory output) {
    return [&world,output=std::move(output)](const VmFrame& frame,const Instruction& instruction,const auto&,const VmDispatch& dispatch)
        ->PrimitiveOutcome {
        if(instruction.opcode!=22)return {};
        const auto mode=instruction.operands[0];
        const auto* avatar=world.frame_entity(frame,VmEntityRole::caller,true);
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
        if(!output.write||!output.write(frame,18,41,target)||
           !output.write(frame,18,42,1)||
           !output.write(frame,18,43,0)||
           !output.write(frame,18,44,1)||
           !output.write(frame,18,45,0))
            return {PrimitiveDisposition::unsupported,"LookTowards head-target write rejected"};
        return {PrimitiveDisposition::goto_true,{}};
    };
}

}
