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
    std::vector<int32_t> Events;
    int32_t CarrySuppressed = 0;
    int32_t CarryKind = 0;
    int32_t CarryCachedIndex = -1;
};

}}}
