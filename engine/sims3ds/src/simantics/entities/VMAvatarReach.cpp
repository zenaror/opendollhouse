// SPDX-License-Identifier: MPL-2.0
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VMContext.h"
#include "content/ContentManager.h"
#include "files/formats/iff/chunks/STR.h"
#include <cmath>

namespace FSO { namespace SimAntics {

bool VMAvatar::StartTS1ReachAnimation(float height) {
    //592602 selects the Reach bank by converted cumulative support height.
    //The seated entry is the next resource ordinal, not a renamed clip.
    if (!std::isfinite(height) || !Thread || !Thread->Context ||
        !Thread->Context->Globals || !Thread->Context->Globals->Resource) return false;
    int code = height < 0.f ? 9 : height < .75f ? 1 : height < 2.f ? 3
        : height < 2.6666667461395264f ? 5 : height < 4.f ? 7 : -1;
    if (code < 0) return false;
    if (GetPersonData(Model::VMPersonDataVariable::Posture) == 1) ++code;
    const bool child = GetPersonData(Model::VMPersonDataVariable::PersonsAge) <= 17;
    auto* table = Thread->Context->Globals->Resource->Get<Files::Formats::IFF::Chunks::STR>(child ? 155 : 154);
    std::string name;
    if (!table || !table->TryGetString(code, Files::Formats::IFF::Chunks::STR::DefaultLangCode, name) || name.empty()) return false;
    auto* content = FSO::Content::Content::Get();
    auto* animation = content && content->AvatarAnimations ? content->AvatarAnimations->Get(name + ".anim") : nullptr;
    if (!animation) return false;

    auto clip = std::make_shared<Model::VMAnimationState>(animation, false);
    clip->TS1ScriptAnimation = true;
    ClearTS1MotionAnimation();
    TS1AnimationController.Events.clear();
    //589ef7 owns model1a4, not the separate AnimateSim script latch+a0.
    TS1AnimationController.CarrySuppressed = 1;
    TS1HurryableAnimation = false;
    StartTS1Animation(clip, 1.f);
    Animations.push_back(std::move(clip));
    SetTS1MotionModelState(0);
    if (TS1ModelMotion.Kind != Engine::Routing::TS1MotionKind::None)
        TS1ModelMotion.ForegroundAnimation = animation->Name;
    return true;
}

bool VMAvatar::TS1ReachAnimationComplete() const {
    const auto* clip = CurrentAnimationState();
    return !clip || clip->EndReached || clip->Speed == 0.f; //58a004 ->586f7a
}

void VMAvatar::EndTS1ReachAnimation() {
    //58a011: foreground, automatic-carry suppression and FIFO are retired;
    //the unrelated script admission/event count remains unchanged.
    ClearTS1MotionAnimation();
    TS1AnimationController.CarrySuppressed = 0;
    TS1AnimationController.Events.clear();
    const auto posture = GetPersonData(Model::VMPersonDataVariable::Posture);
    if (posture == 0) SetTS1MotionModelState(3);
    else if (posture == 1) SetTS1MotionModelState(1);
    else if (posture == 2) SetTS1MotionModelState(2);
}

}} //namespace
