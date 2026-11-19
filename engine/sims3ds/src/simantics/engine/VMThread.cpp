/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Engine.VMThread — handles instruction execution.
//#define THROW_SIMANTICS
#define IDE_COMPAT

#include "simantics/engine/VMTS1Neighbourhood.h"
#include "files/formats/iff/chunks/FAMI.h"
#include "content/TS1NeighbourProvider.h"
#include <algorithm>
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMActionCallback.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "simantics/engine/VMDialogOperand.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/model/VMDialogInfo.h"
#include "simantics/model/TSOPlatform/VMTSOAvatarState.h"
#include "simantics/model/TSOPlatform/VMTSOObjectState.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include <algorithm>
#include <stdexcept>

namespace FSO { namespace SimAntics { namespace Engine {
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Model::TSOPlatform;

int32_t VMThread::MAX_USER_ACTIONS = 20;
const int32_t VMThread::MAX_LOOP_COUNT = 500000;

bool VMThread::TryResumeTS1Route(VMStackFrame* frame, VMPrimitiveExitCode& result) {
    if (!Context || !Context->VM->TS1 || IsCheck || !TS1RouteDefaultsKnown ||
        !frame || frame->Thread != this || frame->IsRoutingFrame() ||
        Stack.empty() || Stack.back().get() != frame || !frame->Routine ||
        frame->InstructionPointer >= frame->Routine->Instructions.size() || frame->PrimitiveState == 0)
        return false;
    const auto opcode = frame->GetCurrentInstruction().Opcode;
    if (opcode != 27 && opcode != 45) return false;
    // Defaults are below every live native descriptor. An ancestor's native
    // descriptor cannot be consumed as this instruction's classic placeholder.
    for (const auto& child : Stack) if (child->IsRoutingFrame()) return false;
    if (TS1RouteDefaultsCount != 0) {
        --TS1RouteDefaultsCount;
        frame->PrimitiveState = 0;
        result = VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    } else result = VMPrimitiveExitCode::GOTO_FALSE; // normal branch clears state
    return true;
}

VMPrimitiveExitCode VMThread::EvaluateCheck(VMContext* context, VMEntity* entity, std::shared_ptr<VMStackFrame> initFrame) {
    return EvaluateCheck(context, entity, initFrame, nullptr, nullptr);
}

VMPrimitiveExitCode VMThread::EvaluateCheck(VMContext* context, VMEntity* entity, std::shared_ptr<VMStackFrame> initFrame, std::shared_ptr<VMQueuedAction> action) {
    return EvaluateCheck(context, entity, initFrame, action, nullptr);
}

VMPrimitiveExitCode VMThread::EvaluateCheck(VMContext* context, VMEntity* entity, std::shared_ptr<VMStackFrame> initFrame, std::shared_ptr<VMQueuedAction> action, std::vector<VMPieMenuInteraction>* actionStrings) {
    VM::TS1InterpreterScope running(*context->VM);
    // Both pinned references clone temporaries when Scheduler.RunningNow is
    // false. In particular, opening a pie menu must not overwrite live action
    // registers. Keep the existing in-tick writeback for simulation checks.
    const bool persistTemps = context->VM && context->VM->EntityTickRunning();
    VMThread temp(context, entity, 5);
    if (entity->Thread != nullptr) {
        temp.TempRegisters = entity->Thread->TempRegisters;
        temp.TempXL = entity->Thread->TempXL;
    }
    temp.IsCheck = true;
    temp.ActionStrings = actionStrings; //generate and place action strings in here
    temp.Push(initFrame);
    if (action != nullptr) {
        temp.Queue.push_back(action); //this check runs an action. We may need its interaction number, etc.
        temp.ActiveQueueBlock = 0; // TS1(sims): FreeSO
    }
    while (temp.Stack.size() > 0 && temp.DialogCooldown == 0 && !entity->Dead) //keep going till we're done! idling is for losers!
    {
        temp.Tick();
        temp.ThreadBreak = VMThreadBreakMode::Active; //cannot breakpoint in check trees
    }
    // Reference checks share registers only during entity execution.
    if (persistTemps && entity->Thread != nullptr) {
        entity->Thread->TempRegisters = temp.TempRegisters;
        entity->Thread->TempXL = temp.TempXL;
    }
    // TS1(sims): FreeSO — add an action string containing any modified ads
    if (actionStrings != nullptr && actionStrings->size() == 0) {
        VMPieMenuInteraction item;
        if (!temp.MotiveAdChanges.empty()) item.MotiveAdChanges = std::make_shared<std::unordered_map<int32_t, int16_t>>(temp.MotiveAdChanges);
        actionStrings->push_back(item);
    }
    return (temp.DialogCooldown > 0) ? VMPrimitiveExitCode::RETURN_FALSE : temp.LastStackExitCode;
}

bool VMThread::RunTS1Initializer(std::shared_ptr<VMStackFrame> frame) {
    if (!Context || !Context->VM || !Context->VM->TS1 || IsCheck ||
        !Queue.empty() || !TS1ObjectUses.empty())
        throw std::runtime_error("TS1 initializer requires a fresh object machine");
    struct InitializerScope {
        bool& Flag;bool Previous;
        explicit InitializerScope(bool& flag):Flag(flag),Previous(flag){Flag=true;}
        ~InitializerScope(){Flag=Previous;}
    } scope(TS1InitializerActive);
    return RunTS1SynchronousCall(std::move(frame));
}

bool VMThread::RunTS1Load(std::shared_ptr<VMStackFrame> frame) {
    return RunTS1SynchronousCall(std::move(frame));
}

bool VMThread::RunTS1SynchronousCall(std::shared_ptr<VMStackFrame> frame) {
    if (!Context || !Context->VM || !Context->VM->TS1 || IsCheck)
        throw std::runtime_error("TS1 synchronous call requires an original object machine");
    // The DVD's sentinel protects the prepared Main stack, while the same
    // machine owns Init's temporaries. Keep that boundary on every exit.
    struct Restore {
        VMThread& Thread;
        std::vector<std::shared_ptr<VMStackFrame>> Stack;
        int32_t Count;
        bool Continue, Initializing;
        VMPrimitiveExitCode Result;
        VMThreadBreakMode Break;
        std::shared_ptr<VMStackFrame> Boundary;
        explicit Restore(VMThread& thread) : Thread(thread), Stack(thread.Stack),
            Count(thread.TicksThisFrame), Continue(thread.ContinueExecution),
            Initializing(thread.TS1SynchronousActive), Result(thread.LastStackExitCode),
            Break(thread.ThreadBreak), Boundary(thread.TS1SynchronousBoundary) {}
        ~Restore() {
            Thread.DetachTS1StackMotion(Stack.size());
            Thread.Stack=std::move(Stack);Thread.TicksThisFrame=Count;
            Thread.ContinueExecution=Continue;Thread.TS1SynchronousActive=Initializing;
            Thread.LastStackExitCode=Result;Thread.ThreadBreak=Break;
            Thread.TS1SynchronousBoundary=std::move(Boundary);
        }
    } restore(*this);
    TS1SynchronousActive=true;TicksThisFrame=0;ThreadBreak=VMThreadBreakMode::Active;
    ContinueExecution=true;LastStackExitCode=VMPrimitiveExitCode::RETURN_FALSE;
    TS1SynchronousBoundary=std::make_shared<VMStackFrame>();
    Stack.push_back(TS1SynchronousBoundary);
    if (!frame || !frame->Routine || !Push(std::move(frame))) return false;
    while (Stack.size()>restore.Stack.size() && !Entity->Dead) {
        if (TicksThisFrame++ > MAX_LOOP_COUNT)
            throw std::runtime_error(TS1InitializerActive ? "TS1 initializer entered an instruction loop" : "TS1 synchronous call entered an instruction loop");
        NextInstruction();
        if (Stack.size()>restore.Stack.size() && !ContinueExecution)
            throw std::runtime_error(TS1InitializerActive ? "TS1 initializer attempted to suspend" : "TS1 synchronous call attempted to suspend");
    }
    return LastStackExitCode==VMPrimitiveExitCode::RETURN_TRUE;
}

void VMThread::RunTS1FreshTick() {
    struct Scope {
        bool& Active;
        bool Previous;
        explicit Scope(bool& active):Active(active),Previous(active){Active=true;}
        ~Scope(){Active=Previous;}
    } scope(TS1FreshTickActive);
    Entity->Tick();
}

bool VMThread::RunInMyStack(BHAV* bhav, FSO::Content::GameObject* CodeOwner, const int16_t passVars[4], VMEntity* stackObj) {
    VM::TS1InterpreterScope running(*Context->VM);
    //a little bit hacky. We may not need to do as serious a context switch as this.
    auto OldStack = Stack;
    auto OldQueue = Queue;
    auto OldCheck = IsCheck;
    auto OldQueueBlock = ActiveQueueBlock;

    std::shared_ptr<VMStackFrame> prevFrame = std::make_shared<VMStackFrame>();
    prevFrame->Caller = Entity; prevFrame->Callee = Entity;
    if (Stack.size() > 0) {
        prevFrame = Stack[Stack.size() - 1];
        Stack = std::vector<std::shared_ptr<VMStackFrame>>{ prevFrame };
    } else {
        Stack = std::vector<std::shared_ptr<VMStackFrame>>();
    }

    Queue = std::vector<std::shared_ptr<VMQueuedAction>>();
    if (Queue.size() > 0) Queue.push_back(Queue[0]);
    IsCheck = true;

    ExecuteSubRoutine(prevFrame.get(), bhav, CodeOwner, Primitives::VMSubRoutineOperand(passVars));
    Stack.erase(Stack.begin());
    if (Stack.size() == 0) {
        Stack = OldStack;
        Queue = OldQueue;
        return false;
        //bhav was invalid/empty
    }
    auto frame = Stack[Stack.size() - 1];
    frame->StackObject = stackObj;

    try {
        while (Stack.size() > 0) {
            NextInstruction();
        }
    } catch (std::exception&) {
        //we need to catch these so that the parent can be restored.
    }

    //copy child stack things to parent stack
    Stack = OldStack;
    Queue = OldQueue;
    IsCheck = OldCheck;
    ActiveQueueBlock = OldQueueBlock;

    return (LastStackExitCode == VMPrimitiveExitCode::RETURN_TRUE) ? true : false;
}

VMThread::VMThread(VMContext* context, VMEntity* entity, int32_t stackSize) {
    this->Context = context;
    this->Entity = entity;
    TS1RouteDefaultsKnown = context && context->VM && context->VM->TS1;
    TempRegisters.fill(0);
    TempXL.fill(0);
    this->Stack.reserve((size_t)std::max<int32_t>(0, stackSize));
}

/// Checks to see if it can push an interaction, and pushes it. Returns true on success, false on failure.
// TS1(sims): FreeSO VMThread.AttemptPush. Runs the next queued interaction *after* the active queue block (so
// an interaction can run a nested one, as TS1 does), only if its priority beats the running one (person data
// Priority). Interactions that fail their check are dropped. FreeSims always took Queue[0].
bool VMThread::AttemptPush() {
    auto avatar = dynamic_cast<VMAvatar*>(Entity);
    if (Context->VM->TS1 && avatar != nullptr) {
        if (!TS1ShouldNotifyIdle()) return false;
        return AttemptTS1Push(Stack.empty() ? nullptr : Stack.back().get()) == VMPrimitiveExitCode::CONTINUE;
    }
    int32_t priorityCompare = (avatar == nullptr) ? 0 : avatar->GetPersonData(VMPersonDataVariable::Priority);
    if (priorityCompare <= 2) priorityCompare -= 1; //autonomous interactions fall through to other ones
    QueueDirty = true;
    while ((int)Queue.size() > ActiveQueueBlock + 1) {
        auto item = Queue[ActiveQueueBlock + 1];
        if (item->Priority <= priorityCompare) return false;
        if (item->Cancelled) Entity->SetFlag(VMEntityFlags::InteractionCanceled, item->Cancelled);
        if (IsCheck || ((item->Mode != VMQueueMode::ParentIdle || !Entity->GetFlag(VMEntityFlags::InteractionCanceled)) && CheckAction(item) != nullptr)) {
            Entity->SetFlag(VMEntityFlags::InteractionCanceled, false);
            if (!ExecuteAction(item)) return false;
            ActiveQueueBlock++;
            return true;
        } else {
            Queue.erase(Queue.begin() + (ActiveQueueBlock + 1)); //keep going.
        }
    }
    return false;
}

// DVD 0045d92c. The native queue retains active records before its pending
// records; only the first pending record represents the original ring front.
bool VMThread::TS1ShouldNotifyIdle() const {
    const auto avatar = dynamic_cast<VMAvatar*>(Entity);
    if (!avatar || avatar->GetPersonData(VMPersonDataVariable::NonInterruptable) != 0) return false;
    const auto next = size_t(ActiveQueueBlock + 1);
    if (next < Queue.size() && Queue[next] &&
        (Queue[next]->Priority > avatar->GetPersonData(VMPersonDataVariable::Priority) ||
         (Queue[next]->Flags & TTABFlags::FSOPushHead) != 0)) return true;
    const auto lot = Context->VM->GetGlobalValue(10);
    bool residential = lot == 0;
    if (lot != 0) {
        auto hood = FSO::Content::Content::Get()->Neighborhood.get();
        if (hood) {
            const auto zone = hood->ZoningDictionary.find(lot);
            residential = zone != hood->ZoningDictionary.end() && zone->second == 0;
        }
    }
    return residential && avatar->GetPersonData(VMPersonDataVariable::PersonType) < 2 &&
        (avatar->GetMotiveDataFloat(VMMotive::Hunger) < -99.0f ||
         avatar->GetMotiveDataFloat(VMMotive::Bladder) < -99.0f ||
         (avatar->GetMotiveDataFloat(VMMotive::Energy) < -99.0f &&
          avatar->GetPersonData(VMPersonDataVariable::Gender) <= 1));
}

VMPrimitiveExitCode VMThread::AttemptTS1Push(VMStackFrame* context) {
    const auto next = size_t(ActiveQueueBlock + 1);
    if (next >= Queue.size()) return VMPrimitiveExitCode::GOTO_TRUE;
    const auto action = Queue[next];
    if (context && context->PrimitiveState == 1 && (action->Flags & TTABFlags::FSOPushHead) == 0)
        return VMPrimitiveExitCode::GOTO_TRUE;
    // A failed front record is discarded for this tick, not followed by a
    // synchronous scan of every later item. The original retries opcode17.
    Queue.erase(Queue.begin() + next);
    QueueDirty = true;
    bool available = true;
    if (action->Mode == VMQueueMode::Normal && action->InteractionNumber >= 0 && action->Callee && !action->Callee->Dead) {
        auto authored = action->Callee->GetAction(action->InteractionNumber, Entity, Context);
        available = bool(authored);
        if (authored) {
            action->CheckRoutine = authored->CheckRoutine;
            const auto runtime = action->Flags & (TTABFlags::FSOPushHead | TTABFlags::FSOPushTail |
                TTABFlags::FSODirectControl | TTABFlags::TS1HideQueueIcon | TTABFlags::TS1InheritActionName);
            action->Flags = authored->Flags | static_cast<TTABFlags>(runtime);
            action->Flags2 = authored->Flags2;
        }
    }
    if (!available || !action->Callee || action->Callee->Dead || !action->ActionRoutine ||
        action->ActionRoutine->Instructions.empty() || CheckAction(action) == nullptr || !ExecuteAction(action)) {
        if (action->Callee && !action->Callee->Dead)
            action->Callee->ExecuteEntryPoint(4, Context, true, Entity);
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    }
    if (Context->VM->TS1 && !IsCheck) if (auto avatar=dynamic_cast<VMAvatar*>(Entity)) {
        AcquireTS1ObjectUse(action->Callee); RememberTS1CurrentInteraction(); avatar->ClearTS1InteractionFootprint();
        TS1FlaggedUseTarget=action->IconOwner();
    }
    Queue.insert(Queue.begin() + next, action);
    ++ActiveQueueBlock;
    Entity->SetFlag(VMEntityFlags::InteractionCanceled, false);
    if (context) {
        context->PrimitiveState = 1;
        context->StackObject = action->Callee;
        context->StackObjectRawID = action->Callee->ObjectID;
    }
    return VMPrimitiveExitCode::CONTINUE;
}

// DVD 0045a9c5 / 0045e707: Gosub Found Action resolves the selected entry
// afresh and starts its action function. It does not re-run its test function,
// transfer a dynamic-menu parameter or reorder pending player commands.
VMPrimitiveExitCode VMThread::ExecuteTS1SelectedAction(VMStackFrame* context) {
    const auto targetID = context->StackObjectID();
    if (targetID == 0) throw VMSimanticsException("Gosub Found Action: no selected object", context);
    auto target = Context->VM->GetObjectById(targetID);
    if (target == nullptr || target->Dead || target->TreeTable == nullptr) return VMPrimitiveExitCode::GOTO_FALSE;
    const auto index = Entity->GetValue(VMStackObjectVariable::TreeTableEntry);
    const auto found = target->TreeTable->InteractionByIndex.find(uint32_t(int32_t(index)));
    if (found == target->TreeTable->InteractionByIndex.end())
        throw VMSimanticsException("Gosub Found Action: selected interaction is unavailable", context);
    const auto entry = found->second;
    auto tree = target->GetBHAVWithOwner(entry->ActionFunction, Context);
    if (tree == nullptr || tree->bhav == nullptr) return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    auto action = std::make_shared<VMQueuedAction>();
    action->Callee = action->StackObject = target;
    action->CodeOwner = tree->owner;
    action->ActionRoutine = Context->VM->Assemble(tree->bhav);
    action->Args.assign(4, 0);
    action->InteractionNumber = index;
    action->Priority = int16_t(VMQueuePriority::Autonomous);
    // This invocation came directly from opcode30, not the special enqueue
    // path. A TTAB immediate flag must not change its child-return policy.
    action->Flags = static_cast<TTABFlags>(entry->Flags & ~TTABFlags::RunImmediately);
    action->Flags2 = entry->Flags2;
    if (target->TreeTableStrings) action->Name = target->TreeTableStrings->GetString(index);
    if (!action->ActionRoutine || action->ActionRoutine->Instructions.empty() || !ExecuteAction(action))
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    if (Context->VM->TS1 && !IsCheck) if (auto avatar=dynamic_cast<VMAvatar*>(Entity)) {
        AcquireTS1ObjectUse(action->Callee); RememberTS1CurrentInteraction(); avatar->ClearTS1InteractionFootprint();
        TS1FlaggedUseTarget=action->Callee;
    }
    action->UID = ActionUID++;
    Queue.insert(Queue.begin() + (ActiveQueueBlock + 1), action);
    ++ActiveQueueBlock;
    QueueDirty = true;
    return VMPrimitiveExitCode::CONTINUE;
}

// TS1(sims): FreeSO VMThread.TryRunImmediately.
void VMThread::TryRunImmediately() {
    //check if we have a run immediately interaction, and inject it if we do.
    while (true) {
        int ind = -1;
        for (size_t i = 0; i < Queue.size(); i++) if ((Queue[i]->Flags & TTABFlags::RunImmediately) > 0) { ind = (int)i; break; }
        if ((ind > ActiveQueueBlock || (!(Stack.size() > 0 && Stack.back()->ActionTree) && ind > -1))) {
            //not already running (if no action we are still not running if we're queue[0], so go for it)
            //swap current item with ind.
            auto temp = Queue[ind];
            Queue.erase(Queue.begin() + ind);
            if (CheckAction(temp) != nullptr) {
                Queue.insert(Queue.begin() + (ActiveQueueBlock + 1), temp);
                // FIX(sims): run the "run immediately" tree at its own priority, as ExecuteAction does. FreeSO left the
                // priority of the interrupted (often autonomous/idle) action in person data 'priority', so 'push
                // interaction' with Inherited priority gave the pushes of a user social (Hot Date Talk, Compliment ...:
                // Person A side on the actor, Person B side on the target) priority 1: the target never left its post-
                // greet 'Wait', the actor timed out waiting for Person B and the relationship never changed.
                // EndCurrentInteraction restores the interrupted action's priority afterwards.
                if (auto av = dynamic_cast<VMAvatar*>(Entity)) av->SetPersonData(Model::VMPersonDataVariable::Priority, temp->Priority);
                auto frame = temp->ToStackFrame(Entity);
                frame->SpecialResult = VMSpecialResult::Interaction;
                Push(frame);
                ActiveQueueBlock++; //both the run immediately interaction and the active interaction must be protected.
                break;
            }
        } else {
            break;
        }
    }
}

void VMThread::ResetInteractionState() {
    auto avatar = dynamic_cast<VMAvatar*>(Entity);
    if (avatar != nullptr && !IsCheck) {
        avatar->SetPersonData(VMPersonDataVariable::NonInterruptable, 0);
        avatar->ClearMotiveChanges();
        avatar->SetPersonData(VMPersonDataVariable::Priority, 0);
    }
}

void VMThread::AcquireTS1ObjectUse(VMEntity* target, uint8_t flag) {
    if (!Context || !Context->VM->TS1 || IsCheck || !dynamic_cast<VMAvatar*>(Entity) || !target) return;
    TS1ObjectUses.push_back({target, int32_t(Stack.size()), flag});
    if (flag) TS1FlaggedUseTarget=target;
    auto& count=target->ObjectData.at(size_t(VMStackObjectVariable::UseCount));
    const uint16_t bits=uint16_t(count)+uint16_t(1);
    count=bits<=0x7fff ? int16_t(bits) : int16_t(int32_t(bits)-65536);
}

void VMThread::RememberTS1CurrentInteraction() {
    auto action=ActiveAction();if (!action || !action->Callee) return;
    using FSO::Files::Formats::IFF::Chunks::OBJMInteractionFlags;
    auto& out=TS1LastInteraction;out={};out.UID=action->UID;out.CallerID=Entity->ObjectID;
    out.TargetID=action->Callee->ObjectID;out.Icon=TS1FlaggedUseTarget ? TS1FlaggedUseTarget->ObjectID : 0;
    out.TTAIndex=action->InteractionNumber;
    for (size_t i=0;i<4 && i<action->Args.size();++i) out.Args[i]=action->Args[i];
    out.Priority=action->Priority;out.ActionTreeID=action->ActionRoutine ? int16_t(action->ActionRoutine->ID) : 0;
    if ((action->Flags & TTABFlags::FSOPushHead)>0) out.Flags|=int32_t(OBJMInteractionFlags::PushHeadContinuation);
    if ((action->Flags & TTABFlags::TS1InheritActionName)>0) out.Flags|=int32_t(OBJMInteractionFlags::CarryNameOver);
    if (action->UserQueued) out.Flags|=int32_t(OBJMInteractionFlags::UserInitiated);
    if (action->Cancelled) out.Flags|=int32_t(OBJMInteractionFlags::UserInterrupted);
}

void VMThread::RetireTS1ObjectUse() {
    if (!Context || !Context->VM->TS1 || IsCheck || TS1ObjectUses.empty()) return;
    if (Stack.size() < size_t(TS1ObjectUses.back().StackDepth)) {
        if (Stack.size()+1==size_t(TS1ObjectUses.back().StackDepth)) {
            for (const auto& use : TS1ObjectUses) if (use.Flag) TS1FlaggedUseTarget=use.Target;
        }
        auto target=TS1ObjectUses.back().Target;
        if (target) {
            auto& count=target->ObjectData.at(size_t(VMStackObjectVariable::UseCount));
            const uint16_t bits=uint16_t(count)-uint16_t(1);
            count=bits<=0x7fff ? int16_t(bits) : int16_t(int32_t(bits)-65536);
        }
        TS1ObjectUses.pop_back();
        if (TS1ObjectUses.empty()) {
            RememberTS1CurrentInteraction();
            TS1FlaggedUseTarget=nullptr;
            if (auto avatar=dynamic_cast<VMAvatar*>(Entity)) avatar->ClearTS1InteractionFootprint();
        }
    }
}

void VMThread::DetachTS1FrameMotion(VMStackFrame* frame) {
    if (IsCheck || !Context || !Context->VM || !Context->VM->TS1 ||
        !frame || frame->Thread!=this || !frame->TS1MotionToken) return;
    auto* avatar=dynamic_cast<VMAvatar*>(Entity);
    if (!avatar || avatar->Thread.get()!=this || frame->Caller!=avatar) return;
    // The avatar verifies the token before clearing either the shared model
    // or its owner frame, so an obsolete frame cannot detach a newer request.
    avatar->DetachTS1MotionRequest(frame->TS1MotionToken);
}

void VMThread::DetachTS1StackMotion(size_t first) {
    if (IsCheck) return;
    for (size_t i=Stack.size();i>first;--i) DetachTS1FrameMotion(Stack[i-1].get());
}

void VMThread::DiscardTS1ObjectUses() {
    // Reset/interrupt previously discarded the interpreter stack outright.
    // Release counts without inventing additional person-field Reset behavior.
    for (const auto& use : TS1ObjectUses) if (use.Target) {
        auto& count=use.Target->ObjectData.at(size_t(VMStackObjectVariable::UseCount));
        const uint16_t bits=uint16_t(count)-uint16_t(1);
        count=bits<=0x7fff ? int16_t(bits) : int16_t(int32_t(bits)-65536);
    }
    TS1ObjectUses.clear();
    TS1FlaggedUseTarget=nullptr;
}

int32_t VMThread::TS1ClassicUseDepth(int32_t nativeDepth) const {
    if (nativeDepth<=0 || size_t(nativeDepth)>Stack.size()) throw std::runtime_error("invalid object use depth");
    const auto& last=Stack[size_t(nativeDepth-1)];
    if (!last || last->IsRoutingFrame() || !last->Routine) throw std::runtime_error("object use ends on a non-authored frame");
    int32_t depth=0;for(int32_t i=0;i<nativeDepth;++i) if(Stack[size_t(i)] && !Stack[size_t(i)]->IsRoutingFrame() && Stack[size_t(i)]->Routine) ++depth;
    return depth;
}

int32_t VMThread::TS1NativeUseDepth(int32_t classicDepth) const {
    if (classicDepth<=0) throw std::runtime_error("invalid classic object use depth");
    int32_t classic=0,native=0;
    for (const auto& frame : Stack) { ++native;if (frame && !frame->IsRoutingFrame() && frame->Routine) ++classic; if (classic==classicDepth) return native; }
    throw std::runtime_error("invalid classic object use depth");
}

void VMThread::TS1ObjectUseTargetRemoved(VMEntity* target) {
    if (!target || !Context || !Context->VM->TS1 || IsCheck) return;
    auto clearLast=[&] {
        if (TS1LastInteraction.TargetID==target->ObjectID) TS1LastInteraction={};
        else if (TS1LastInteraction.Icon==target->ObjectID) TS1LastInteraction.Icon=0;
    };
    const auto pending=std::min(Queue.size(),size_t(ActiveQueueBlock+1));
    const auto first=Queue.begin()+pending;
    const auto kept=std::remove_if(first,Queue.end(),[&](const auto& action){
        return action && (action->Callee==target || action->IconOwner()==target);
    });
    if (kept!=Queue.end()) { Queue.erase(kept,Queue.end());QueueDirty=true; }
    bool matched=TS1FlaggedUseTarget==target;
    for (const auto& use : TS1ObjectUses) if (use.Target==target) matched=true;
    if (!matched) { clearLast();return; }
    // The matched native path removes the active interaction and pending
    // actions that reference the deleted target (as callee or icon), then
    // restarts Main at priority zero. Generic Reset() also runs authored Reset3
    // for pets and clears the queue, so perform the interpreter restart here
    // without Reset3 and preserve the unrelated pending suffix. The native
    // pending domain starts after ActiveQueueBlock; nested active prefixes are
    // discarded with the interaction, not reconstructed. The source match
    // establishes selective deletion, Main restart, and priority, but not all
    // surrounding state; auxiliary cleanup follows the existing Reset path.
    std::vector<std::shared_ptr<VMQueuedAction>> retainedPending;
    const auto pendingStart=std::min(Queue.size(),size_t(ActiveQueueBlock+1));
    retainedPending.reserve(Queue.size()-pendingStart);
    for (auto it=Queue.begin()+pendingStart;it!=Queue.end();++it)
        if (*it && (*it)->Callee!=target && (*it)->IconOwner()!=target)
            retainedPending.push_back(*it);
    while (!TS1ObjectUses.empty()) {
        const auto depth=TS1ObjectUses.back().StackDepth;
        if (depth<=0 || size_t(depth)>Stack.size()) { DiscardTS1ObjectUses(); break; }
        while (Stack.size()>=size_t(depth)) {
            DetachTS1FrameMotion(Stack.back().get());Stack.pop_back();RetireTS1ObjectUse();
        }
    }
    // Unwinding can copy the just-ended current record into LastInteraction.
    clearLast();
    if (auto avatar=dynamic_cast<VMAvatar*>(Entity)) avatar->ClearTS1InteractionFootprint();
    if (Context->VM->TS1) Entity->TS1SetSchedulerDelay(-1);
    DiscardTS1ObjectUses();
    DetachTS1StackMotion();
    Stack.clear();
    TS1RouteDefaultsCount = 0;
    TS1RouteDefaultsKnown = Context->VM->TS1;
    Queue=std::move(retainedPending);
    QueueDirty=true;
    ActiveQueueBlock=-1;
    BlockingState=nullptr;
    EODConnection=nullptr;
    Entity->Reseted=false;
    if (auto avatar=dynamic_cast<VMAvatar*>(Entity)) {
        avatar->SetPersonData(Model::VMPersonDataVariable::Priority,0);
    }
    if (!Entity->GhostImage) Entity->ExecuteEntryPoint(1,Context,false);
}

// TS1(sims): FreeSO VMThread.EndCurrentInteraction.
void VMThread::EndCurrentInteraction() {
    QueueDirty = true;
    if (ActiveQueueBlock < 0 || ActiveQueueBlock >= (int)Queue.size()) { ActiveQueueBlock = -1; return; } // C# would throw
    auto interaction = Queue[ActiveQueueBlock];
    //clear "interaction cancelled" since we are leaving the interaction
    if (interaction->Mode != VMQueueMode::ParentIdle) Entity->SetFlag(VMEntityFlags::InteractionCanceled, false);
    if (interaction->Callback != nullptr) interaction->Callback->Run(Entity);
    if (Queue.size() > 0) {
        // the callback may have changed the queue; remove this interaction wherever it is now.
        auto it = std::find(Queue.begin(), Queue.end(), interaction);
        if (it != Queue.end()) Queue.erase(it);
    }
    auto avatar = dynamic_cast<VMAvatar*>(Entity);
    if (avatar != nullptr && !IsCheck && ActiveQueueBlock == 0) {
        //some things are reset when an interaction ends
        //motive deltas reset between interactions
        ResetInteractionState();
    }
    ContinueExecution = true; //continue where the Allow Push idle left off
    ActiveQueueBlock--;
    //update priority with the priority of the interaction we are going back to (or 0)
    if (avatar != nullptr) avatar->SetPersonData(VMPersonDataVariable::Priority, (ActiveQueueBlock > -1) ? Queue[ActiveQueueBlock]->Priority : (int16_t)0);
    EvaluateQueuePriorities();
}

// TS1(sims): FreeSO VMThread.AbortCurrentInteraction.
void VMThread::AbortCurrentInteraction() {
    //go all the way back to the stack frame that Allow Push'd us.
    int ind = -1;
    for (int i = (int)Stack.size() - 1; i >= 0; i--) if (Stack[i]->SpecialResult == VMSpecialResult::Interaction) { ind = i; break; }
    if (ind != -1) {
        while ((int)Stack.size() > ind) {
            DetachTS1FrameMotion(Stack.back().get());Stack.pop_back();RetireTS1ObjectUse();
        }
        EndCurrentInteraction();
    }
}

void VMThread::Tick() {
    Tick(MAX_LOOP_COUNT, true);
}

void VMThread::Tick(int32_t instructionBudget, bool resetOnError) {
    VM::TS1InterpreterScope running(*Context->VM);
    instructionBudget = std::max<int32_t>(1, std::min(instructionBudget, MAX_LOOP_COUNT));
#ifdef IDE_COMPAT
    if (ThreadBreak == VMThreadBreakMode::Pause) return;
    else if (ThreadBreak == VMThreadBreakMode::Immediate) {
        Breakpoint(Stack.empty() ? nullptr : Stack.back().get()); return;
    }
    if (RoutineDirty) {
        for (auto& frame : Stack)
            if (frame->Routine->Chunk->RuntimeVer != frame->Routine->RuntimeVer) frame->Routine = Context->VM->Assemble(frame->Routine->Chunk);
        RoutineDirty = false;
    }
#endif

    if (BlockingState != nullptr) BlockingState->WaitTime++;
    if (DialogCooldown > 0) DialogCooldown--;

    try {
        if (!Entity->Dead) {
            if (QueueDirty) {
                EvaluateQueuePriorities();
                TryRunImmediately();
                QueueDirty = false;
            }
            if (Stack.size() == 0) {
                if (IsCheck) return; //running out of execution means check trees have ended.
                Entity->ExecuteEntryPoint(1, Context, false);
                if (Stack.size() == 0) return;
            }
            if ((!Stack.back()->ActionTree) || (Queue.size() > 0 && (Queue[0] != nullptr && Queue[0]->Callee != nullptr && !Queue[0]->Callee->Dead))) //main or our target is not dead
            {
#ifdef IDE_COMPAT
                if (ThreadBreak == VMThreadBreakMode::ReturnTrue) {
                    auto bf = Stack[BreakFrame].get();
                    HandleResult(bf, &bf->GetCurrentInstruction(), VMPrimitiveExitCode::RETURN_TRUE);
                    Breakpoint(Stack.empty() ? nullptr : Stack.back().get());
                    return;
                }
                if (ThreadBreak == VMThreadBreakMode::ReturnFalse) {
                    auto bf = Stack[BreakFrame].get();
                    HandleResult(bf, &bf->GetCurrentInstruction(), VMPrimitiveExitCode::RETURN_TRUE);
                    Breakpoint(Stack.empty() ? nullptr : Stack.back().get());
                    return;
                }
#endif
                ContinueExecution = true;
                while (ContinueExecution) {
                    // TS1(sims): a check tree that never ends (e.g. the Vacation picnic basket's 'Clean up Picnic
                    // Basket TEST' walking the parts of a Polar Bear basket, which has no "B" part, forever) answers
                    // false instead of resetting the Sim that asked (the Vacation maid / desk clerk reset every tick).
                    if (IsCheck && Context->VM->TS1 && TicksThisFrame > MAX_CHECK_LOOP_COUNT) {
                        Stack.clear();
                        LastStackExitCode = VMPrimitiveExitCode::RETURN_FALSE;
                        return;
                    }
                    if (TicksThisFrame++ > instructionBudget) throw std::runtime_error("Thread entered infinite loop! ( >" + std::to_string(instructionBudget) + " primitives)");

                    if (Entity->GetBadObjects())
                        Entity->Delete(true, Context);

                    ContinueExecution = false;
                    NextInstruction();
                }
            } else //interaction owner is dead, rip
            {
                Entity->Reset(Context);
                Entity->Reseted = true;
            }
        }
    } catch (std::exception& e) {
        if (!resetOnError) throw; // isolated caller owns recovery; never start a gameplay reset
        if (Stack.size() == 0) return; //???
        auto context = Stack[Stack.size() - 1]; //shared_ptr keeps the frame alive through the resets below
        if (Context->VM->OnSimanticsException) Context->VM->OnSimanticsException(Entity, context->Callee, e.what()); // ADDED(sims)
        bool Delete = ((dynamic_cast<VMGameObject*>(Entity) != nullptr) && (DialogCooldown > 30 * 20 - 10));
        if (DialogCooldown == 0) {
            VMSimanticsException simExcept(e.what(), context.get());
            std::string exceptionStr = "A SimAntics Exception has occurred, and has been suppressed: \r\n\r\n" + simExcept.ToString() + "\r\n\r\nThe object will be reset. Please report this!";
            VMDialogInfo info;
            info.Caller = nullptr;
            info.Icon = context->Callee;
            info.Message = exceptionStr;
            info.Title = "SimAntics Exception!";
            Context->VM->SignalDialog(info);
            DialogCooldown = 30 * 20;
        }

        if (!IsCheck) {
            if (context->Callee != nullptr) context->Callee->Reset(context->VM()->Context);
            if (context->Caller != nullptr) context->Caller->Reset(context->VM()->Context);

            if (Delete) Entity->Delete(true, context->VM()->Context);
        } else {
            Stack.clear(); // TS1(sims): FreeSO — an exception in a check tree fails the check, it does not reset anything
        }
    }
}

// TS1(sims): FreeSO VMThread.EvaluateQueuePriorities — compares against the running interaction's person data
// Priority, and only for the items after the active queue block.
void VMThread::EvaluateQueuePriorities() {
    if (ActiveQueueBlock == -1 || ActiveQueueBlock >= (int)Queue.size()) return;
    if (Context->VM->TS1 && dynamic_cast<VMAvatar*>(Entity)) {
        // The red X is record flag256, set by cancellation, not by a higher
        // pending priority. Keep the existing routing adapter notified without
        // conflating its flag with the original saved cancellation state.
        for (int i = ActiveQueueBlock + 1; i < int(Queue.size()); ) {
            if (!Queue[i]->Callee || Queue[i]->Callee->Dead) Queue.erase(Queue.begin() + i);
            else ++i;
        }
        Entity->SetFlag(VMEntityFlags::InteractionCanceled, TS1ShouldNotifyIdle());
        return;
    }
    auto active = Queue[ActiveQueueBlock];
    auto avatar = dynamic_cast<VMAvatar*>(Entity);
    int32_t CurrentPriority = (avatar == nullptr) ? active->Priority : (int32_t)avatar->GetPersonData(VMPersonDataVariable::Priority);
    if (CurrentPriority == (int32_t)VMQueuePriority::Autonomous) CurrentPriority -= 1; // allow other auto actions to interrupt us

    // HACK: TS1 pushes a "Cancel Interaction" action onto the tree to interrupt itself, as well as setting current interaction to prio 0.
    // We're simulating that by notifiying idle if priority hits 0. (implied cancel interaction queued)
    // Make sure this interaction is not *meant* to be priority 0, like tso's idle.
    active->Cancelled = active->Priority != 0 && CurrentPriority == 0;
    for (int i = ActiveQueueBlock + 1; i < (int)Queue.size(); i++) {
        if (Queue[i]->Callee == nullptr || Queue[i]->Callee->Dead) {
            Queue.erase(Queue.begin() + i); i--; //remove interactions to dead objects (not within active queue block)
            continue;
        }
        if ((int32_t)Queue[i]->Priority > CurrentPriority) {
            active->Cancelled = true;
            Entity->SetFlag(VMEntityFlags::InteractionCanceled, true);
        }
    }
}

void VMThread::NextInstruction() {
    VM::TS1InterpreterScope running(*Context->VM);
    /** Next instruction **/
    if (Stack.empty()) return;
    auto currentFrame = Stack.back(); //hold a reference: the frame may pop itself
    if (currentFrame == nullptr) return;

    if (currentFrame->IsRoutingFrame()) {
        auto rf = (VMRoutingFrame*)currentFrame.get();
        // TS1(sims): a route (Go To Relative Position / Routing Slot) never finishes in the tick it starts, even
        // when the Sim is already there or no route exists. NPC loops such as the Vacation desk clerks' 'Main' (go to
        // the desk, Wait For Notify 0 ticks, again) relied on it and spun until the loop guard reset them.
        if (rf->HeldResult >= 0) { auto r = (VMPrimitiveExitCode)rf->HeldResult; rf->HeldResult = -1; HandleResult(rf, nullptr, r); return; }
        auto r = rf->Tick();
        if (Context->VM->TS1 && (r == VMPrimitiveExitCode::RETURN_TRUE || r == VMPrimitiveExitCode::RETURN_FALSE)
            && rf->PushTick == Context->Clock->Ticks && !IsCheck) {
            rf->HeldResult = (int8_t)r;
            r = VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
        }
        HandleResult(rf, nullptr, r);
    }
    else ExecuteInstruction(currentFrame.get());
}

VMRoutingFrame* VMThread::PushNewRoutingFrame(VMStackFrame* frame, bool failureTrees) {
    auto childFrame = std::make_shared<VMRoutingFrame>();
    childFrame->Routine = frame->Routine;
    childFrame->Caller = frame->Caller;
    childFrame->Callee = frame->Callee;
    childFrame->CodeOwner = frame->CodeOwner;
    childFrame->StackObject = frame->StackObject;
    childFrame->StackObjectRawID = frame->StackObjectRawID; // TS1(sims): neighbour id in stack object
    childFrame->Thread = this;
    childFrame->CallFailureTrees = failureTrees;
    childFrame->PushTick = Context->Clock->Ticks; // TS1(sims): see NextInstruction
    childFrame->ActionTree = frame->ActionTree;

    Stack.push_back(childFrame);
    return childFrame.get();
}

void VMThread::ExecuteSubRoutine(VMStackFrame* frame, BHAV* bhav, FSO::Content::GameObject* codeOwner, const Primitives::VMSubRoutineOperand& args) {
    if (bhav == nullptr) {
        Pop(VMPrimitiveExitCode::ERROR);
        return;
    }

    auto routine = Context->VM->Assemble(bhav);
    auto childFrame = std::make_shared<VMStackFrame>();
    childFrame->Routine = routine;
    childFrame->Caller = frame->Caller;
    childFrame->Callee = frame->Callee;
    childFrame->CodeOwner = codeOwner;
    childFrame->StackObject = frame->StackObject;
    childFrame->StackObjectRawID = frame->StackObjectRawID; // TS1(sims): neighbour id in stack object
    childFrame->ActionTree = frame->ActionTree;
    childFrame->Args.assign((routine->Arguments > 4) ? routine->Arguments : 4, 0);
    for (size_t i = 0; i < childFrame->Args.size(); i++) {
        int16_t argValue = (i > 3) ? (int16_t)-1 : args.Arguments[i];
        if (argValue == -1 && args.UseTemp0()) { // TS1(sims): FreeSO UseTemp0
            argValue = TempRegisters[i];
        }
        childFrame->Args[i] = argValue;
    }
    Push(childFrame);
}

void VMThread::ExecuteInstruction(VMStackFrame* frame) {
    VMInstruction& instruction = frame->GetCurrentInstruction();
    auto opcode = instruction.Opcode;

    if (opcode >= 256) {
        BHAV* bhav = nullptr;
        FSO::Content::GameObject* CodeOwner;
        if (opcode >= 8192) {
            // Semi-Global sub-routine call
            bhav = (frame->ScopeResource()->SemiGlobal == nullptr) ? nullptr : frame->ScopeResource()->SemiGlobal->Get<BHAV>(opcode);
        } else if (opcode >= 4096) {
            // Private sub-routine call
            bhav = frame->ScopeResource()->Get<BHAV>(opcode);
        } else {
            // Global sub-routine call
            //CodeOwner = frame.Global.Resource;
            bhav = frame->Global()->Resource->Get<BHAV>(opcode);
        }

        CodeOwner = frame->CodeOwner;

        auto operand = static_cast<Primitives::VMSubRoutineOperand*>(instruction.Operand.get());
        if (Context->VM->OnTraceInstruction) Context->VM->OnTraceInstruction(frame, opcode, 255); // ADDED(sims): diagnostics
        ExecuteSubRoutine(frame, bhav, CodeOwner, *operand);
#ifdef IDE_COMPAT
        if ((Stack.size() > 0 && Stack.back()->GetCurrentInstruction().Breakpoint) || ThreadBreak == VMThreadBreakMode::StepIn) {
            Breakpoint(frame);
            ContinueExecution = false;
        } else
#endif
        {
            ContinueExecution = true;
        }
        return;
    }

    auto primitive = Context->Primitives[opcode].get();
    if (primitive == nullptr) {
        HandleResult(frame, &instruction, VMPrimitiveExitCode::GOTO_TRUE);
        return;
    }

    VMPrimitiveHandler* handler = primitive->GetHandler();
    auto result = handler->Execute(frame, instruction.Operand.get());
    if (Context->VM->OnTraceInstruction) Context->VM->OnTraceInstruction(frame, opcode, (int)result); // ADDED(sims): diagnostics

    HandleResult(frame, &instruction, result);
}

void VMThread::HandleResult(VMStackFrame* frame, VMInstruction* instruction, VMPrimitiveExitCode result) {
    if (TS1SynchronousActive && (result==VMPrimitiveExitCode::CONTINUE_NEXT_TICK ||
        result==VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK ||
        result==VMPrimitiveExitCode::GOTO_FALSE_NEXT_TICK || result==VMPrimitiveExitCode::INTERRUPT))
        throw std::runtime_error(TS1InitializerActive ? "TS1 initializer attempted to suspend" : "TS1 synchronous call attempted to suspend");
    switch (result) {
        // Don't advance the instruction pointer, this primitive isnt finished yet
        case VMPrimitiveExitCode::CONTINUE_NEXT_TICK:
            ContinueExecution = false;
            break;
        case VMPrimitiveExitCode::ERROR:
            Pop(result);
            break;
        case VMPrimitiveExitCode::RETURN_TRUE:
        case VMPrimitiveExitCode::RETURN_FALSE:
            /** pop stack and return false **/
            Pop(result);
            break;
        case VMPrimitiveExitCode::GOTO_TRUE:
            MoveToInstruction(frame, (instruction == nullptr) ? (uint8_t)0 : instruction->TruePointer, true); // FIX(sims): routing frames pass no instruction (C# passed their current one; MoveToInstruction ignores it)
            break;
        case VMPrimitiveExitCode::GOTO_FALSE:
            MoveToInstruction(frame, (instruction == nullptr) ? (uint8_t)0 : instruction->FalsePointer, true);
            break;
        case VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK:
            MoveToInstruction(frame, (instruction == nullptr) ? (uint8_t)0 : instruction->TruePointer, true);
            // Stack returns may choose immediate continuation; this primitive
            // still owns the explicit next-tick boundary (both FreeSO pins).
            ContinueExecution = false;
            break;
        case VMPrimitiveExitCode::GOTO_FALSE_NEXT_TICK:
            MoveToInstruction(frame, (instruction == nullptr) ? (uint8_t)0 : instruction->FalsePointer, true);
            // Stack returns may choose immediate continuation; this primitive
            // still owns the explicit next-tick boundary (both FreeSO pins).
            ContinueExecution = false;
            break;
        case VMPrimitiveExitCode::CONTINUE:
            ContinueExecution = true;
            break;
        case VMPrimitiveExitCode::INTERRUPT:
            if (Context->VM->TS1 && !IsCheck) DiscardTS1ObjectUses();
            DetachTS1StackMotion();
            Stack.clear();
            if (Context->VM->TS1 && !IsCheck) {
                TS1RouteDefaultsCount = 0;
                TS1RouteDefaultsKnown = true;
            }
            QueueDirty = true;
            if (Queue.size() > 0) Queue.erase(Queue.begin());
            LastStackExitCode = result;
            break;
    }
}

void VMThread::MoveToInstruction(VMStackFrame* frame, uint8_t instruction, bool continueExecution) {
    if (frame->IsRoutingFrame()) {
        //TODO: Handle returning false into the pathfinder (indicates failure)
        return;
    }

    // DVD 00474e97 clears primitive state on a completed instruction branch,
    // while CONTINUE/next-tick retries retain it.
    if (Context->VM->TS1) { frame->PrimitiveState = 0; frame->TS1LegacyReach = false; }
    // A return can unwind an interaction and request a yield from Pop.
    // Seed the branch policy before dispatch, as in both pinned FreeSO versions.
    ContinueExecution = continueExecution;
    switch (instruction) {
        case 255:
            Pop(VMPrimitiveExitCode::RETURN_FALSE);
            break;
        case 254:
            Pop(VMPrimitiveExitCode::RETURN_TRUE); break;
        case 253:
            // TS1(sims): FreeSO — attempt to continue along only path available (FreeSims popped with ERROR).
            if (frame->GetCurrentInstruction().TruePointer != 253) {
                MoveToInstruction(frame, frame->GetCurrentInstruction().TruePointer, continueExecution); return;
            } else if (frame->GetCurrentInstruction().FalsePointer != 253) {
                MoveToInstruction(frame, frame->GetCurrentInstruction().FalsePointer, continueExecution); return;
            }
            Pop(VMPrimitiveExitCode::ERROR); break;
        default:
            frame->InstructionPointer = instruction;
            if (frame->GetCurrentInstruction().Breakpoint ||
                (ThreadBreak != VMThreadBreakMode::Active && (
                    ThreadBreak == VMThreadBreakMode::StepIn ||
                    (ThreadBreak == VMThreadBreakMode::StepOver && (int)Stack.size() - 1 <= BreakFrame) ||
                    (ThreadBreak == VMThreadBreakMode::StepOut && (int)Stack.size() <= BreakFrame)
                ))) {
                Breakpoint(frame);
            }
            break;
    }

    // Preserve any continuation/yield decision made while unwinding the stack.
    ContinueExecution = (ThreadBreak != VMThreadBreakMode::Pause) && ContinueExecution;
}

void VMThread::Breakpoint(VMStackFrame* frame) {
    if (IsCheck) return; //can't breakpoint in check trees.
    ThreadBreak = VMThreadBreakMode::Pause;
    BreakFrame = -1;
    for (size_t i = 0; i < Stack.size(); i++) if (Stack[i].get() == frame) { BreakFrame = (int32_t)i; break; }
    Context->VM->BreakpointHit(Entity);
}

// TS1(sims): FreeSO VMThread.Pop — the frame that ran an interaction (SpecialResult Interaction) ends that
// interaction; the frame that pushed it continues next tick at the same instruction (C#: CONTINUE_NEXT_TICK, or
// CONTINUE for run immediately). FreeSims detected a "context switch" and always removed Queue[0].
void VMThread::Pop(VMPrimitiveExitCode result) {
    // Missing/invalid subroutines are interpreter faults in the DVD's
    // immediate runner, not an authored false result from Init.
    if (TS1SynchronousActive && result==VMPrimitiveExitCode::ERROR)
        throw std::runtime_error(TS1InitializerActive ? "TS1 initializer interpreter error" : "TS1 synchronous call interpreter error");
    if (Context->VM->OnTraceFrameReturn) {
        auto frame = Stack.back();
        Context->VM->OnTraceFrameReturn(frame.get(), static_cast<int>(result));
    }
    auto discardResult = Stack.back()->SpecialResult;
    DetachTS1FrameMotion(Stack.back().get());
    Stack.pop_back();
    RetireTS1ObjectUse();
    LastStackExitCode = result;

    // The immediate call returns to its sentinel, never branches the prepared
    // Main frame or treats Init as a completed user interaction.
    if (TS1SynchronousActive && !Stack.empty() && Stack.back()==TS1SynchronousBoundary) {
        DetachTS1FrameMotion(Stack.back().get());
        Stack.pop_back();
        ContinueExecution=false;
        return;
    }

    if (discardResult == VMSpecialResult::Interaction) //interaction switching back to main (it cannot be the other way...)
    {
        auto interaction = ActiveAction();
        EndCurrentInteraction();
        // DVD 00474cae advances a Gosub Found Action parent by the child's
        // result. Idle For Input marks a retrying parent and ignores that result.
        // PrimitiveState is carried by the existing classic frame companion.
        const bool originalParent = Context->VM->TS1 && !Stack.empty() &&
            !Stack.back()->IsRoutingFrame() &&
            (interaction == nullptr || (interaction->Flags & TTABFlags::RunImmediately) == 0);
        if (originalParent) {
            //474f17 ->474cf4 returns0 for the retrying parent.475027
            //loops that parent in the SAME owner visit, without changing PC
            //or primitive state. Its authored Sleep owns any later yield.
            if(Stack.back()->PrimitiveState!=0)result=VMPrimitiveExitCode::CONTINUE;
        } else {
            result = (interaction == nullptr || (interaction->Flags & TTABFlags::RunImmediately) == 0)
                ? VMPrimitiveExitCode::CONTINUE_NEXT_TICK : VMPrimitiveExitCode::CONTINUE;
        }
    } else if (discardResult == VMSpecialResult::Retry) {
        result = VMPrimitiveExitCode::CONTINUE;
    }

    if (Stack.size() > 0) {
        if (result == VMPrimitiveExitCode::RETURN_TRUE)
            result = VMPrimitiveExitCode::GOTO_TRUE;
        else if (result == VMPrimitiveExitCode::RETURN_FALSE)
            result = VMPrimitiveExitCode::GOTO_FALSE;
        auto currentFrame = Stack.back();
        if (currentFrame->IsRoutingFrame()) HandleResult(currentFrame.get(), nullptr, result);
        else HandleResult(currentFrame.get(), &currentFrame->GetCurrentInstruction(), result);
    } else // :(
    {
        ContinueExecution = false;
    }
}

bool VMThread::Push(std::shared_ptr<VMStackFrame> frame) {
    if (frame->Routine->Instructions.size() == 0) return false; //some bhavs are empty... do not execute these.
    Stack.push_back(frame);

    /** Initialize the locals **/
    auto numLocals = std::max(frame->Routine->Locals, frame->Routine->Arguments);
    frame->Locals.assign(numLocals, 0);
    frame->Thread = this;

    frame->InstructionPointer = 0;
    return true;
}

/// Add an item to the action queue
// TS1(sims): FreeSO VMThread.EnqueueAction (TS1: no leapfrog, queue skip of lower priority items).
bool VMThread::EnqueueAction(std::shared_ptr<VMQueuedAction> invocation) {
    invocation->UID = ActionUID++;
    QueueDirty = true;
    if (!IsCheck && (invocation->Flags & TTABFlags::RunImmediately) > 0) {
        // shove this action in the queue to try run it next tick.
        // interaction can be run normally if we actually hit it using allow push. (unlikely)
        // otherwise next tick will detect the "run immediately" interaction's prescence.
        // and it will be pushed to the stack immediately.
        // TODO: check if any interactions of this kind clobber the temps.
        // this doesnt ""run immediately"", but is good enough.

        invocation->Mode = VMQueueMode::Idle; //hide
        //invocation.Priority = short.MinValue; (FreeSO; FreeSims set it)
        this->Queue.push_back(invocation);
        return true;
    }
    // DVD 0045d377 keeps an eight-record pending ring. These are ordinary
    // original records; legacy generated parent modes keep their adapter below.
    const size_t pending = size_t(ActiveQueueBlock + 1);
    const bool ordinary = invocation->Mode == VMQueueMode::Normal &&
        std::all_of(Queue.begin() + pending, Queue.end(), [](const auto& action) {
            return action && action->Mode == VMQueueMode::Normal;
        });
    if (Context->VM->TS1 && ordinary) {
        const auto count = Queue.size() - pending;
        const bool forced = (invocation->Flags & TTABFlags::FSOPushHead) != 0;
        if ((invocation->Flags & TTABFlags::TS1InheritActionName) != 0 && forced) {
            if (const auto active = ActiveAction()) invocation->Name = active->Name;
        }
        if (forced) {
            // The original's full-ring continuation path retains only six
            // old records alongside the new head (seven total), without the
            // ordinary skipped-entry notifications.
            if (count >= 8) Queue.erase(Queue.begin() + pending + 6, Queue.end());
            Queue.insert(Queue.begin() + pending, invocation);
        } else {
            auto position = Queue.size();
            while (position > pending && Queue[position - 1]->Priority < invocation->Priority) --position;
            std::vector<std::shared_ptr<VMQueuedAction>> prefix(Queue.begin() + pending, Queue.begin() + position);
            std::vector<VMQueuedAction> saved;
            for (const auto& action : prefix) saved.push_back(*action);
            // Notifications run synchronously against the old live ring. A
            // callback may mutate it; reacquire each item by index. Restore the
            // copied prefix only afterwards, as in the original ring rebuild.
            for (size_t i = position; i < Queue.size(); ++i) {
                const auto action = Queue[i];
                if (action->Callee && !action->Callee->Dead)
                    action->Callee->ExecuteEntryPoint(4, Context, true, Entity);
            }
            Queue.resize(std::min(Queue.size(), size_t(ActiveQueueBlock + 1)));
            for (size_t i = 0; i < prefix.size(); ++i) {
                *prefix[i] = saved[i];
                Queue.push_back(prefix[i]);
            }
            if (Queue.size() - size_t(ActiveQueueBlock + 1) >= 8) return false;
            Queue.push_back(invocation);
        }
        EvaluateQueuePriorities();
        return true;
    }
    auto leapfrog = Context->VM->TS1 ? (TTABFlags)0 : TTABFlags::Leapfrog;

    if (Queue.size() == 0) //if empty, just queue right at the front
        this->Queue.push_back(invocation);
    else if ((invocation->Flags & TTABFlags::FSOPushHead) > 0)
        //place right after active interaction, ignoring all priorities.
        this->Queue.insert(Queue.begin() + std::min((size_t)(ActiveQueueBlock + 1), Queue.size()), invocation);
    else if ((invocation->Flags & (TTABFlags::FSOPushTail | leapfrog)) > 0 && invocation->Mode != VMQueueMode::ParentExit) {
        //this one's weird. start at the left til there's a lower priority. (eg. parent exit or idle)
        for (int i = ActiveQueueBlock + 1; i < (int)Queue.size(); i++) {
            if (Queue[i]->Priority < invocation->Priority) //we have higher priority than this item
            {
                this->Queue.insert(Queue.begin() + i, invocation); //insert before this. will come before parent exit and pals.
                EvaluateQueuePriorities();
                return true;
            }
        }
        this->Queue.push_back(invocation); //right at the end! (somehow)
    } else //we've got an even harder job! find a place for this interaction based on its priority
    {
        bool hitParentEnd = (invocation->Mode != VMQueueMode::ParentIdle);
        for (int i = (int)Queue.size() - 1; i > ActiveQueueBlock; i--) {
            if (hitParentEnd && (invocation->Priority <= Queue[i]->Priority || Queue[i]->Mode == VMQueueMode::ParentExit)) //skip until we find a parent exit or something with the same or higher priority.
            {
                this->Queue.insert(Queue.begin() + (i + 1), invocation);
                if (Context->VM->TS1 && invocation->Priority <= Queue[i]->Priority) {
                    // queue skip all items with a lower priority (after this interaction)
                    // i've verified this happens in ts1, but I don't know if it does in TSO so i've locked it for now.
                    i += 2;
                    while (i < (int)this->Queue.size()) {
                        // FIX(sims): the player's own queue survives (EP agent: user actions vanished when a second
                        // priority-100 push, e.g. Vacation 'Be Asked About', landed behind the first). Pushed/autonomous
                        // lower-priority items are still skipped as in FreeSO.
                        if (Queue[i]->UserQueued) { i++; continue; }
                        auto before = Queue.size();
                        CancelAction(this->Queue[i]->UID);
                        if (Queue.size() == before) i++; // FIX(sims): C# loops forever when the item cannot be removed (parent exit / must run)
                    }
                }
                EvaluateQueuePriorities();
                return true;
            }
            if (Queue[i]->Mode == VMQueueMode::ParentExit) hitParentEnd = true;
        }
        this->Queue.insert(Queue.begin() + std::min((size_t)(ActiveQueueBlock + 1), Queue.size()), invocation); //this is more important than all other queued items that are not running, so stick this to run next.
    }
    EvaluateQueuePriorities();
    return true;
}

// TS1(sims): FreeSO VMThread.CancelAction (TS1 runs the callee's "queue skipped" entry point, 4).
void VMThread::CancelAction(uint16_t actionUID) {
    std::shared_ptr<VMQueuedAction> interaction;
    int index = -1;
    for (size_t i = 0; i < Queue.size(); i++) if (Queue[i]->UID == actionUID) { interaction = Queue[i]; index = (int)i; break; }
    if (interaction != nullptr) {
        QueueDirty = true;
        if (Context->VM->TS1 && interaction->Mode == VMQueueMode::Normal) {
            if (index > ActiveQueueBlock) {
                std::vector<std::shared_ptr<VMQueuedAction>> kept;
                std::vector<VMQueuedAction> saved;
                for (size_t i = size_t(ActiveQueueBlock + 1); i < Queue.size(); ++i) {
                    const auto item = Queue[i];
                    if (item->UID == actionUID) {
                        if (item->Callee && !item->Callee->Dead)
                            item->Callee->ExecuteEntryPoint(4, Context, true, Entity);
                    } else {
                        kept.push_back(item);
                        saved.push_back(*item);
                    }
                }
                Queue.resize(std::min(Queue.size(), size_t(ActiveQueueBlock + 1)));
                for (size_t i = 0; i < kept.size(); ++i) {
                    *kept[i] = saved[i];
                    Queue.push_back(kept[i]);
                }
            } else {
                interaction->Cancelled = true;
                // DVD 0045d764 leaves the active record priority unchanged.
                // Lower the current priority and request the authored cleanup
                // action when no pending record already wakes Idle For Input.
                if (auto avatar = dynamic_cast<VMAvatar*>(Entity))
                    avatar->SetPersonData(VMPersonDataVariable::Priority, 0);
                Entity->SetFlag(VMEntityFlags::InteractionCanceled, true); // routing adapter
                if (!TS1ShouldNotifyIdle()) {
                    auto cancel = Entity->GetAction(33, Entity, Context);
                    if (cancel) {
                        cancel->Args.assign(4, 0);
                        cancel->Priority = int16_t(VMQueuePriority::UserDriven);
                        cancel->Flags |= TTABFlags::TS1HideQueueIcon;
                        EnqueueAction(cancel);
                    }
                }
            }
            return;
        }
        interaction->Cancelled = true;
        //cancel any idle parents after this interaction

        if (interaction->Mode == VMQueueMode::ParentIdle) {
            for (int i = index + 1; i < (int)Queue.size(); i++) {
                if (Queue[i]->Mode == VMQueueMode::ParentIdle) {
                    if (interaction->Mode == VMQueueMode::ParentIdle) { Queue.erase(Queue.begin() + i); i--; }
                    else {
                        Queue[i]->Cancelled = true;
                        Queue[i]->Priority = 0;
                    }
                } else if (Queue[i]->Mode == VMQueueMode::ParentExit) {
                    Queue[i]->Cancelled = true;
                    Queue[i]->Priority = 0;
                }
                //parent exit needs to "appear" like it is cancelled.
            }
        }

        auto canQueueSkip = (interaction->Flags & TTABFlags::MustRun) == 0 || Context->VM->TS1; // TS1: bit 10 is "allow dogs", not must run
        bool inMain = Stack.size() > 0 && !Stack.back()->ActionTree;

        if (canQueueSkip && (index > ActiveQueueBlock || inMain) && (interaction->Mode == VMQueueMode::Normal || (interaction->Flags & TTABFlags::FSODirectControl) > 0)) {
            auto it = std::find(Queue.begin(), Queue.end(), interaction);
            if (it != Queue.end()) Queue.erase(it);
            if (Context->VM->TS1 && interaction->Callee != nullptr) interaction->Callee->ExecuteEntryPoint(4, Context, true, Entity); //queue skipped
        } else {
            Entity->SetFlag(VMEntityFlags::InteractionCanceled, true);
            auto avatar = dynamic_cast<VMAvatar*>(Entity);
            if (avatar != nullptr) avatar->SetPersonData(VMPersonDataVariable::Priority, 0);
        }
    }
}

// TS1(sims): FreeSO — set the new interaction's priority, push it as an interaction frame.
bool VMThread::ExecuteAction(std::shared_ptr<VMQueuedAction> action) {
    auto avatar = dynamic_cast<VMAvatar*>(Entity);
    if (avatar != nullptr) avatar->SetPersonData(VMPersonDataVariable::Priority, action->Priority);
    auto frame = action->ToStackFrame(Entity);
    frame->SpecialResult = VMSpecialResult::Interaction;
    return Push(frame);
}

// TS1(sims): FreeSO VMThread.CheckTS1Action.
std::unique_ptr<std::vector<VMPieMenuInteraction>> VMThread::CheckTS1Action(std::shared_ptr<VMQueuedAction> action, bool autonomous, bool joining) {
    if (!action) return nullptr;
    std::unique_ptr<std::vector<VMPieMenuInteraction>> result(new std::vector<VMPieMenuInteraction>());
    if (!action->Callee || action->Callee->Dead || action->Callee->GetFlag(VMEntityFlags::Burning)) return nullptr;

    auto avatar = dynamic_cast<VMAvatar*>(Entity);
    if (avatar != nullptr && (action->Flags & TTABFlags::FSOSkipPermissions) == 0) //just let everyone use the CSR interactions
    {
        if ((action->Flags & (TTABFlags::TS1AllowCats | TTABFlags::TS1AllowDogs)) > 0) {
            //interaction can only be performed by cats or dogs
            //if (!avatar.IsPet) return null;
            //check we're the correct type
            if (avatar->IsCat() && (action->Flags & TTABFlags::TS1AllowCats) == 0) return nullptr;
            if (avatar->IsDog() && (action->Flags & TTABFlags::TS1AllowDogs) == 0) return nullptr;
        } else if (avatar->IsPet()) return nullptr; //not allowed

        auto isVisitor = avatar->GetPersonData(VMPersonDataVariable::PersonType) == 1 && avatar->GetPersonData(VMPersonDataVariable::GreetStatus) < 2;
        //avatar.ObjectID != Context.VM.GetGlobalValue(3);
        // FIX(sims): the player's own Sims are never visitors for the TTAB permission: on a Downtown/Vacation/community
        // lot the arriving traveller is spawned as person type 1, greet 1, so every interaction without the visitor
        // flag (Greet, the waiter, ...) was refused and the socials' 'get social eligibility' (selected Sim + target not
        // greeted) failed: no Talk/Greet/Flirt at all on any community lot. Members of the active family who are on
        // the lot (FAMI.RuntimeSubset: the travellers) count as residents.
        if (isVisitor && Context->VM->TS1) {
            auto fam = Utils::VMTS1Neighbourhood::CurrentFamily(Context->VM);
            if (fam != nullptr && avatar->GetPersonData((VMPersonDataVariable)Utils::VMTS1Neighbourhood::PD_FAMILY_NUMBER) == (int16_t)fam->ChunkID
                && std::find(fam->RuntimeSubset.begin(), fam->RuntimeSubset.end(), avatar->Object->OBJ->GUID) != fam->RuntimeSubset.end())
                isVisitor = false;
        }
        auto debugTrees = false;

        const auto age = avatar->GetPersonData(VMPersonDataVariable::PersonsAge);
        const bool child = age > 0 && age < 18;
        TTABFlags ts1State = (TTABFlags)(
              (int32_t)((isVisitor) ? TTABFlags::AllowVisitors : (TTABFlags)0)
            | (int32_t)(child ? TTABFlags::TS1NoChild : (TTABFlags)0)
            | (int32_t)((!child && !avatar->IsPet()) ? TTABFlags::TS1NoAdult : (TTABFlags)0));

        //DEBUG: enable debug interction for all CSRs.
        if ((action->Flags & TTABFlags::Debug) > 0) {
            if (!isVisitor && debugTrees)
                return result; //do not bother running check
            else
                return nullptr; //disable debug for everyone else.
        }

        //NEGATIVE EFFECTS:
        auto pos = (TTABFlags)(ts1State & (TTABFlags::TS1NoChild | TTABFlags::TS1NoAdult));
        auto ts1Compare = action->Flags;
        if ((pos & ts1Compare) > 0) return nullptr;

        auto negMask = (TTABFlags::AllowVisitors);

        auto negatedFlags = (TTABFlags)((~(int32_t)ts1Compare) & (int32_t)negMask);
        if ((negatedFlags & ts1State) > 0) return nullptr; //we are disallowed
    }
    if (action->CheckRoutine != nullptr) {
        auto frame = std::make_shared<VMStackFrame>();
        frame->Caller = Entity;
        frame->Callee = action->Callee;
        frame->CodeOwner = action->CodeOwner;
        frame->StackObject = action->StackObject;
        frame->Routine = action->CheckRoutine;
        frame->Args.assign(4, 0);
        if (autonomous) frame->Args[0] = 1;
        // DVD458485: type1 (joining) is the second check argument.
        if (joining) frame->Args[1] = 1;
        if (EvaluateCheck(Context, Entity, frame, nullptr, result.get()) != VMPrimitiveExitCode::RETURN_TRUE) return nullptr;
    }
    return result;
}

std::unique_ptr<std::vector<VMPieMenuInteraction>> VMThread::CheckAction(std::shared_ptr<VMQueuedAction> action, bool autonomous) {
    // 1. check action flags for permissions (if we are avatar)
    // 2. run check tree

    // rules:
    // Dogs/Cats means people CANNOT use these interactions. (DogsFlag|CatsFlag & IsDog|IsCat)

    // When Allow Object Owner is OFF it disallows the object owner, otherwise no effect
    // Visitors, Roommates, Ghosts have this same negative effect.
    // Friends apprars to override Owner, Visitors, Roommates

    // Allow CSRs:positive effect.

    if (action == nullptr) return nullptr;
    if (Context->VM->TS1) return CheckTS1Action(action, autonomous); // TS1(sims): FreeSO
    std::unique_ptr<std::vector<VMPieMenuInteraction>> result(new std::vector<VMPieMenuInteraction>());

    auto avatarEnt = dynamic_cast<VMAvatar*>(Entity);
    if (((action->Flags & TTABFlags::MustRun) == 0) && avatarEnt != nullptr && ((action->Flags2 & TSOFlags::AllowCSRs) == 0)) //just let everyone use the CSR interactions
    {
        auto avatar = avatarEnt;

        if (avatar->GetSlot(0) != nullptr && (action->Flags | TTABFlags::TSOAvailableCarrying) == 0) return nullptr;

        if ((action->Flags & (TTABFlags::TS1AllowCats | TTABFlags::TS1AllowDogs)) > 0) {
            //interaction can only be performed by cats or dogs
            if (!avatar->IsPet()) return nullptr;
            //check we're the correct type
            if (avatar->IsCat() && (action->Flags & TTABFlags::TS1AllowCats) == 0) return nullptr;
            if (avatar->IsDog() && (action->Flags & TTABFlags::TS1AllowDogs) == 0) return nullptr;
        }
        else if (avatar->IsPet()) return nullptr; //not allowed

        if ((action->Flags & TTABFlags::TSOIsRepair) > 0) return nullptr;

        auto avaState = (VMTSOAvatarState*)avatar->TSOState();
        auto calleeObjState = (dynamic_cast<VMGameObject*>(action->Callee) == nullptr) ? nullptr : (VMTSOObjectState*)action->Callee->TSOState();
        TSOFlags tsoState = (TSOFlags)(
            (int64_t)((calleeObjState == nullptr || avatar->PersistID == calleeObjState->OwnerID) ? TSOFlags::AllowObjectOwner : (TSOFlags)0)
            | (int64_t)((avaState->Permissions == VMTSOAvatarPermissions::Visitor) ? TSOFlags::AllowVisitors : (TSOFlags)0)
            | (int64_t)((avaState->Permissions >= VMTSOAvatarPermissions::Roommate) ? TSOFlags::AllowRoommates : (TSOFlags)0)
            | (int64_t)((avaState->Permissions == VMTSOAvatarPermissions::Admin) ? TSOFlags::AllowCSRs : (TSOFlags)0)
            | (int64_t)((avatar->GetPersonData(VMPersonDataVariable::IsGhost) > 0) ? TSOFlags::AllowGhost : (TSOFlags)0)
            | (int64_t)TSOFlags::AllowFriends);
        TSOFlags tsoCompare = action->Flags2;
        //if flags are empty apart from "Non-Empty", force everything but visitor. (a kind of default state)
        if (tsoCompare == (int)TSOFlags::NonEmpty) tsoCompare |= TSOFlags::AllowFriends | TSOFlags::AllowRoommates | TSOFlags::AllowObjectOwner;

        //DEBUG: enable debug interction for all roommates. change to only CSRs for production!
        if ((action->Flags & TTABFlags::Debug) > 0) {
            // if ((tsoState & TSOFlags.AllowRoommates) > 0)
            //   return result; //do not bother running check
            //else
            return nullptr; //disable debug for everyone else.
        }

        if ((action->Flags & TTABFlags::TSOAvailableWhenDead) > 0) tsoCompare |= TSOFlags::AllowGhost;
        if ((action->Flags & TTABFlags::AllowVisitors) > 0) tsoCompare |= TSOFlags::AllowVisitors; //wrong???????

        auto posMask = (TSOFlags::AllowObjectOwner);
        if (((tsoState & posMask) & (tsoCompare & posMask)) == 0) {
            //NEGATIVE EFFECTS:
            auto negMask = (TSOFlags::AllowVisitors | TSOFlags::AllowRoommates | TSOFlags::AllowGhost);

            auto negatedFlags = (TSOFlags)((~tsoCompare) & negMask);
            if ((negatedFlags & tsoState) > 0) return nullptr; //we are disallowed
            //if ((tsoCompare & TSOFlags.AllowCSRs) > 0) return null; // && (tsoState & TSOFlags.AllowCSRs) == 0
        }
    }
    if (((action->Flags & TTABFlags::MustRun) == 0 || ((action->Flags & TTABFlags::TSORunCheckAlways) > 0))
        && action->CheckRoutine != nullptr) {
        auto frame = std::make_shared<VMStackFrame>();
        frame->Caller = Entity;
        frame->Callee = action->Callee;
        frame->CodeOwner = action->CodeOwner;
        frame->StackObject = action->StackObject;
        frame->Routine = action->CheckRoutine;
        frame->Args.assign(4, 0);
        if (autonomous) frame->Args[0] = 1;
        if (EvaluateCheck(Context, Entity, frame, nullptr, result.get()) != VMPrimitiveExitCode::RETURN_TRUE) return nullptr;
    }

    return result;
}

}}}
