// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
namespace FSO { namespace Vitaboy { class Animation; class Skeleton; }}

namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {

enum class TS1MotionKind : uint8_t { None, Route, Body };
struct TS1MotionPoint { int32_t X = 0, Y = 0; };

// Value-owned model request. Physical X/Y remain authoritative on VMAvatar;
// animation phase/clock remain authoritative on VMAnimationState. No pointers
// belong in this record: checkpoint owners bind by projected frame identity.
struct TS1MotionRequest {
    TS1MotionKind Kind = TS1MotionKind::None;
    uint64_t Token = 0, NextToken = 1;
    int32_t OwnerFrame = -1;
    int16_t OwnerCaller = 0, OwnerCallee = 0;
    uint32_t OwnerCodeGUID = 0;
    uint16_t OwnerRoutine = 0, OwnerPC = 0;
    int32_t Status = 0, ModelState = 3, Outcome = 0;
    int32_t PathCursor = -1, Stopped = 1, StopDeadline = 0;
    bool StopRequested = false, ModelUpdating = false;
    int64_t LastModelTick = -1;
    std::vector<TS1MotionPoint> Path;
    float TargetX = 0, TargetY = 0, TargetHeading = 0;
    float StartX = 0, StartY = 0, StartHeading = 0;
    float EndX = 0, EndY = 0, EndHeading = 0;
    float SegmentProgress = 0;
    float StopPhase1 = 0, StopPhase2 = 0;
    float StopDistance1 = 0, StopDistance2 = 0;
    float TurnStart = 0, TurnResidual = 0;
    bool StopProfileKnown = false;
    std::string StopProfileAnimation;
    uint64_t StopProfileSignature = 0;
    int32_t ForegroundInterruptible = 0;
    std::string ForegroundAnimation;
    int16_t PendingFailureCode = -1, PendingFailureBlocker = 0;
    int16_t PendingCollisionStatus = -1, PendingCollisionObject = 0;
    bool BlendActive = false;
    int32_t BlendStart = 0, BlendDuration = 0;
    float BlendFrom = 1.f, BlendTo = 1.f;
};

// Binds the setup's sampled bone positions to the exact animation and bind
// skeleton, including keys omitted by ordinary animation-header signatures.
uint64_t TS1StopProfileSignature(FSO::Vitaboy::Animation& animation,
    const FSO::Vitaboy::Skeleton& skeleton);

enum class TS1StopWindowStep { Continue, Created, Finalize };

// 58e8c2..58eab8: creating and consuming a window are alternate branches.
// In particular a newly created zero-displacement window cannot consume itself.
inline TS1StopWindowStep TS1StepStopWindow(TS1MotionRequest& model, float distance,
    float phase, bool foreground, bool ended, float speed, float duration,
    float motionRate, int walkStyle, bool immediateAnchor, int32_t now, float weight = 1.f) {
    if (!std::isfinite(distance) || distance < 0) return TS1StopWindowStep::Continue;
    if (model.StopDeadline == 0) {
        const bool second = phase >= model.StopPhase2 && phase < model.StopPhase1;
        float threshold = second ? model.StopDistance2 : model.StopDistance1;
        if (!immediateAnchor && walkStyle == 1) threshold += threshold;
        if (distance > threshold) return TS1StopWindowStep::Continue;
        model.Outcome = 1;
        if (immediateAnchor) return TS1StopWindowStep::Finalize;
        // Source virtual108 plus the separate time-property flag68.
        if (foreground && model.ForegroundInterruptible == 0 && !ended && speed != 0)
            return TS1StopWindowStep::Continue;
        float remaining = (second ? model.StopPhase1 : model.StopPhase2) - phase;
        if (remaining < 1.f) remaining += 1.f;
        float interval = foreground && motionRate > 0 ? remaining * (duration / motionRate) : 0.f;
        if (!std::isfinite(interval) || interval < 0 || double(interval) >= 2147483648.0)
            return TS1StopWindowStep::Continue;
        const int32_t delay = std::max<int32_t>(100, static_cast<int32_t>(interval));
        const uint32_t deadline = uint32_t(now) + uint32_t(delay);
        model.StopDeadline = deadline <= 0x7fffffffu ? int32_t(deadline)
            : int32_t(int64_t(deadline) - 0x100000000LL);
        if (foreground) {
            // 597a46 schedules pose-weight interpolation, not clock/speed
            // scaling. Source unordered0/0 also takes the minimum ratio.
            model.BlendActive = true; model.BlendStart = now; model.BlendDuration = delay;
            model.BlendFrom = weight;
            model.BlendTo = threshold > 0.f ? std::max(.2f, distance / threshold) : .2f;
        }
        return TS1StopWindowStep::Created;
    }
    if (model.StopDeadline > 0 && (walkStyle == 1 || distance < 2.f || model.StopDeadline <= now))
        model.StopDeadline = -1;
    return model.StopDeadline < 0 ? TS1StopWindowStep::Finalize : TS1StopWindowStep::Continue;
}

// 597c54 consumes foreground44/48/4c/50/54 after advancing its phase.
// Equality at the endpoint retains the active flag until the next update.
inline float TS1AdvanceStopBlend(TS1MotionRequest& model, int32_t now) {
    if (!model.BlendActive || model.BlendDuration <= 0) return model.BlendTo;
    const uint32_t bits = uint32_t(now) - uint32_t(model.BlendStart);
    const int64_t elapsed = bits <= 0x7fffffffu ? int64_t(bits) : int64_t(bits) - 0x100000000LL;
    if (elapsed > model.BlendDuration) { model.BlendActive = false; return model.BlendTo; }
    const float phase = std::clamp(float(elapsed) / float(model.BlendDuration), 0.f, 1.f);
    return (model.BlendTo - model.BlendFrom) * phase + model.BlendFrom;
}

}}}}
