/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Primitives.VMReach (primitives/VMReach.cs)
#include "simantics/primitives/VMReach.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include "core/io.h"
#include "content/ContentManager.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMAnimationState.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace FSO::SimAntics::Engine;
using FSO::Files::Formats::IFF::Chunks::SLOT;
using FSO::Files::Formats::IFF::Chunks::SLOTItem;

namespace {
// C# `entity.Slots.Slots[0][index]` (Dictionary + List indexers): throws instead of reading out of range.
SLOTItem* SlotZeroAt(VMEntity* entity, int32_t index, VMStackFrame* context)
{
    if (entity->Slots == nullptr) throw VMSimanticsException("Object reference not set to an instance of an object.", context);
    auto it = entity->Slots->Slots.find(0);
    if (it == entity->Slots->Slots.end()) throw VMSimanticsException("The given key was not present in the dictionary.", context);
    if (index < 0 || index >= (int32_t)it->second.size()) throw VMSimanticsException("Index was out of range.", context);
    return &it->second[index];
}

// C# (int)Math.Round((slot.Height != 5) ? SLOT.HeightOffsets[slot.Height - 1] : slot.Offset.Z)
int32_t SlotHeight(SLOTItem* slot, VMStackFrame* context)
{
    double value;
    if (slot->Height != 5)
    {
        if (slot->Height - 1 < 0 || slot->Height - 1 >= 9) throw VMSimanticsException("Index was outside the bounds of the array.", context);
        value = SLOT::HeightOffsets[slot->Height - 1];
    }
    else value = slot->Offset.Z;
    return (int32_t)std::nearbyint(value); // Math.Round: round half to even
}
}

