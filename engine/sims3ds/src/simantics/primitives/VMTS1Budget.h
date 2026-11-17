// FSO.SimAntics.Primitives.VMTS1Budget (FreeSO tso.simantics/Primitives/VMTS1Budget.cs)
/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// TS1(sims): The Sims 1 primitive 25 "Alter Budget". Like TSO transfer funds (same operand), but it only
// adds to / takes from the family budget. FreeSims registers the TSO VMTransferFunds (object budgets through the
// TSO global link) for opcode 25; in TS1 the money is the lot family's, kept in globals 13/14 "Budget Mod/Div 10000"
// while the family is on its lot (VMTS1FamilyLoader) and in FAMI.Budget. Used by the expansion packs' shops, food
// stands, tips, rentals, Vacation tokens, Superstar pay, Makin' Magic...
#pragma once
#include "simantics/primitives/VMTransferFunds.h"
#include <array>

namespace FSO { namespace SimAntics { class VM; } }
namespace FSO { namespace SimAntics { namespace Primitives {

//like tso transfer funds, but really scaled back (only deduct from maxis)
class VMTS1Budget : public Engine::VMPrimitiveHandler {
public:
    Engine::VMPrimitiveExitCode Execute(Engine::VMStackFrame* context, Engine::VMPrimitiveOperand* args) override;
    using DaysType = std::array<std::array<int32_t, 8>, 6>;
    // Installs the classic lot state without a transaction or changing FAMI.
    static void LoadState(FSO::SimAntics::VM* vm, int32_t purse, const DaysType& days);
    // Raw purse is distinct from the Global32==1 script getter sentinel.
    static int32_t GetRawBudget(FSO::SimAntics::VM* vm);
    static int32_t GetBudget(FSO::SimAntics::VM* vm);
    // Administrative/family synchronization: no ledger post.
    static void SetBudget(FSO::SimAntics::VM* vm, int32_t budget);
    // Source update: caller checks affordability; Global32==1 suppresses commit.
    // Selector 8 intentionally changes purse only, with an explicit diagnostic.
    static bool Commit(FSO::SimAntics::VM* vm, uint8_t expense, int32_t amount);
    // Source copies history in ascending order, propagating prior history[0].
    static void RolloverDay(FSO::SimAntics::VM* vm);
private:
    VMTransferFunds TSO; // non-TS1 VMs keep FreeSims' behaviour
};

}}}
