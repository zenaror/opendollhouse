// SPDX-License-Identifier: MPL-2.0
// Resource-free regression for the DVD owner/shared RNG boundary. Exercises
// real VM::Update -> Tick, admission and Random8 checks; no retail resources or UI.
// This does not establish all original helper draws or classic Init ordering.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMRandomNumber.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Scope = FSO::SimAntics::Engine::Scopes::VMVariableScope;

namespace {
int checks = 0;
void need(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
    ++checks;
}

// Literal controls from the genuine Novato SIMI owner/shared seeds. Expected
// steps and range97 results were computed independently of production helpers
// using the DVD's carry-aware product; no test-side implementation selects a
// stream or calls RandomStep to derive its own expectation.
constexpr uint64_t Owner = UINT64_C(1918150980);
constexpr uint64_t Shared = UINT64_C(3784694001);
constexpr uint64_t OwnerNext = UINT64_C(2822175277);
constexpr uint64_t OwnerSecond = UINT64_C(1265658722);
constexpr uint64_t SharedNext = UINT64_C(2957835478);

struct Probe : VMGameObject {
    std::function<void()> Body;
    int Runs = 0;
    explicit Probe(FSO::Content::GameObject* object) : VMGameObject(object, nullptr) {}
    void Tick() override { ++Runs; if (Body) Body(); }
};

struct Fixture {
    VMContext Context{nullptr};
    VM Machine{&Context, nullptr};
    IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff, nullptr, nullptr, "rng-scope", true};
    OBJD Definition;
    FSO::Content::GameObject Object;
    Entities::VMMultitileGroup EnabledGroup, DisabledGroup, DelayedGroup, DeadGroup;
    std::unique_ptr<Probe> Enabled, Disabled, Delayed, Dead;

    Fixture() {
        need(Context.TS1SimulationRandomSeed == Context.RandomSeed,
             "constructor did not initialize owner state from its initial RNG state");
        Machine.TS1 = true;
        Machine.GlobalState.assign(64, 0);
        Context.Architecture(std::make_unique<VMArchitecture>(8, 8, nullptr, &Context));
        Context.Architecture()->Rooms[0].Map.assign(64, 0);
        Context.RoomInfo.resize(1);
        Definition.GUID = 0x1234abcd;
        Object.OBJ = &Definition;
        Object.GUID = Definition.GUID;
        Object.Resource = &Resource;
        auto add = [&](std::unique_ptr<Probe>& probe, Entities::VMMultitileGroup& group) {
            probe = std::make_unique<Probe>(&Object);
            probe->MultitileGroup = &group;
            group.AddObject(probe.get());
            Machine.AddEntity(probe.get());
            probe->Thread = std::make_unique<VMThread>(&Context, probe.get(), 16);
        };
        add(Enabled, EnabledGroup);
        add(Disabled, DisabledGroup);
        add(Delayed, DelayedGroup);
        add(Dead, DeadGroup);
        Disabled->TS1SchedulerState = 0;
        Delayed->TS1SchedulerState = 0x00010002u;
        Dead->Dead = true;
        Context.RandomSeed = Shared;
        Context.TS1SimulationRandomSeed = Owner;
    }

    void unchanged() {
        need(Context.RandomSeed == Shared && Context.TS1SimulationRandomSeed == Owner,
             "tick without authored randomness advanced an RNG stream");
    }

    void sweep() {
        // Update is the public frontend path. Force its one ordinary sweep
        // instead of depending on the alternating fractional-animation frame.
        Machine.Ready = false;
        Machine.Update();
    }
};