VMPrimitiveExitCode VMReach::Execute(VMStackFrame* context, VMPrimitiveOperand* args)
{
    auto operand = static_cast<VMReachOperand*>(args);
    auto* originalAvatar = dynamic_cast<VMAvatar*>(context->Caller);
    if (context->VM()->TS1 && originalAvatar && !context->TS1LegacyReach &&
        !originalAvatar->TS1ExternalAnimationClock && originalAvatar->TS1AnimationController.OriginalLifecycle)
        return ExecuteOriginal(context, operand);
    if (context->VM()->TS1) context->TS1LegacyReach = true;
    auto completedPickup = false;

    int32_t height;

    if (operand->Mode == 0)
    { //reach to stack object
        // FIX(sims) QA4: the object was deleted while the Sim walked to it (another Sim ate / bussed / cleaned the same
        // meal: FoodHD 'get food' routes to the plate, then reaches): fail the reach instead of resetting the Sim. The
        // trees branch on false ('get food' -> false -> 'drop food'). FreeSO/FreeSims would throw here.
        if (context->StackObject == nullptr && context->VM()->TS1) return VMPrimitiveExitCode::GOTO_FALSE;
        if (context->StackObject == nullptr) throw VMSimanticsException("Object reference not set to an instance of an object.", context);
        auto container = context->StackObject->Container;
        if (container == context->Caller) completedPickup = true;
        if (container == nullptr || dynamic_cast<VMAvatar*>(container) != nullptr)
        {
            height = 0;
        }
        else
        {
            auto slot = SlotZeroAt(container, context->StackObject->ContainerSlot, context);
            if (slot != nullptr)
            {
                height = SlotHeight(slot, context);
            }
            else height = 0;
        }
       // height = 4; //todo: get slot height
    }
    else if (operand->Mode == 1)
    {
        if (context->StackObject == nullptr && context->VM()->TS1) return VMPrimitiveExitCode::GOTO_FALSE; // FIX(sims) QA4: see above
        if (context->StackObject == nullptr) throw VMSimanticsException("Object reference not set to an instance of an object.", context);
        auto slotNum = context->Args.at(operand->SlotParam);
        auto slot = SlotZeroAt(context->StackObject, slotNum, context);
        if (slot != nullptr)
        {
            height = SlotHeight(slot, context);
        }
        else return VMPrimitiveExitCode::GOTO_FALSE;
    }
    else
    {
        //reach to mouth is unimplemented so no, also none others exist after
        throw VMSimanticsException("Reach to mouth not implemented!", context);
    }

    std::string animationName;
    if (height < 2) animationName = "a2o-reach-floorht.anim";
    else if (height < 4) animationName = "a2o-reach-seatht.anim";
    else animationName = "a2o-reach-tableht.anim";

    auto animation = FSO::Content::Content::Get()->AvatarAnimations->Get(animationName);
    if (animation == nullptr) {
        return VMPrimitiveExitCode::ERROR;
    }
    auto avatar = dynamic_cast<VMAvatar*>(context->Caller); // C#: (VMAvatar)context.Caller
    if (avatar == nullptr) throw VMSimanticsException("Specified cast is not valid.", context);
    const bool sharedEvents = context->VM()->TS1 && !avatar->TS1ExternalAnimationClock &&
        avatar->TS1AnimationController.OriginalLifecycle;

    /** Are we starting the animation or progressing it? **/
    if (avatar->CurrentAnimationState() == nullptr || (avatar->CurrentAnimationState()->Anim != animation && !completedPickup))
    { //start the grab!

        /** Start it **/

        avatar->Animations.clear();
        if (sharedEvents) avatar->TS1AnimationController.Events.clear();
        avatar->Animations.push_back(std::make_shared<Model::VMAnimationState>(animation, false));

        avatar->Avatar->LeftHandGesture = FSO::Vitaboy::SimHandGesture::Idle;
        avatar->Avatar->RightHandGesture = FSO::Vitaboy::SimHandGesture::Idle;
        failed = false;
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    }
    else
    {
        if (avatar->CurrentAnimationState()->EndReached)
        {
            avatar->Animations.clear();
            return VMPrimitiveExitCode::GOTO_TRUE;
        }
        else if (sharedEvents ? !avatar->TS1AnimationController.Events.empty()
                             : !avatar->CurrentAnimationState()->EventQueue.empty())
        {

            const auto event = sharedEvents ? avatar->TS1AnimationController.Events.front()
                : int32_t(avatar->CurrentAnimationState()->EventQueue.front());
            if (event == 0)
            {
                //do the grab/drop
                if (operand->Mode == 0)
                { //pick up stack object. no drop condition
                    if (context->Caller->GetSlot(0) == nullptr)
                    {
                        context->Caller->PlaceInSlot(context->StackObject, 0, true, context->VM()->Context);
                    }
                    else
                    {
                        failed = true;
                    }
                }
                else if (operand->Mode == 1)
                { //grab or drop, depending on if we're holding something
                    auto holding = context->Caller->GetSlot(0);
                    auto slotNum = context->Args.at(operand->SlotParam);

                    if (holding == nullptr)
                    { //grab
                        auto item = context->StackObject->GetSlot(slotNum);
                        if (item != nullptr)
                        {
                            if (!context->Caller->PlaceInSlot(item, 0, true, context->VM()->Context)) failed = true;
                        }
                        else failed = true; //can't grab from an empty space
                    }
                    else //drop
                    {
                        auto itemTest = context->StackObject->GetSlot(slotNum);
                        if (itemTest == nullptr)
                        {
                            if (!context->StackObject->PlaceInSlot(holding, slotNum, true, context->VM()->Context)) failed = true;
                        }
                        else failed = true; //can't drop in an occupied space
                    }
                }
            }
            // C# re-evaluates avatar.CurrentAnimationState here too (the grab/drop above may have touched the animation list)
            if (avatar->CurrentAnimationState() == nullptr) throw VMSimanticsException("Object reference not set to an instance of an object.", context);
            if (sharedEvents) {
                auto& queue = avatar->TS1AnimationController.Events;
                if (queue.empty()) throw VMSimanticsException("Index was out of range.", context);
                queue.erase(queue.begin());
            } else {
                auto& queue = avatar->CurrentAnimationState()->EventQueue;
                if (queue.empty()) throw VMSimanticsException("Index was out of range.", context);
                queue.erase(queue.begin());
            }
            return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
        }
        else
        {
            return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
        }
    }
}

