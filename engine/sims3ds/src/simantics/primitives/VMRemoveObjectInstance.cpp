/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Engine.Primitives.VMRemoveObjectInstance (primitives/VMRemoveObjectInstance.cs)
#include "simantics/primitives/VMRemoveObjectInstance.h"
#include <algorithm>
#include <stdexcept>
#include "core/io.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;

VMPrimitiveExitCode VMRemoveObjectInstance::Execute(VMStackFrame* context, VMPrimitiveOperand* args)
{
    auto operand = static_cast<VMRemoveObjectInstanceOperand*>(args);
    VMEntity* obj;
    if (operand->Target == 0) obj = context->Caller;
    else obj = context->StackObject;
    const bool ts1=context->VM()->TS1;
    const bool immediate=ts1 && context->VM()->TS1ScriptImmediateMode();
    if (ts1) {
        // DVD 451baa accepts selectors0..2; selector2 leaves target ID zero.
        if (operand->Target<0 || operand->Target>2)
            throw std::runtime_error("Remove Object Instance: target out of range");
        if (operand->Target==2) obj=nullptr;
        if (!obj) return VMPrimitiveExitCode::GOTO_FALSE;
    }

    // FIX(sims): FreeSO always deletes the whole multitile group (`obj?.Delete(true, ...)`). FreeSims passed the
    // CleanupAll flag, so a vehicle removing itself without it left its other tiles behind: every Old Town / community
    // visitor trolley (24 tiles) piled up out of world (15 trolleys = 360 orphan objects after 8 game hours).
    // Source442db0 submits every script request before451c51 checks the
    // global interpreter mode. Direct rollback/UI Delete is a separate API.
    if (obj != nullptr && !context->VM()->QueueTS1Removal(obj,ts1))
        obj->Delete(true, context->VM()->Context);

    //if (obj == context.StackObject) context.StackObject = null;

    // FIX(sims): FreeSO "yield if we are going to delete" — an object that removes itself stops running this
    // tick (the expansion pack controllers remove themselves on lots they do not belong to and then idle forever;
    // without the yield their next sleep never yields and the thread hits the infinite loop guard).
    // Source451c48..6c suspends self-removal only in ordinary execution and
    // when flag0 is clear. Immediate474b22 clears that execution-mode byte;
    // its removal request follows TRUE without suspending or deleting inline.
    const bool yieldSelf=obj && obj==context->Caller &&
        (!ts1 || (!immediate && !operand->ReturnImmediately()));
    return yieldSelf ? VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK : VMPrimitiveExitCode::GOTO_TRUE;
}

void VMRemoveObjectInstanceOperand::Read(const uint8_t* bytes)
{
    st::Reader io(bytes, 8, st::Endian::Little);
    Target = io.s16();
    Flags = io.u8();
}

void VMRemoveObjectInstanceOperand::Write(uint8_t* bytes)
{
    st::Writer io(st::Endian::Little);
    io.WriteInt16(Target);
    io.WriteByte(Flags);
    auto& buf = io.ToArray();
    std::copy(buf.begin(), buf.begin() + std::min<size_t>(buf.size(), OPERAND_SIZE), bytes);
}

}}}