VMPrimitiveExitCode randomCheck(Fixture& fixture) {
    VMRoutine routine;
    routine.Arguments = 4;
    VMInstruction instruction;
    instruction.Opcode = 8;
    instruction.TruePointer = 254;
    instruction.FalsePointer = 255;
    auto operand = std::make_unique<VMRandomNumberOperand>();
    operand->DestinationData = 1;
    operand->DestinationScope = Scope::Temps;
    operand->RangeData = 97;
    operand->RangeScope = Scope::Literal;
    instruction.Operand = std::move(operand);
    routine.Instructions.push_back(std::move(instruction));
    auto frame = std::make_shared<VMStackFrame>();
    frame->Caller = fixture.Enabled.get();
    frame->Callee = fixture.Enabled.get();
    frame->StackObject = fixture.Enabled.get();
    frame->CodeOwner = &fixture.Object;
    frame->Routine = &routine;
    frame->Args.assign(4, 0);
    return VMThread::EvaluateCheck(&fixture.Context, fixture.Enabled.get(), frame);
}

void noSyntheticBurn() {
    Fixture f;
    f.Enabled->Body = [&] { need(f.Machine.EntityTickRunning(), "real Tick did not enter dispatch scope"); };
    f.sweep();
    f.unchanged();
    need(f.Enabled->Runs == 1 && f.Disabled->Runs == 0 && f.Delayed->Runs == 0 && f.Dead->Runs == 0,
         "disabled/dead/countdown nodes entered the virtual tick");
    need(f.Delayed->TS1SchedulerDelay() == 1, "countdown did not progress during real Tick");
    f.sweep();
    f.unchanged();
    need(f.Delayed->Runs == 0 && f.Delayed->TS1SchedulerDelay() == 0,
         "last countdown decrement executed the node early");
    need(!f.Machine.EntityTickRunning(), "completed Tick leaked the owner scope");
}

void authoredDrawAndOutsideCall() {
    Fixture f;
    f.Enabled->Body = [&] {
        need(f.Context.NextRandom(97) == 13, "authored owner draw differs from literal DVD control");
    };
    f.sweep();
    need(f.Context.TS1SimulationRandomSeed == OwnerNext && f.Context.RandomSeed == Shared,
         "authored dispatch draw changed shared state or acquired synthetic draws");
    need(!f.Machine.EntityTickRunning(), "authored draw leaked dispatch scope");
    need(f.Context.NextRandom(97) == 2, "outside shared draw differs from literal DVD control");
    need(f.Context.RandomSeed == SharedNext && f.Context.TS1SimulationRandomSeed == OwnerNext,
         "outside draw changed owner state");
}

void realChecksInheritScope() {
    {
        Fixture f;
        f.Enabled->Thread->TempRegisters.fill(73);
        const auto temps = f.Enabled->Thread->TempRegisters;
        need(randomCheck(f) == VMPrimitiveExitCode::RETURN_TRUE, "outside Random8 check did not return true");
        need(f.Context.RandomSeed == SharedNext && f.Context.TS1SimulationRandomSeed == Owner,
             "outside check chose owner stream or rewound its actual random draw");
        need(f.Enabled->Thread->TempRegisters == temps, "outside check leaked its private temporary writes");
        need(f.Enabled->Thread->Stack.empty(), "outside check installed a live frame");
    }
    {
        Fixture f;
        f.Enabled->Body = [&] {
            need(randomCheck(f) == VMPrimitiveExitCode::RETURN_TRUE, "nested Random8 check did not return true");
            need(f.Enabled->Thread->TempRegisters[1] == 13,
                 "nested check did not retain the owner-derived actual primitive result");
            need(f.Context.NextRandom(97) == 82, "outer dispatch did not retain owner progress after nested check");
        };
        f.sweep();
        need(f.Context.TS1SimulationRandomSeed == OwnerSecond && f.Context.RandomSeed == Shared,
             "nested check/outer continuation selected or restored the wrong stream");
        need(f.Enabled->Thread->Stack.empty(), "nested check installed a live frame");
        need(!f.Machine.EntityTickRunning(), "nested check leaked dispatch scope");
    }
}

