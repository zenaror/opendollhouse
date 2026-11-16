/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// TS1(sims): see VMGosubFoundAction.h
#include "simantics/primitives/VMGosubFoundAction.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/VM.h"

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;

VMPrimitiveExitCode VMGosubFoundAction::Execute(VMStackFrame* context, VMPrimitiveOperand* /*args*/)
{
    if (!context->VM()->TS1) return VMPrimitiveExitCode::GOTO_TRUE; // TSO: opcode 30 was "syslog" (no-op)
    // DVD opcode30 executes the selection saved by opcode3. It neither takes
    // a pending command nor repeats its permission/check tree.
    return context->Thread->ExecuteTS1SelectedAction(context);
}

void VMGosubFoundActionOperand::Read(const uint8_t* /*bytes*/) { }
void VMGosubFoundActionOperand::Write(uint8_t* /*bytes*/) { }

}}}
