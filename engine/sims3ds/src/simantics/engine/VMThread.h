/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Engine.VMThread — handles instruction execution.
#pragma once
namespace FSO { namespace SimAntics { namespace Engine { namespace Utils { class InteractionCheckpointCodec; }}}}
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <memory>
#include <array>
#include "simantics/_files_contract.h"
#include "content/ContentManager.h"
#include "simantics/engine/VMPrimitiveExitCode.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/engine/VMSubRoutineOperand.h"
#include "simantics/model/TSOPlatform/VMAsyncState.h"
#include "files/formats/iff/chunks/OBJM.h"

namespace FSO { namespace SimAntics {
class VM;
class VMEntity;
class VMContext;
class VMPieMenuInteraction;
namespace Engine {
class VMRoutingFrame;

enum class VMThreadBreakMode : int {
    Active = 0, Pause = 1, StepIn = 2, StepOut = 3, StepOver = 4, ReturnTrue = 5, ReturnFalse = 6, Immediate = 7
};

// ADDED(primitives): FSO.SimAntics.NetPlay.EODs.Model.VMEODEvent (serialization dropped) — needed by VMInvokePlugin.
class VMEODEvent {
public:
    int16_t Code = 0;
    std::vector<int16_t> Data;
    VMEODEvent() {}
    explicit VMEODEvent(int16_t code) : Code(code) {}
    VMEODEvent(int16_t code, std::vector<int16_t> data) : Code(code), Data(std::move(data)) {}
};

// STUB(3ds): FSO.SimAntics.NetPlay.EODs.Model.VMEODPluginThreadState — EOD plugins (TSO) are not ported.
class VMEODPluginThreadState : public Model::TSOPlatform::VMAsyncState {
public:
    // ADDED(primitives): the C# state fields (serialization dropped) — read/written by VMInvokePlugin.
    int16_t AvatarID = 0;
    int16_t ObjectID = 0;
    bool Joinable = false;
    bool Ended = false;
    std::vector<VMEODEvent> Events;

    Model::TSOPlatform::VMAsyncStateType StateType() const override { return Model::TSOPlatform::VMAsyncStateType::PluginState; }
};

class VMThread {
public:
    VMContext* Context;

    static int32_t MAX_USER_ACTIONS;

    VMThreadBreakMode ThreadBreak = VMThreadBreakMode::Active;
    int32_t BreakFrame = 0; //frame the last breakpoint was performed on
    bool RoutineDirty = false;

    //check tree only vars
    bool IsCheck = false;
    std::vector<VMPieMenuInteraction>* ActionStrings = nullptr;

    std::vector<std::shared_ptr<VMStackFrame>> Stack;

    // Original count-only restored records, distinct from native route frames.
    // Unknown is the compatibility policy for pre-16 native checkpoints.
    bool TS1RouteDefaultsKnown = false;
    uint32_t TS1RouteDefaultsCount = 0;
    bool TryResumeTS1Route(VMStackFrame* frame, VMPrimitiveExitCode& result);

    struct TS1ObjectUse { VMEntity* Target = nullptr; int32_t StackDepth = 0; uint8_t Flag = 0; };
    // Ordered original person use records; independent of the UI queue.
    std::vector<TS1ObjectUse> TS1ObjectUses;
    VMEntity* TS1FlaggedUseTarget = nullptr;
    FSO::Files::Formats::IFF::Chunks::OBJMInteraction TS1LastInteraction;
    void RememberTS1CurrentInteraction();
    void AcquireTS1ObjectUse(VMEntity* target, uint8_t flag = 0);
    void RetireTS1ObjectUse();
    void DiscardTS1ObjectUses();
    // Release only this machine's matching model owner before actual frame
    // destruction. Temporary check-stack switches preserve the live request.
    void DetachTS1FrameMotion(VMStackFrame* frame);
    void DetachTS1StackMotion(size_t first = 0);
    void TS1ObjectUseTargetRemoved(VMEntity* target);
    int32_t TS1ClassicUseDepth(int32_t nativeDepth) const;
    int32_t TS1NativeUseDepth(int32_t classicDepth) const;