namespace {
struct ReachEndpoints {
    VMEntity* Moved = nullptr;
    VMEntity* Support = nullptr;
    int PhysicalSlot = -1;
    int8_t Level = 1;
    float Height = 0;
};

float ReachHeight(VMStackFrame* frame, VMEntity* support, int slot) {
    auto& ctx = *frame->VM()->Context;
    if (!support || slot < 0 || size_t(slot + 1) >= ctx.TS1SourceVectorExtent(support)) return 0.f;
    float height = 0.f;
    size_t visits = 0;
    while (support) {
        if (++visits > frame->VM()->Entities.size() + 1)
            throw VMSimanticsException("Original Reach has cyclic support ancestry.", frame);
        if (slot >= 0 && size_t(slot + 1) < ctx.TS1SourceVectorExtent(support)) {
            const auto* metadata = ctx.TS1SourceSlotMetadata(support, slot + 1);
            if (!metadata) throw VMSimanticsException("Original Reach lacks source slot metadata.", frame);
            //4340f2 stores binary32 after every ancestor addition.
            volatile float sum = height + metadata->Offset.Z;
            height = sum;
        }
        const auto placement = ctx.TS1ResolvePhysicalPlacement(support, false);
        support = frame->VM()->GetObjectById(placement.ParentID);
        slot = placement.PhysicalSlot;
    }
    //454f69 caches this binary32 reciprocal of 2*sqrt(6), followed by *3.
    //The two products have no intervening source memory store.
    return float(double(height) * double(0.204124152660369873f) * 3.0);
}

bool ResolveReach(VMStackFrame* frame, const VMReachOperand& operand, ReachEndpoints& result) {
    auto* vm = frame->VM();
    auto& ctx = *vm->Context;
    if (operand.Mode == 2) { result.Height = -1.f; return true; }
    auto* target = vm->GetObjectById(frame->StackObjectID());
    if (!target || target->Dead) return false;
    if (operand.Mode == 0) {
        result.Moved = target;
        const auto placement = ctx.TS1ResolvePhysicalPlacement(target, false);
        result.Support = vm->GetObjectById(placement.ParentID);
        result.PhysicalSlot = placement.PhysicalSlot;
    } else if (operand.Mode == 1) {
        //45c9b4: mode1 puts the item from caller physical0 onto StackObject.
        //It does not turn an empty hand into a grab from the support.
        if (!frame->Routine || operand.SlotParam >= frame->Routine->Arguments || operand.SlotParam >= frame->Args.size())
            throw VMSimanticsException("Original Reach slot parameter is outside the authored arguments.", frame);
        result.Moved = ctx.TS1SourceSlot(frame->Caller, 1);
        result.Support = target;
        result.PhysicalSlot = frame->Args[operand.SlotParam];
    } else return false;
    if (result.Support) {
        if (result.PhysicalSlot < 0 || size_t(result.PhysicalSlot + 1) >= ctx.TS1SourceVectorExtent(result.Support)) return false;
        result.Level = result.Support->Position().Level;
        result.Height = ReachHeight(frame, result.Support, result.PhysicalSlot);
    }
    return true;
}

void ReachCallback(VMStackFrame* parent, VMEntity* moved, const char* name) {
    if (!moved || !moved->Object || !moved->Object->Resource) return;
    const auto& names = moved->Object->Resource->TreeByName;
    const auto found = names.find(name);
    if (found == names.end() || !found->second.bhav) return;
    auto frame = std::make_shared<VMStackFrame>();
    frame->Caller = parent->Caller;
    frame->Callee = moved;
    frame->StackObject = moved;
    frame->StackObjectRawID = moved->ObjectID;
    frame->CodeOwner = moved->Object;
    frame->Routine = parent->VM()->Assemble(found->second.bhav);
    const auto count = size_t(frame->Routine->Arguments);
    if (count > parent->Thread->TempRegisters.size())
        throw VMSimanticsException("Original Reach callback exceeds the interpreter argument bank.", parent);
    frame->Args.assign(std::max<size_t>(4, count), 0);
    std::copy_n(parent->Thread->TempRegisters.begin(), count, frame->Args.begin());
    //452c2d ->474b22: same interpreter, NULL argument pointer fallback,
    //moved object's resource and StackOID; result does not undo the transfer.
    parent->Thread->RunTS1SynchronousCall(std::move(frame));
}
}

