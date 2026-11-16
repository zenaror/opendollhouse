/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// The Sims 1 opcode30 executes the target and TTAB index selected by opcode3.
// Original PC DVD: 0045a9c5 resolves the entry, passes four zero arguments to
// its action function, installs the active record and transfers control. The
// child result selects this instruction's true/false branch when it returns.
// Idle For Input instead retries its own instruction after a pushed action.
#pragma once
#include <cstdint>
#include "simantics/engine/VMPrimitiveHandler.h"
#include "simantics/engine/VMPrimitiveOperand.h"

namespace FSO { namespace SimAntics { namespace Primitives {

class VMGosubFoundAction : public Engine::VMPrimitiveHandler {
public:
    Engine::VMPrimitiveExitCode Execute(Engine::VMStackFrame* context, Engine::VMPrimitiveOperand* args) override;
};

class VMGosubFoundActionOperand : public Engine::VMPrimitiveOperand {
public:
    void Read(const uint8_t* bytes) override;
    void Write(uint8_t* bytes) override;
};

}}}
