// SPDX-License-Identifier: MPL-2.0
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMAnimationState.h"
#include "vitaboy/SimAvatar.h"
#include "vitaboy/Skeleton.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace FSO { namespace SimAntics {

bool VMAvatar::PrepareTS1AnimationBindings(Model::VMAnimationState& state) {
    auto skeleton=Avatar ? Avatar->Skeleton : nullptr;
    if (!skeleton || !state.Anim) return false;
    if (state.TS1BindingSkeleton!=skeleton || state.TS1BindingAnimation!=state.Anim ||
        state.TS1BindingBones.size()!=state.Anim->Motions.size()) {
        state.TS1BindingSkeleton=skeleton;state.TS1BindingAnimation=state.Anim;
        state.TS1BindingBones.resize(state.Anim->Motions.size());state.TS1ResolvedBindings=0;
        for(size_t i=0;i<state.Anim->Motions.size();++i) {
            auto bone=skeleton->GetBone(state.Anim->Motions[i].BoneName);
            state.TS1BindingBones[i]=bone;
            if(bone)++state.TS1ResolvedBindings;
        }
    }
    return state.TS1ResolvedBindings!=0;
}

void VMAvatar::DispatchTS1AnimationEvents(Model::VMAnimationState* state,
        int32_t previousTime,int32_t eventTime,bool allowSound) {
    if (!TS1AnimationController.OriginalLifecycle || !state || !state->TS1Clock ||
        !state->Anim || !Avatar || !Avatar->Skeleton ||
        state->Anim->NumFrames()==0 || !(state->Weight>0.f) || !PrepareTS1AnimationBindings(*state)) return;
    const auto& motions=state->Anim->Motions;
    if (state->TS1BindingEventCursors.size()!=motions.size())
        throw std::runtime_error("Original animation lacks per-binding event cursors");

    // Default +58 is false. Claim traversal is needed only for event-bearing
    // bindings; it allocates nothing and leaves ordinary idle clips cheap.
    const auto admission=[&](const std::string& boneName) {
        int32_t priority=-10000;bool claimed=false;
        const auto claim=[&](const Model::VMAnimationState* other) {
            if (!other || !other->TS1Clock || !other->Anim || !other->TS1ClaimsBonePriority) return;
            for (const auto& motion:other->Anim->Motions) if (motion.BoneName==boneName) {
                // 599214 stable ascending writes; weight does not gate claims.
                if (!claimed || other->TS1CompositionPriority>priority) priority=other->TS1CompositionPriority;
                claimed=true;break;
            }
        };
        claim(TS1BackgroundAnimationState.get());
        for (const auto& animation:Animations) claim(animation.get());
        claim(CarryAnimationState.get());
        return priority;
    };
    const double duration=std::trunc(double(state->Anim->Duration));
    // 597a90 appends bound motions in resource order, skipping unresolved bones.
    // This is not a globally time-sorted event stream across those bones.
    for (size_t binding=0;binding<motions.size();++binding) {
        const auto& motion=motions[binding];
        size_t total=0;
        for (const auto& group:motion.TimeProperties) total+=group.Items.size();
        if (!total) continue;
        if (!state->TS1BindingBones[binding] || admission(motion.BoneName)>state->TS1CompositionPriority) continue;
        if (total>size_t(INT32_MAX))
            throw std::runtime_error("Original bone event table exceeds signed cursor range");
        auto& cursor=state->TS1BindingEventCursors[binding];
        const auto count=int32_t(total);
        if (cursor < -1 || cursor > count)
            throw std::runtime_error("Original bone event cursor is outside its table");
        // Preserve group/item order without allocating a flattened pointer table.
        const auto property=[&](int32_t index)->FSO::Vitaboy::TimePropertyListItem& {
            size_t offset=size_t(index);
            for (auto& group:state->Anim->Motions[binding].TimeProperties) {
                if (offset<group.Items.size()) return group.Items[offset];
                offset-=group.Items.size();
            }
            throw std::runtime_error("Original bone event cursor lookup failed");
        };
        const auto validCursor=[&]{return cursor>=0 && cursor<count;};
        const auto signedTime=[](uint32_t value) {
            return value<=uint32_t(INT32_MAX)?int32_t(value):int32_t(int64_t(value)-0x100000000LL);
        };
        const auto validTime=[&](int32_t time){return time>=0 && double(time)<=duration;};
        const auto dispatch=[&] {
            auto& item=property(cursor);
            if (!validTime(signedTime(item.ID))) return;
            // The source callback requires a nonempty property-pair vector.
            for (const auto& list:item.Properties.Items) if (!list.KeyPairs.empty()) {
                HandleTimePropsEvent(item,allowSound,state);break;
            }
        };
        if (!state->PlayingBackwards || state->Speed==0) {
            if (eventTime<previousTime) {
                while (validCursor()) {dispatch();++cursor;}
                cursor=0; // 5947da
            }
            while (validCursor()) {
                const auto time=signedTime(property(cursor).ID);
                if (validTime(time) && time>eventTime) break;
                dispatch();++cursor;
            }
        } else {
            if (previousTime<eventTime) {
                // 598111 decrements BEFORE the reverse-wrap tail callback.
                // Initial cursors are0 even for a backwards-created instance.
                while (cursor>0) {--cursor;dispatch();}
                cursor=count-1; // 5947e3; the exhausted index is not serialized here.
            }
            while (validCursor()) {
                const auto time=signedTime(property(cursor).ID);
                if (validTime(time) && time<eventTime) break;
                dispatch();--cursor;
            }
        }
    }
}

}} // namespace FSO::SimAntics