    std::vector<std::shared_ptr<VMQueuedAction>> Queue;
    /// Set when a change to the queue or an item's priority is changed. Internal functions set this, but since you can modify the queue
    /// from other classes MAKE SURE you set this when such a change is made. (eg. priority set from VMAvatar)
    bool QueueDirty = false;

    // TS1(sims): FreeSO queue model — index of the innermost running interaction (-1 = none, main is running).
    // FreeSims kept a byte at 0 and only ever ran Queue[0], so an interaction could never run another one (nested
    // TS1 interactions: social "Be Talked To", parent idle/exit) and Allow Push inside an action tree did nothing.
    int8_t ActiveQueueBlock = -1; //cannot reorder items in the queue with index <= this.
    /// C# property ActiveAction: the running interaction (null in main).
    std::shared_ptr<VMQueuedAction> ActiveAction() const { return (ActiveQueueBlock > -1 && ActiveQueueBlock < (int)Queue.size()) ? Queue[ActiveQueueBlock] : nullptr; }
    std::array<int16_t, 20> TempRegisters;
    std::array<int32_t, 2> TempXL;
    /** TS1(sims): FreeSO MotiveAdChanges — "check tree ad range / ad min / ad personality var" (scopes 12/29/28).
     ** Check trees adjust the autonomy advertisement of their interaction through these. Keyed by (type << 16) | motive,
     ** type 0 = min, 1 = range, 2 = personality (FreeSO VMMemory.SetTreeAd); EvaluateCheck hands them to
     ** VMFindBestAction through the action strings. **/
    std::unordered_map<int32_t, int16_t> MotiveAdChanges;
    VMPrimitiveExitCode LastStackExitCode = VMPrimitiveExitCode::GOTO_FALSE;
    static constexpr int32_t MAX_CHECK_LOOP_COUNT = 20000; // TS1(sims): see VMThread::Tick

    std::shared_ptr<Model::TSOPlatform::VMAsyncState> BlockingState;
    std::shared_ptr<VMEODPluginThreadState> EODConnection;
    bool Interrupt = false;

    // Exception handling variables
    // Don't need to be serialized.
    int32_t DialogCooldown = 0;
    // the number of ticks that have executed so far this frame. If this exceeds the allowed max,
    // the thread resets, and a SimAntics Error pops up.
    int32_t TicksThisFrame = 0;
    // the maximum number of primitives a thread can execute in one frame. Tweak appropriately.
    static const int32_t MAX_LOOP_COUNT;

    static VMPrimitiveExitCode EvaluateCheck(VMContext* context, VMEntity* entity, std::shared_ptr<VMStackFrame> initFrame);
    static VMPrimitiveExitCode EvaluateCheck(VMContext* context, VMEntity* entity, std::shared_ptr<VMStackFrame> initFrame, std::shared_ptr<VMQueuedAction> action);
    static VMPrimitiveExitCode EvaluateCheck(VMContext* context, VMEntity* entity, std::shared_ptr<VMStackFrame> initFrame, std::shared_ptr<VMQueuedAction> action, std::vector<VMPieMenuInteraction>* actionStrings);

    bool RunInMyStack(FSO::Files::Formats::IFF::Chunks::BHAV* bhav, FSO::Content::GameObject* CodeOwner, const int16_t passVars[4], VMEntity* stackObj);
    /// DVD fresh-object Init runs on its existing machine, preserving Main and
    /// sharing temporaries. It cannot suspend across simulation updates.
    bool RunTS1Initializer(std::shared_ptr<VMStackFrame> frame);
    /// Original474b22: same-machine immediate call above a sentinel. Existing
    /// action/temporary state stays live; prepared execution frames are restored.
    bool RunTS1SynchronousCall(std::shared_ptr<VMStackFrame> frame);
    bool RunTS1Load(std::shared_ptr<VMStackFrame> frame);
    bool TS1ImmediateCallRunning() const { return TS1SynchronousActive; }
    bool TS1InitializerRunning() const { return TS1InitializerActive; }
    bool TS1SynchronousCallRunning() const { return TS1SynchronousActive || TS1FreshTickActive; }
    void RunTS1FreshTick();

