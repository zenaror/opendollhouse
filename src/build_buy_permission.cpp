// SPDX-License-Identifier: MPL-2.0
#include "odh/build_buy_permission.hpp"

namespace odh {
bool BuildBuyPermission::apply_generic(std::uint8_t mode,const Notify& notify) {
    if(mode!=10&&mode!=11)return false;
    enabled_=mode==11;
    if(notify)notify(enabled_);
    return true;
}
VmPrimitive build_buy_permission_primitive(BuildBuyPermission& state,const BuildBuyPermission::Notify& notify) {
    return [&state,notify](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome {
        if(instruction.opcode!=1||(instruction.operands[0]!=10&&instruction.operands[0]!=11))return {};
        if(!dispatch.origin||!dispatch.origin->matches_active_query(frame,instruction,dispatch))
            return {PrimitiveDisposition::unsupported,"Build/Buy permission requires its actual VM dispatch"};
        state.apply_generic(instruction.operands[0],notify);
        return {PrimitiveDisposition::goto_true,{}};
    };
}
std::vector<std::uint8_t> encode_build_buy_permission(BuildBuyPermissionSnapshot value) {
    return {1,0,static_cast<std::uint8_t>(value.enabled?1:0)};
}
BuildBuyPermissionSnapshot decode_build_buy_permission(const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()!=3||bytes[0]!=1||bytes[1]!=0||bytes[2]>1)
        throw FormatError("unsupported or noncanonical Build/Buy permission snapshot");
    return {bytes[2]!=0};
}
}
