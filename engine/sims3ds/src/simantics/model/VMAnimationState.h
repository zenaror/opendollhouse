/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Model.VMAnimationState
#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include "vitaboy/Animation.h"
#include "simantics/utils/TimePropertyListItemSorter.h"
namespace FSO { namespace Vitaboy { class Skeleton; class Bone; }}
namespace FSO { namespace SimAntics { namespace Model {
class VMAnimationState {
public:
    FSO::Vitaboy::Animation* Anim;
    float CurrentFrame = 0;
    std::vector<int16_t> EventQueue;
    uint8_t EventsRun = 0; //total # of xevts fired during anim. up to short if anything uses more than 255.
    bool EndReached = false;
    bool PlayingBackwards = false;
    float Speed = 1.0f;
    float Weight = 1.0f; //For animation blending. All active animations should add up to 1 but won't break if it doesn't.
    bool Loop = false;
    // TS1 uses a model counter clock and normalized asset duration. Legacy
    // native checkpoints retain their frame-driven policy until replacement.
    bool TS1Clock = false;
    bool TS1ScriptAnimation = false;
    int32_t TS1ScriptInterruptible = 0; // original instance+68; callbacks use signed atoi
    float TS1Phase = 0;
    int32_t TS1Timestamp = 0;
    int32_t TS1EventCursor = 0;
    // Original instance+14 and+58 are separate: normal script clips have
    // priority1 but do not claim/mask the bones they bind.
    int32_t TS1CompositionPriority = 1;
    bool TS1ClaimsBonePriority = false;
    std::vector<int32_t> TS1BindingEventCursors; // resource Motion index; initial0 even backwards
    int32_t TS1BindingPreviousEventTime = 0; // source instance+34, not phase-before-update
    // Derived binding cache: never serialized, rebuilt against the live skeleton.
    const FSO::Vitaboy::Skeleton* TS1BindingSkeleton = nullptr;
    const FSO::Vitaboy::Animation* TS1BindingAnimation = nullptr;
    std::vector<FSO::Vitaboy::Bone*> TS1BindingBones;
    size_t TS1ResolvedBindings = 0;

    std::vector<FSO::Vitaboy::TimePropertyListItem> TimePropertyLists;

    // Convert only at consumers. A legitimate clock-policy change can leave
    // a signed phase outside [0,1]; it must not be rewritten to fit a pose.
    int32_t TS1EventTime() const {
        const float time = Anim ? Anim->Duration * TS1Phase : 0.f;
        if (std::isnan(time)) return 0;
        return int32_t(std::clamp(double(time), double(INT32_MIN), double(INT32_MAX)));
    }
    float TS1PoseFrame() const {
        return BoundTS1PoseFrame(CurrentFrame);
    }
    float BoundTS1PoseFrame(float frame) const {
        if (!Anim || std::isnan(frame)) return 0.f;
        const float maxIndex = std::nextafter(float(INT32_MAX), 0.f);
        return std::clamp(frame, 0.f, std::max(0.f, std::min(float(Anim->NumFrames()), maxIndex)));
    }
    void SyncTS1Frame() {
        const double frame = Anim ? double(TS1Phase) * Anim->NumFrames() : 0.;
        const double bound = std::numeric_limits<float>::max();
        CurrentFrame = std::isnan(frame) ? 0.f : float(std::clamp(frame, -bound, bound));
    }

    void StartTS1Clock(int32_t timestamp, float rate) {
        TS1Clock = true;
        TS1Timestamp = timestamp;
        TS1BindingPreviousEventTime = 0;
        TS1BindingSkeleton = nullptr;
        TS1BindingAnimation = nullptr;
        TS1BindingEventCursors.assign(Anim ? Anim->Motions.size() : 0,0);
        TS1Phase = PlayingBackwards ? 1.f : 0.f;
        TS1EventCursor = PlayingBackwards ? int32_t(TimePropertyLists.size()) - 1 : 0;
        Speed = rate;
        SyncTS1Frame();
    }

    // Returns a wrap crossing; the caller traverses time properties before
    // posing bones. Endpoint equality belongs to the ending cycle.
    bool AdvanceTS1Clock(int32_t timestamp) {
        const uint32_t bits = uint32_t(timestamp) - uint32_t(TS1Timestamp);
        const int64_t delta = bits <= 0x7fffffff ? int64_t(bits) : int64_t(bits) - 0x100000000LL;
        TS1Timestamp = timestamp;
        if (!Anim || Anim->NumFrames() == 0 || Anim->Motions.empty()) return false;
        const float duration = std::abs(Speed) > 1e-6f ? std::abs(Anim->Duration / Speed) : 0.f;
        // Malformed zero-duration resources cannot define a finite phase.
        // Keep their last pose rather than making rendering decide completion.
        if (!(duration > 0.f) || !std::isfinite(duration)) return false;
        float increment = float(delta) / duration;
        if (PlayingBackwards) increment = -increment;
        const float nextPhase = TS1Phase + increment;
        // Invalid metadata must not publish NaN/Inf into persistent state.
        if (!std::isfinite(nextPhase)) return false;
        TS1Phase = nextPhase;
        const bool ended = PlayingBackwards ? TS1Phase <= 0.f : TS1Phase >= 1.f;
        bool wrapped = false;
        if (delta != 0) EndReached = ended;
        if (ended) {
            if (Loop) {
                wrapped = TS1Phase < 0.f || TS1Phase > 1.f;
                if (wrapped) TS1Phase -= std::floor(TS1Phase);
            } else TS1Phase = std::clamp(TS1Phase, 0.f, 1.f);
        }
        SyncTS1Frame();
        return wrapped;
    }

    VMAnimationState(FSO::Vitaboy::Animation* animation, bool backwards) : Anim(animation) {
        TS1BindingEventCursors.assign(Anim->Motions.size(),0);
        if (backwards) {
            PlayingBackwards = true;
            CurrentFrame = (float)Anim->NumFrames();
        }
        for (auto& motion : animation->Motions) {
            for (auto& tp : motion.TimeProperties) {
                for (auto& item : tp.Items) {
                    TimePropertyLists.push_back(item);
                }
            }
        }
        /** Sort time property lists by time **/
        std::stable_sort(TimePropertyLists.begin(), TimePropertyLists.end(), Utils::TimePropertyListItemSorter());
    }
};
}}}
