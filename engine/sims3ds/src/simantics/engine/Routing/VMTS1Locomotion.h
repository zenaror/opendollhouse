// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cmath>

namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {

// DVD model 00588fd4 and the pre-FSM update at 0058dc76. Model states
// are source state numbers here, not VMRoutingFrameState enum values.
inline float TS1TargetMotionRate(int walkStyle, int modelState, int species) {
    float factor = 1.0f;
    if (walkStyle == 1 && (modelState == 0 || (modelState >= 4 && modelState <= 9)))
        factor = (modelState != 0 && species > 1) ? 0.0f : 8.0f;
    return 1.2f + 0.2f * factor;
}
inline float TS1AdvanceMotionRate(float current, float target) {
    // Only increasing rates are limited; decreasing rates change immediately.
    return current < target && (current < target - 0.2f || target + 0.2f < current)
        ? current + 0.2f : target;
}

// DVD walking states 9/10 at 0058edf0. Asset Distance is in the Vitaboy
// coordinate scale; positions use sixteenths of a lot tile. The nominal
// animation tick is 42ms even when the model's clock advances by 50ms.
inline float TS1MotionStep(float distance, float durationMs, float rate) {
    if (!std::isfinite(distance) || !std::isfinite(durationMs) || !std::isfinite(rate) ||
        durationMs <= 0.0f || rate <= 0.0f || distance < 0.0f) return 0.0f;
    const float displacement = distance * (1.0f / 3.0f) * 16.0f;
    const float ticks = (durationMs / rate) / 42.0f;
    if (!std::isfinite(displacement) || !std::isfinite(ticks) || !(ticks > 0.0f)) return 0.0f;
    const float magnitude = displacement / ticks;
    return std::isfinite(magnitude) && magnitude >= 0.0f ? magnitude : 0.0f;
}

}}}}
