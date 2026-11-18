// SPDX-License-Identifier: MPL-2.0
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "content/ContentManager.h"

namespace FSO { namespace SimAntics {

void VMAvatar::UpdateTS1CarryAnimation() {
    if (!TS1AnimationController.OriginalLifecycle || TS1ExternalAnimationClock || !Thread || !Thread->Context ||
        !Thread->Context->VM || !Thread->Context->VM->TS1) return;

    // DVD5903a7: a custom carry (kind6) bypasses automatic selection even
    // when another producer has cleared its animation pointer.
    auto& controller = TS1AnimationController;
    if (controller.CarryKind == 6) return;

    int32_t wanted = -1;
    if (controller.CarrySuppressed == 0) {
        // DVD589fd2 checks the first logical attachment's signed object ID.
        // Original vector record0 is the tile link; record1 maps to native slot0.
        const auto child = GetSlot(0);
        controller.CarryKind = child && child->ObjectID != 0 ? 1 : 0;
        if (controller.CarryKind > 0 && controller.CarryKind < 6) wanted = 0;
    }
    if (wanted == controller.CarryCachedIndex) return;

    CarryAnimationState.reset();
    if (wanted == 0) {
        constexpr size_t carryCode = 0x26;
        auto content = FSO::Content::Content::Get();
        if (carryCode < WalkAnimations.size() && !WalkAnimations[carryCode].empty()
            && content && content->AvatarAnimations) {
            auto clip = content->AvatarAnimations->Get(WalkAnimations[carryCode] + ".anim");
            if (clip) {
                auto state = std::make_shared<Model::VMAnimationState>(clip, false);
                state->TS1CompositionPriority = 10;
                // Generic source start: rate+1, weight1, loop0. Custom mode2
                // sets loop1 separately; this automatic branch does not.
                StartTS1Animation(state);
                CarryAnimationState = std::move(state);
            }
        }
    }
    // A missing/default-unbound clip still consumes this selection transition.
    // The original retries only after the desired index changes.
    controller.CarryCachedIndex = wanted;
}

}}
