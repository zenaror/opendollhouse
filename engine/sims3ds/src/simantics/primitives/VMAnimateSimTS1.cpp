// SPDX-License-Identifier: MPL-2.0
#include "simantics/primitives/VMAnimateSim.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMMemory.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/engine/scopes/VMVariableScope.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "content/ContentManager.h"

namespace FSO { namespace SimAntics { namespace Primitives {
using namespace Engine;
using namespace Engine::Utils;
using namespace Engine::Scopes;
using namespace Model;

VMPrimitiveExitCode VMAnimateSim::ExecuteOriginal(VMStackFrame* context, VMPrimitiveOperand* args) {
    auto& operand = *static_cast<VMAnimateSimOperand*>(args);
    auto& avatar = *static_cast<VMAvatar*>(context->Caller);
    auto& model = avatar.TS1AnimationController;
    const auto state = avatar.TS1ModelMotion.Kind == Engine::Routing::TS1MotionKind::None
        ? model.ModelState : avatar.TS1ModelMotion.ModelState;
    //58950f checks model190 before ANY mode or parameter/asset resolution.
    if (state < 0 || state > 3) return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    const uint16_t id = operand.IDFromParam() ? uint16_t(context->Args.at(operand.AnimationID)) : operand.AnimationID;
    const auto mode = operand.Mode();
    const auto setModelState = [&](int32_t value) {
        model.ModelState = value;
        if (avatar.TS1ModelMotion.Kind != Engine::Routing::TS1MotionKind::None)
            avatar.TS1ModelMotion.ModelState = value;
    };
    auto postureState = [&] {
        const auto posture = avatar.GetPersonData(VMPersonDataVariable::Posture);
        if (posture == 0) setModelState(3);
        else if (posture == 1) setModelState(1);
        else if (posture == 2) setModelState(2);
    };
    if (!id) {
        if (mode == 1) avatar.TS1BackgroundAnimationState.reset();
        else if (mode == 2) {
            avatar.CarryAnimationState.reset(); model.CarryKind = 0; model.CarryCachedIndex = -1;
        } else {
            avatar.Animations.clear(); postureState(); model.CarrySuppressed = 0;
            avatar.SetPersonData(static_cast<VMPersonDataVariable>(19), 0);
        }
        // Zero ID does not reset script admission, consumed events or FIFO.
        return VMPrimitiveExitCode::GOTO_TRUE;
    }
    auto resolve = [&] { return VMMemory::GetAnimation(context, operand.Source, id, mode); };
    auto start = [&](FSO::Vitaboy::Animation* animation, bool foreground, bool backwards) {
        if (!animation) throw VMSimanticsException("Animate Sim: animation not found.",context);
        auto clip = std::make_shared<VMAnimationState>(animation,backwards);
        clip->TS1ScriptAnimation = true;
        avatar.TS1RawAnimationClock = operand.RawTS1Clock();
        if (foreground) avatar.TS1HurryableAnimation = operand.Hurryable();
        avatar.StartTS1Animation(clip,foreground && operand.Hurryable() ? avatar.TS1MotionRate : 1.f);
        return clip;
    };
    if (mode == 1 || mode == 2) {
        auto animation = resolve();
        if (!animation) return VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK;
        auto clip = start(animation,false,operand.PlayBackwards()); clip->Loop = true;
        clip->TS1CompositionPriority = mode == 1 ? -10 : 10;
        if (mode == 1) avatar.TS1BackgroundAnimationState = std::move(clip);
        else {
            avatar.CarryAnimationState = std::move(clip);
            model.CarryKind = 6; model.CarryCachedIndex = -1;
        }
        return VMPrimitiveExitCode::GOTO_TRUE;
    }
    if (!model.ScriptActive) {
        auto animation = resolve();
        bool fallback = false;
        if (!animation) {
            fallback = true;
            auto content = FSO::Content::Content::Get();
            if (content && content->AvatarAnimations && avatar.WalkAnimations.size() > 0x25 &&
                !avatar.WalkAnimations[0x25].empty())
                animation = content->AvatarAnimations->Get(avatar.WalkAnimations[0x25]+".anim");
        }
        if (!animation) return VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK;
        model.Events.clear(); avatar.Animations.clear();
        //5898c1..5898d3 starts the fallback FORWARD even for a reverse operand.
        auto clip = start(animation,true,!fallback && operand.PlayBackwards());
        clip->TS1ScriptInterruptible = (operand.Flags & 8) != 0;
        // Replacing even the SAME resource creates a fresh instance. Motion
        // blend/interrupt history belongs to the replaced instance, not its name.
        if (avatar.TS1ModelMotion.Kind != Engine::Routing::TS1MotionKind::None) {
            avatar.TS1ModelMotion.ForegroundAnimation = animation->Name;
            avatar.TS1ModelMotion.ForegroundInterruptible = clip->TS1ScriptInterruptible;
            avatar.TS1ModelMotion.BlendActive = false;
        }
        avatar.Animations.push_back(std::move(clip));
        if (mode == 3) {
            avatar.CarryAnimationState.reset(); model.CarryCachedIndex = -1; model.CarrySuppressed = 1;
        }
        model.EventsRun = 0; setModelState(0); model.ScriptActive = 1;
    }
    const int32_t count = operand.ExpectedEventCount < 128
        ? operand.ExpectedEventCount : int32_t(operand.ExpectedEventCount)-256;
    const auto expected = [&] {
        const int64_t index = operand.PlayBackwards() ? int64_t(count)-model.EventsRun-1 : model.EventsRun;
        return int32_t(index >= 0 && index < count ? index : -1);
    };
    const auto emit = [&](int32_t event) {
        // Only the final script-variable store narrows the signed32 event.
        const auto value = int16_t(uint16_t(event));
        if (operand.StoreFrameInLocal())
            VMMemory::SetVariable(context,VMVariableScope::Local,operand.LocalEventNumber,value);
        else VMMemory::SetVariable(context,VMVariableScope::Parameters,0,value);
        return VMPrimitiveExitCode::GOTO_FALSE;
    };
    if (!model.Events.empty()) {
        const auto event = model.Events.front(); model.Events.erase(model.Events.begin());
        if (event >= 100) return emit(event);
        const auto wanted = expected();
        if (wanted >= 0 && event == wanted) { ++model.EventsRun; return emit(event); }
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    }
    auto clip = avatar.CurrentAnimationState();
    const bool ended = !clip || clip->EndReached || clip->Speed == 0;
    if (!ended && !(clip->TS1ScriptInterruptible && context->Thread->TS1ShouldNotifyIdle()))
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    if (model.EventsRun < count) {
        const auto event = expected(); ++model.EventsRun;
        return emit(event);
    }
    model.ScriptActive = 0; postureState(); avatar.UpdateTS1BackgroundAnimation();
    model.CarrySuppressed = 0; avatar.SetPersonData(static_cast<VMPersonDataVariable>(19),0);
    // Source completion retains terminal foreground and queued events.
    return VMPrimitiveExitCode::GOTO_TRUE;
}

}}}