    VMThread(VMContext* context, VMEntity* entity, int32_t stackSize);

    /// Checks to see if it can push an interaction, and pushes it. Returns true on success, false on failure.
    bool AttemptPush();
    /// Execute opcode3's stored TS1 target/index without queue arbitration.
    /// The existing queue still records the installed active action for UI/save.
    VMPrimitiveExitCode ExecuteTS1SelectedAction(VMStackFrame* context);
    /// Original person Idle For Input examines only the next pending record.
    bool TS1ShouldNotifyIdle() const;
    VMPrimitiveExitCode AttemptTS1Push(VMStackFrame* context);
    void TryRunImmediately();
    /// TS1(sims): FreeSO — go all the way back to the stack frame that Allow Push'd us.
    void AbortCurrentInteraction();
    void Tick();
    /// Same interpreter with a caller-owned instruction budget and error policy.
    /// The ordinary Tick keeps its existing budget/reset behavior.
    void Tick(int32_t instructionBudget, bool resetOnError);

    VMRoutingFrame* PushNewRoutingFrame(VMStackFrame* frame, bool failureTrees);
    void ExecuteSubRoutine(VMStackFrame* frame, FSO::Files::Formats::IFF::Chunks::BHAV* bhav, FSO::Content::GameObject* codeOwner, const Primitives::VMSubRoutineOperand& args);

    void Breakpoint(VMStackFrame* frame);
    void Pop(VMPrimitiveExitCode result);
    bool Push(std::shared_ptr<VMStackFrame> frame); // C# (FreeSO) returns false for empty routines

    /// Add an item to the action queue
    bool EnqueueAction(std::shared_ptr<VMQueuedAction> invocation);
    void CancelAction(uint16_t actionUID);

    /// Returns null (nullptr) when the action is not allowed, otherwise the (possibly empty) list of action strings.
    /// TS1(sims): FreeSO `auto` — passes 1 in param 0 of the check tree (autonomous check).
    std::unique_ptr<std::vector<VMPieMenuInteraction>> CheckAction(std::shared_ptr<VMQueuedAction> action, bool autonomous = false);
    /// TS1(sims): FreeSO CheckTS1Action — TS1 TTAB flags (pets, children/adults, visitors, debug), then the check tree.
    std::unique_ptr<std::vector<VMPieMenuInteraction>> CheckTS1Action(std::shared_ptr<VMQueuedAction> action, bool autonomous);

    VMEntity* GetEntity() const { return Entity; }

public:
    /// Shared end-of-interaction cleanup for an abandoned classic continuation.
    void ResetInteractionState();
private:
    bool TS1InitializerActive = false;
    bool TS1SynchronousActive = false;
    bool TS1FreshTickActive = false;
    std::shared_ptr<VMStackFrame> TS1SynchronousBoundary;
    VMEntity* Entity;
    bool ContinueExecution = false;
    friend class FSO::SimAntics::Engine::Utils::InteractionCheckpointCodec;
    uint16_t ActionUID = 0;

    void EvaluateQueuePriorities();
    void EndCurrentInteraction(); // TS1(sims): FreeSO
    void NextInstruction();
    void ExecuteInstruction(VMStackFrame* frame);
    void HandleResult(VMStackFrame* frame, VMInstruction* instruction, VMPrimitiveExitCode result);
    void MoveToInstruction(VMStackFrame* frame, uint8_t instruction, bool continueExecution);
    bool ExecuteAction(std::shared_ptr<VMQueuedAction> action);
};
}}}
