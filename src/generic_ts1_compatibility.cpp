// SPDX-License-Identifier: MPL-2.0
// Reference: FreeSO7e76c1960252ddd033d8937d5edb47d326f5b69c,
// TSOClient/tso.simantics/Primitives/VMGenericTS1Call.cs. Operand.Read411
// reads one byte. Execute330 returns GOTO_TRUE after unimplemented modes.
#include "odh/generic_ts1_compatibility.hpp"
namespace odh {
GenericTs1CompatibilityEffect generic_ts1_compatibility_effect(std::uint8_t mode) noexcept {
    switch(mode){
    // These source comments/enum names are NOT implementations of the original
    // tutorial, camera, queue, outfit, fame, UI, token or headline operations.
    case 0:  // HouseTutorialComplete: source24, no case.
    case 3:  // PullDownTaxiDialog: source43, comment only; no dialog/UI effects.
    case 7:  // MakeNewNeighbor: source73-76, deprecated/comment only.
    case 8:  // FamilyTutorialComplete: source73-76, comment only.
    case 9:  // ArchitectureTutorialComplete: source76, comment only.
    case 12: // GetDistanceToCameraInTemp0: source87, comment only; Temp0 unchanged.
    case 13: // AbortInteractions: source88, comment only; no queue cancellation.
    case 16: // ChangeNormalOutfit: source96, comment only; no outfit changes.
    case 22: // SelectDowntownLot: source208-210, explicit return without effects.
    case 23: // GetDowntownTimeFromSOInventory: source211-216, reads are comments; Temps unchanged.
    case 25: // SaveSimPersistentData: source227-229, explicit no-op; no persistence permission.
    case 34: // PromoteFameIfNeeded: source298, comment only; no fame changes.
    case 35: // TakeTaxiHook: source299-301, explicit break without effects.
    case 36: // DemoteFameIfNeeded: source302, comment only; no fame changes.
    case 37: // CancelPieMenu: source303, comment only; no UI changes.
    case 38: // GetTokensFromString: source304, comment only; no parsing/output.
    case 39: // ChildToAdult: source305, comment only; no growth/skin effects.
    case 40: // PetToAdult: source306, comment only; no growth effects.
    case 41: // HeadFlush: source307, comment only; no headline state changes.
        return GenericTs1CompatibilityEffect::no_effect;
    case 14: // HouseRadioStationEqualsTemp0: source90-92, SetGlobalValue(31,Temp0).
        return GenericTs1CompatibilityEffect::global_state;
    default:return GenericTs1CompatibilityEffect::unsupported;
    }
}
bool generic_ts1_compatibility_mode(std::uint8_t mode) noexcept {
    return generic_ts1_compatibility_effect(mode)!=GenericTs1CompatibilityEffect::unsupported;
}
VmPrimitive generic_ts1_compatibility_primitive(){
    return [](const VmFrame&,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch&)->PrimitiveOutcome{
        if(instruction.opcode==1){
            const auto effect=generic_ts1_compatibility_effect(instruction.operands[0]);
            if(effect==GenericTs1CompatibilityEffect::no_effect)return {PrimitiveDisposition::goto_true,{}};
            if(effect==GenericTs1CompatibilityEffect::global_state){
                PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};
                result.variable_write=VmVariableWrite{6,31,temps[0]};
                return result;
            }
        }
        return {PrimitiveDisposition::unsupported,"GenericTS1 mode is outside the finite reference compatibility profile"};
    };
}
} // namespace odh
