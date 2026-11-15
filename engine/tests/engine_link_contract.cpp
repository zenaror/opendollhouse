/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// Exercise the public VM consumer, not individual primitive implementation symbols.
// Directly constructing handlers would hide archive-linking registration failures.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static VMPrimitiveExitCode Execute(VMContext& context, VMStackFrame& frame,
                                  size_t opcode, const std::array<uint8_t, 8>& bytes) {
    auto* registration = context.Primitives.at(opcode).get();
    Require(registration != nullptr, "required gameplay instruction is not linked");
    Require(registration->GetHandler() != nullptr && bool(registration->OperandModel),
            "required gameplay instruction is incomplete");
    std::unique_ptr<VMPrimitiveOperand> operand(registration->OperandModel());
    operand->Read(bytes.data());
    return registration->GetHandler()->Execute(&frame, operand.get());
}

int main() {
    try {
        VMContext context(nullptr);
        VM vm(&context, nullptr);
        vm.TS1 = true;
        vm.GlobalState.resize(64, 0);
        VMThread thread(&context, nullptr, 8);
        VMStackFrame frame;
        frame.Thread = &thread;
        frame.Args = {1};

        // A game's wait instruction must yield a tick; a missing handler used to
        // report success immediately and spin controllers until the reset guard.
        const std::array<uint8_t, 8> wait{};
        Require(Execute(context, frame, 0, wait) == VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
                "waiting did not yield to the next tick");
        Require(frame.Args[0] == 0, "waiting did not consume one tick");
        Require(Execute(context, frame, 0, wait) == VMPrimitiveExitCode::GOTO_TRUE,
                "finished waiting did not continue the routine");

        // The TS1 registration replaces the online-game transfer instruction.
        // Spend five from a household with 100, then reject an unaffordable 200.
        vm.SetGlobalValue(13, 100);
        vm.SetGlobalValue(14, 0);
        std::array<uint8_t, 8> purchase{0, 0, 5, 0, 0, 0, 0, 0};
        Require(Execute(context, frame, 25, purchase) == VMPrimitiveExitCode::GOTO_TRUE,
                "affordable household purchase failed");
        Require(vm.GetGlobalValue(13) == 95 && vm.GetGlobalValue(14) == 0,
                "household purchase did not debit the budget");
        purchase[2] = 200;
        Require(Execute(context, frame, 25, purchase) == VMPrimitiveExitCode::GOTO_FALSE,
                "unaffordable purchase was accepted");
        Require(vm.GetGlobalValue(13) == 95 && vm.GetGlobalValue(14) == 0,
                "rejected purchase changed the household budget");
        std::puts("engine link contract: waiting and TS1 household purchases work");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "engine link contract failed: %s\n", error.what());
        return 1;
    }
}
