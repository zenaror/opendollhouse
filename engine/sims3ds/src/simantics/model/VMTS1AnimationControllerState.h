// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <vector>

namespace FSO { namespace SimAntics { namespace Model {

// Original per-person animator state. Clip identity cannot reconstruct script
// admission, accepted events or automatic/custom carry history.
struct VMTS1AnimationControllerState {
    // Older saves lack model-wide admission/event history. Keep their adapter
    // until no authored foreground, carry or background is in flight.
    bool OriginalLifecycle = true;
    int32_t ScriptActive = 0;
    int32_t EventsRun = 0;
    int32_t ModelState = 3;
    // Model history (98/19c/28c/290) survives path request ownership.
    bool OriginalLocomotion = true;
    int32_t MotionCode = -1, Moving = 0, StopLatched = 0, StepLatched = 0;
    int32_t MotionForegroundTransfer = 0; //1a0: next foreground priority transfer
    //294/298: analytical proposal direction retained between requests. Older
    //native snapshots have no proof of these values until a real model step.
    bool MotionStepKnown = true;
    float MotionStepX = 0, MotionStepY = 0;
    bool MotionTransitionKnown = false;
    float MotionStartX = 0, MotionStartY = 0, MotionStartHeading = 0;
    float MotionEndX = 0, MotionEndY = 0, MotionEndHeading = 0;
    float MotionSavedX = 0, MotionSavedY = 0, MotionSavedHeading = 0;
    std::vector<int32_t> Events;
    int32_t CarrySuppressed = 0;
    int32_t CarryKind = 0;
    int32_t CarryCachedIndex = -1;
};

}}}