VMPrimitiveExitCode VMReach::ExecuteOriginal(VMStackFrame* context, VMReachOperand* operand) {
    auto& avatar = *static_cast<VMAvatar*>(context->Caller);
    auto& model = avatar.TS1AnimationController;
    auto failResolution = [&] { return operand->GrabOrDrop == 0 ? VMPrimitiveExitCode::GOTO_TRUE : VMPrimitiveExitCode::GOTO_FALSE; };
    if (context->PrimitiveState == 0) {
        ReachEndpoints endpoints;
        if (!ResolveReach(context, *operand, endpoints)) return VMPrimitiveExitCode::GOTO_FALSE;
        if (!avatar.StartTS1ReachAnimation(endpoints.Height)) return failResolution();
        context->PrimitiveState = 1;
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    }
    if (context->PrimitiveState == 1) {
        bool transfer = false;
        for (const auto event : model.Events) if (event == 0) transfer = true;
        model.Events.clear(); //45cb8e consumes the entire FIFO before one transfer.
        if (!transfer && !avatar.TS1ReachAnimationComplete()) return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
        ReachEndpoints endpoints;
        if (!ResolveReach(context, *operand, endpoints)) return failResolution();
        if (operand->GrabOrDrop == 0) {
            context->PrimitiveState = 2;
        } else if (operand->Mode == 0 || operand->Mode == 1) {
            auto* parent = operand->Mode == 0 ? static_cast<VMEntity*>(&avatar) : endpoints.Support;
            const auto slot = operand->Mode == 0 ? 0 : endpoints.PhysicalSlot;
            bool success = endpoints.Moved && parent;
            if (success) {
                auto& ctx = *context->VM()->Context;
                auto request = ctx.TS1PhysicalRequest(endpoints.Moved, parent, slot);
                request.Position.Level = operand->Mode == 0 ? avatar.Position().Level : endpoints.Level;
                success = ctx.TS1QueryContainedPlacement(endpoints.Moved, request).Status == Model::VMPlacementError::Success;
                if (success) success = parent->PlaceInSlot(endpoints.Moved, slot, true, &ctx);
            }
            if (success) ReachCallback(context, endpoints.Moved, operand->Mode == 0 ? "CT - Grab" : "CT - Put");
            //Set AFTER the synchronous callback, which can replace FG/FIFO.
            context->PrimitiveState = success ? 2 : 3;
        }
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    }
    if (context->PrimitiveState == 2 || context->PrimitiveState == 3) {
        if (!avatar.TS1ReachAnimationComplete()) return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
        const bool success = context->PrimitiveState == 2;
        avatar.EndTS1ReachAnimation();
        return success ? VMPrimitiveExitCode::GOTO_TRUE : VMPrimitiveExitCode::GOTO_FALSE;
    }
    throw VMSimanticsException("Original Reach has an invalid primitive state.", context);
}

void VMReachOperand::Read(const uint8_t* bytes)
{
    st::Reader io(bytes, 8, st::Endian::Little);
    Mode = io.u16();
    GrabOrDrop = io.u16();
    SlotParam = io.u16();
}

void VMReachOperand::Write(uint8_t* bytes)
{
    st::Writer io(st::Endian::Little);
    io.WriteUInt16(Mode);
    io.WriteUInt16(GrabOrDrop);
    io.WriteUInt16(SlotParam);
    auto& buf = io.ToArray();
    std::copy(buf.begin(), buf.begin() + std::min<size_t>(buf.size(), OPERAND_SIZE), bytes);
}

}}}
