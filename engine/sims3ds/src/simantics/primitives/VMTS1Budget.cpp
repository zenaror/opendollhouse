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
#include "simantics/engine/VMSimanticsException.h"
#include <limits>
#include <cstdlib>
#include <stdexcept>
#include <cstdio>

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;
using FSO::SimAntics::Engine::Utils::VMTS1Neighbourhood;

namespace {
// Map low bits to signed values without implementation-defined narrowing or
// signed overflow. The original x86 arithmetic wraps at 32 bits.
int32_t Signed32(uint32_t bits) {
    return bits <= uint32_t(INT32_MAX) ? int32_t(bits) : int32_t(int64_t(bits) - 0x100000000LL);
}
int16_t Signed16(uint32_t bits) {
    bits &= 0xffffu;
    return bits <= 0x7fffu ? int16_t(bits) : int16_t(int32_t(bits) - 0x10000);
}
void Mirrors(VM* vm, int32_t purse) {
    vm->SetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD, Signed16(uint32_t(purse % 10000)));
    vm->SetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_DIV, Signed16(uint32_t(purse / 10000)));
}
int32_t LegacyPurse(VM* vm) {
    const auto family = VMTS1Neighbourhood::CurrentFamily(vm);
    return (vm->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_CURRENT_FAMILY) > 0 || family == nullptr)
        ? int32_t(vm->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_DIV)) * 10000 + vm->GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD)
        : family->Budget;
}
}

void VMTS1Budget::LoadState(VM* vm, int32_t purse, const DaysType& days) {
    vm->TS1Budget.Purse = purse;
    vm->TS1Budget.Days = days;
    vm->TS1Budget.Initialized = true;
    Mirrors(vm, purse);
}

int32_t VMTS1Budget::GetRawBudget(VM* vm) {
    return vm->TS1Budget.Initialized ? vm->TS1Budget.Purse : LegacyPurse(vm);
}

int32_t VMTS1Budget::GetBudget(VM* vm) {
    return vm->GetGlobalValue(32) == 1 ? 999999 : GetRawBudget(vm);
}

void VMTS1Budget::SetBudget(VM* vm, int32_t purse) {
    vm->TS1Budget.Purse = purse;
    vm->TS1Budget.Initialized = true;
    Mirrors(vm, purse);
    if (auto family = VMTS1Neighbourhood::CurrentFamily(vm))
        if (!vm->TS1Budget.LegacyOwnerRestricted ||
            (vm->TS1Budget.LegacyOwnerID > 0 && int16_t(family->ChunkID)==vm->TS1Budget.LegacyOwnerID))
            family->Budget = purse;
}

bool VMTS1Budget::Commit(VM* vm, uint8_t expense, int32_t amount) {
    if (expense > 8) throw std::out_of_range("TS1 budget expense selector exceeds 8");
    if (vm->GetGlobalValue(32) == 1) return false;
    const int32_t previous = GetRawBudget(vm);
    const int32_t updated = Signed32(uint32_t(previous) - uint32_t(amount));
    if (expense < 8) {
        auto& entry = vm->TS1Budget.Days[0][expense];
        entry = Signed32(uint32_t(entry) + uint32_t(amount));
    } else {
        // Original source writes outside its eight-slot array into a list
        // pointer. Preserve the purse transaction, but do not corrupt memory.
        std::fprintf(stderr, "[ts1-budget] expense 8: purse transaction committed without an out-of-bounds ledger write (amount=%d)\n", int(amount));
    }
    SetBudget(vm, updated);
    return true;
}

void VMTS1Budget::RolloverDay(VM* vm) {
    for (size_t i=2; i<vm->TS1Budget.Days.size(); ++i)
        vm->TS1Budget.Days[i]=vm->TS1Budget.Days[i-1];
    vm->TS1Budget.Days[1]=vm->TS1Budget.Days[0];
    vm->TS1Budget.Days[0].fill(0);
}

VMPrimitiveExitCode VMTS1Budget::Execute(VMStackFrame* context, VMPrimitiveOperand* args) {
    if (!context->VM()->TS1) return TSO.Execute(context, args);
    auto operand = static_cast<VMTransferFundsOperand*>(args);
    const auto expense = static_cast<uint8_t>(operand->ExpenseType);
    if (expense > 8) throw VMSimanticsException("TS1 budget: invalid expense selector", context);
    const auto raw = Utils::VMMemory::GetBigVariable(context, operand->GetAmountOwner(), Signed16(operand->AmountData));
    int32_t amount = Signed16(uint32_t(raw));
    if ((operand->Flags & VMTransferFundsFlags::Subtract) > 0) amount = -amount;
    const auto available = GetBudget(context->VM());
    if (amount >= 0 && amount > available) return VMPrimitiveExitCode::GOTO_FALSE;
    if ((operand->Flags & VMTransferFundsFlags::JustTest) == 0) {
        const bool committed = Commit(context->VM(), expense, amount);
        // Source event 0xda refreshes UI; that native notification remains
        // unmapped here. This opt-in trace is observational only.
        static const bool trace = std::getenv("ODH_TS1_BUDGET_TRACE") != nullptr;
        if (trace && committed) {
            const auto action = context->Thread ? context->Thread->ActiveAction() : nullptr;
            std::fprintf(stderr, "[ts1-budget] caller=%d callee=%d action=%u tree=%u routine=%u pc=%u expense=%u amount=%d before=%d after=%d\n",
                context->Caller ? int(context->Caller->ObjectID) : 0,
                context->Callee ? int(context->Callee->ObjectID) : 0,
                action ? unsigned(action->UID) : 0u,
                action && action->ActionRoutine ? unsigned(action->ActionRoutine->ID) : 0u,
                context->Routine ? unsigned(context->Routine->ID) : 0u,
                unsigned(context->InstructionPointer), unsigned(expense), int(amount), int(available),
                int(GetRawBudget(context->VM())));
        }
    }
    return VMPrimitiveExitCode::GOTO_TRUE;
}

}}}
