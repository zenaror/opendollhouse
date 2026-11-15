/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Primitives.VMTS1Budget — see VMTS1Budget.h
// TS1(sims): translated from FreeSO (VMTS1Budget.cs, MPL-2.0); budget storage adapted to this port.
#include "simantics/primitives/VMTS1Budget.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMMemory.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "simantics/VM.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/engine/VMThread.h"
#include <cstdio>

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;
using FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood;

int32_t VMTS1Budget::GetBudget(FSO::SimAntics::VM* vm)
{
    auto fam = VMTS1Neighbourhood::CurrentFamily(vm);
    if (vm->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY) > 0 || fam == nullptr)
        return (int32_t)vm->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_DIV) * 10000 + vm->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD);
    return fam->Budget;
}

void VMTS1Budget::SetBudget(FSO::SimAntics::VM* vm, int32_t budget)
{
    vm->SetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD, (int16_t)(budget % 10000));
    vm->SetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_DIV, (int16_t)(budget / 10000));
    auto fam = VMTS1Neighbourhood::CurrentFamily(vm);
    if (fam != nullptr) fam->Budget = budget;
}

VMPrimitiveExitCode VMTS1Budget::Execute(VMStackFrame* context, VMPrimitiveOperand* args)
{
    if (!context->VM()->TS1) return TSO.Execute(context, args);
    auto operand = static_cast<VMTransferFundsOperand*>(args);
    auto amount = Utils::VMMemory::GetBigVariable(context, operand->GetAmountOwner(), (int16_t)operand->AmountData);

    if ((operand->Flags & VMTransferFundsFlags::Subtract) > 0) amount = -amount; //instead of subtracting, we're adding
                                                                                //weird terms for the flags here but ts1 is inverted
                                                                                //amount contains the amount we are subtracting from the budget.

    auto oldBudget = GetBudget(context->VM());
    auto newBudget = oldBudget - amount;
    if (oldBudget < 0) return VMPrimitiveExitCode::GOTO_FALSE;
    // TS1(sims): FreeSO lets the test pass even when the purchase overdraws; the game refuses it.
    if (newBudget < 0 && amount > 0) return VMPrimitiveExitCode::GOTO_FALSE;
    if ((operand->Flags & VMTransferFundsFlags::JustTest) == 0) {
        SetBudget(context->VM(), newBudget);
        // Log only committed transfers, never affordability/menu probes. The
        // caller/action identity distinguishes autonomous NPC charges from the
        // player's interaction without guessing from a money headline.
        if (newBudget != oldBudget) {
            const auto caller = context->Caller;
            const auto callee = context->Callee;
            const auto action = context->Thread ? context->Thread->ActiveAction() : nullptr;
            const auto guid = [](const VMEntity* entity) {
                return entity && entity->Object && entity->Object->OBJ ? entity->Object->OBJ->GUID : 0u;
            };
            std::printf("[budget] family_global=%d caller{oid=%d guid=%08x} callee{oid=%d guid=%08x} "
                        "action{uid=%u tree=%04x} routine=%04x ip=%u amount=%d old=%d new=%d expense=%u\n",
                int(context->VM()->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY)),
                caller ? int(caller->ObjectID) : 0, unsigned(guid(caller)),
                callee ? int(callee->ObjectID) : 0, unsigned(guid(callee)),
                action ? unsigned(action->UID) : 0u,
                action && action->ActionRoutine ? unsigned(action->ActionRoutine->ID) : 0u,
                context->Routine ? unsigned(context->Routine->ID) : 0u,
                unsigned(context->InstructionPointer), int(amount), int(oldBudget), int(newBudget),
                unsigned(operand->ExpenseType));
        }
    }
    return VMPrimitiveExitCode::GOTO_TRUE;

    //ts1 does have expense types, which could be used for expenses monitoring (i do not think ts1 had this)
}

}}}