void exceptionRestoresBoundary() {
    Fixture f;
    f.Enabled->Body = [&] {
        need(f.Context.NextRandom(97) == 13, "exception probe did not execute its authored owner draw");
        throw std::runtime_error("injected entity failure");
    };
    bool caught = false;
    try { f.sweep(); }
    catch (const std::runtime_error& error) { caught = std::string(error.what()) == "injected entity failure"; }
    need(caught, "entity exception did not propagate from real Tick");
    need(!f.Machine.EntityTickRunning(), "entity exception leaked owner scope");
    need(f.Context.TS1SimulationRandomSeed == OwnerNext && f.Context.RandomSeed == Shared,
         "native exception policy lost owner progress or contaminated shared state");
    need(f.Context.NextRandom(97) == 2 && f.Context.RandomSeed == SharedNext,
         "outside call after exception still used owner stream");
    need(f.Context.TS1SimulationRandomSeed == OwnerNext, "outside recovery draw changed owner state");
}

void captureDuringDispatch() {
    Fixture f;
    bool captured = false;
    f.Enabled->Body = [&] {
        need(f.Context.NextRandom(97) == 13, "capture probe did not advance owner state");
        IffRawFile house;
        house.Set("ObjM", 1, {1, 2, 3});
        need(!f.Machine.TS1Budget.Initialized, "capture fixture unexpectedly initialized finance");
        Utils::VMTS1InteractionCheckpoint::Capture(f.Machine, house, false);
        const auto snapshot = house.Find("ODHI", 1);
        need(snapshot && snapshot->Data.size() >= 67, "in-dispatch Capture omitted the ODHI14 header");
        const auto& bytes = snapshot->Data;
        need(bytes[0] == 14 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0,
             "capture test requires the version14 wire contract");
        need(bytes[8] == 0 && bytes[9] == 0 && bytes[10] == 0,
             "direct capture unexpectedly added departure/scheduler/finance binding");
        // Independent fixed-header contract: version/hash(8), three bools(3),
        // ticks(i64) and eight clock i32 fields(40), shared(u64), owner(u64).
        // Read only the two semantic slots, not a copy of ClockState/Archive.
        const auto word = [&](size_t offset) {
            uint64_t value = 0;
            for (unsigned i = 0; i < 8; ++i) value |= uint64_t(bytes[offset + i]) << (8 * i);
            return value;
        };
        need(word(51) == Shared, "in-dispatch Capture stored owner progress in the shared slot");
        need(word(59) == OwnerNext, "in-dispatch Capture lost or swapped owner progress");
        need(f.Machine.EntityTickRunning(), "Capture ended the surrounding dispatcher scope");
        need(f.Context.RandomSeed == Shared && f.Context.TS1SimulationRandomSeed == OwnerNext,
             "Capture consumed or relabelled live RNG states");
        need(!house.Find("SIMI", 1), "resource-free capture synthesized a SIMI dependency");
        captured = true;
    };
    f.sweep();
    need(captured && !f.Machine.EntityTickRunning(), "capture probe did not complete the real Tick boundary");
    need(f.Context.RandomSeed == Shared && f.Context.TS1SimulationRandomSeed == OwnerNext,
         "completed capture tick acquired an extra random draw");
}

void tsoLegacyBurnRemains() {
    Fixture f;
    f.Machine.TS1 = false;
    // One ordinary TSO node isolates the existing per-entity xorshift step.
    // TS1's disabled word must not change the independent TSO contract.
    f.Enabled->TS1SchedulerState = 0;
    f.Machine.Entities = {f.Enabled.get()};
    f.Context.RandomSeed = 1;
    f.sweep();
    need(f.Enabled->Runs == 1, "TS1 admission gate leaked into TSO dispatch");
    need(f.Context.RandomSeed == UINT64_C(33554433), "TSO legacy per-entity draw was removed or duplicated");
    need(f.Context.TS1SimulationRandomSeed == Owner, "TSO dispatch consumed the TS1 owner state");
    need(!f.Machine.EntityTickRunning(), "TSO Tick leaked dispatch scope");
}
} // namespace

int main() {
    try {
        VM::UseWorld(false);
        noSyntheticBurn();
        authoredDrawAndOutsideCall();
        realChecksInheritScope();
        exceptionRestoresBoundary();
        captureDuringDispatch();
        tsoLegacyBurnRemains();
        std::cout << "PASS TS1 RNG scope: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
