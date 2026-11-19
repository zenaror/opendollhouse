// SPDX-License-Identifier: MPL-2.0
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/Routing/VMTS1Locomotion.h"
#include "content/ContentManager.h"
#include <cmath>
#include <stdexcept>

namespace FSO { namespace SimAntics {
using namespace Model;
namespace {
constexpr float EighthTurn = 0.7853981633974483f;
float WrappedHeading(float radians) {
    while (radians >= 4.f * EighthTurn) radians -= 8.f * EighthTurn;
    while (radians < -4.f * EighthTurn) radians += 8.f * EighthTurn;
    return radians;
}
}

bool VMAvatar::UsesTS1OriginalLocomotion() const {
    return Thread && Thread->Context && Thread->Context->VM->TS1
        && TS1AnimationController.OriginalLifecycle && TS1AnimationController.OriginalLocomotion;
}

void VMAvatar::SetTS1MotionModelState(int32_t state) {
    TS1AnimationController.ModelState = state;
    if (TS1ModelMotion.Kind != Engine::Routing::TS1MotionKind::None)
        TS1ModelMotion.ModelState = state;
}

VMAnimationState* VMAvatar::SelectTS1MotionAnimation(int32_t code, bool loop, bool reverse) {
    auto& controller = TS1AnimationController;
    auto current = CurrentAnimationState();
    //59267d: the numeric selector, rather than a clip-name guess, controls reuse.
    if (current && controller.MotionCode == code) return current;
    auto& names = GetPersonData(VMPersonDataVariable::IsSwimming) ? SwimAnimations : WalkAnimations;
    if (code < 0 || size_t(code) >= names.size() || names[code].empty())
        throw std::runtime_error("Missing TS1 locomotion animation selector");
    auto content = FSO::Content::Content::Get();
    if (!content || !content->AvatarAnimations)
        throw std::runtime_error("Missing TS1 animation resource provider");
    auto animation = content->AvatarAnimations->Get(names[code] + ".anim");
    if (!animation) throw std::runtime_error("Missing TS1 locomotion animation resource");
    Animations.clear();
    auto state = std::make_shared<VMAnimationState>(animation, reverse);
    state->Loop = loop;
    StartTS1Animation(state, reverse ? -TS1MotionRate : TS1MotionRate);
    if (controller.MotionForegroundTransfer) {
        state->TS1CompositionPriority += 5;
        controller.MotionForegroundTransfer = 0;
    }
    Animations.push_back(state);
    controller.MotionCode = code;
    TS1ModelMotion.ForegroundAnimation = animation->Name;
    TS1ModelMotion.ForegroundInterruptible = 0;
    TS1ModelMotion.BlendActive = false;
    return state.get();
}

void VMAvatar::ClearTS1MotionAnimation() {
    Animations.clear();
    TS1AnimationController.MotionCode = -1; //5926cc
    TS1ModelMotion.ForegroundAnimation.clear();
    TS1ModelMotion.ForegroundInterruptible = 0;
    TS1ModelMotion.BlendActive = false;
}

void VMAvatar::BeginTS1MotionTransition(int32_t state, float endX, float endY, float endHeading,
    float savedX, float savedY, float savedHeading) {
    auto& c = TS1AnimationController;
    c.MotionStartX = TS1MotionX; c.MotionStartY = TS1MotionY;
    c.MotionStartHeading = RadianDirection();
    c.MotionEndX = endX; c.MotionEndY = endY; c.MotionEndHeading = endHeading;
    c.MotionSavedX = savedX; c.MotionSavedY = savedY; c.MotionSavedHeading = savedHeading;
    c.MotionTransitionKnown = true;
    SetTS1MotionModelState(state);
}

bool VMAvatar::AdvanceTS1MotionTransition() {
    auto& c = TS1AnimationController;
    if (!UsesTS1OriginalLocomotion()) return false;
    const int state = c.ModelState;
    if (!c.MotionTransitionKnown && state != 12) return false;
    if (state != 4 && state != 5 && state != 6 && state != 7 && state != 11 && state != 12) return false;
    auto clip = CurrentAnimationState();
    c.Moving = c.StepLatched = 0;
    if (state == 12) {
        if (clip && clip->TS1ScriptInterruptible == 0) return true;
        c.MotionTransitionKnown = false; SetTS1MotionModelState(3); return false;
    }
    float phase = clip && !clip->EndReached ? clip->TS1Phase : 1.f;
    if (state == 7 && clip && clip->Speed < 0.f) phase = 1.f - clip->TS1Phase;
    const float terminal = state == 6 || state == 11 ? 0.95f : 1.f;
    const bool ended = phase >= terminal || (state == 7 && clip && std::abs(clip->Speed) <= 1e-6f);
    if (!ended) {
        if (clip) clip->Speed = state == 7 && clip->Speed < 0.f ? -TS1MotionRate : TS1MotionRate;
        TS1MotionX = (1.f-phase)*c.MotionStartX + phase*c.MotionEndX;
        TS1MotionY = (1.f-phase)*c.MotionStartY + phase*c.MotionEndY;
        if (state == 4 || state == 5 || state == 7) {
            const float delta = state == 7 ? WrappedHeading(c.MotionEndHeading-c.MotionStartHeading)
                : c.MotionEndHeading-c.MotionStartHeading;
            RadianDirection(c.MotionStartHeading + phase*delta);
        }
        //58f223 changes model80/84 only. Integer position/spatial placement is
        //published by the subsequent state9 proposal, not each start frame.
        VisualPosition(st::Vector3(TS1MotionX/16.f, TS1MotionY/16.f, (Position().Level-1)*2.95f));
        Velocity = st::Vector3(0,0,0);
        return true;
    }
    if (state == 5 && clip) clip->Weight = 0.f;
    if (state == 4 || state == 7) c.MotionForegroundTransfer = 1;
    ClearTS1MotionAnimation();
    c.MotionTransitionKnown = false;
    if (state == 6 || state == 11) {
        TS1MotionX = c.MotionEndX; TS1MotionY = c.MotionEndY;
        c.Moving = c.StepLatched = 1;
        SetTS1MotionModelState(state == 11 ? 10 : 9);
    } else {
        TS1MotionX = c.MotionSavedX; TS1MotionY = c.MotionSavedY;
        TS1ModelMotion.TargetX = c.MotionSavedX; TS1ModelMotion.TargetY = c.MotionSavedY;
        TS1ModelMotion.TargetHeading = c.MotionSavedHeading;
        RadianDirection(c.MotionSavedHeading);
        SetTS1MotionModelState(3);
        if (state == 7) {
            const auto outcome = TS1ModelMotion.Outcome;
            if (outcome == 1) TS1ModelMotion.Status = -1;
            else if (outcome == 2) TS1ModelMotion.Status = 2;
            else if (outcome == 3) TS1ModelMotion.Status = 3;
        }
    }
    return false;
}

bool VMAvatar::TryTS1StartMotion(float dx, float dy, float headingDifference) {
    auto& c = TS1AnimationController;
    if (!UsesTS1OriginalLocomotion()) return false;
    if (c.StopLatched || c.Moving || c.MotionCode == 20 || c.MotionCode == 21) return false;
    const int style = GetValue(VMStackObjectVariable::WalkStyle);
    const int code = style == 1 ? 22 : 19;
    auto& names = GetPersonData(VMPersonDataVariable::IsSwimming) ? SwimAnimations : WalkAnimations;
    auto content = FSO::Content::Content::Get();
    if (!content || !content->AvatarAnimations)
        throw std::runtime_error("Missing TS1 animation resource provider");
    auto animation = content->AvatarAnimations->Get(names[code] + ".anim");
    const float radius = animation ? animation->Distance*(16.f/3.f) : 0.f;
    const float factor = TS1MotionRate < 1.f ? TS1MotionRate : 1.f;
    const float h = (GetPersonData(VMPersonDataVariable::Gender)>1 ? 0.057f : 0.2f)/factor;
    const bool large = headingDifference < -h || headingDifference > h;
    if (!GetPersonData(VMPersonDataVariable::IsSwimming)
        && (large || dx*dx+dy*dy <= radius*radius)) return false;
    auto clip = SelectTS1MotionAnimation(code, false);
    //58e145/58e397 explicitly install the requested rate on entering start.
    TS1MotionRate = Engine::Routing::TS1TargetMotionRate(style, style == 1 ? 11 : 6,
        GetPersonData(VMPersonDataVariable::Gender));
    clip->Speed = TS1MotionRate;
    const float distance = clip->Anim->Distance * (1.f/3.f) * 16.f;
    BeginTS1MotionTransition(style == 1 ? 11 : 6,
        TS1MotionX + std::sin(RadianDirection())*distance,
        TS1MotionY - std::cos(RadianDirection())*distance, RadianDirection(),
        TS1MotionX, TS1MotionY, RadianDirection());
    c.Moving = c.StepLatched = 0;
    return true;
}

bool VMAvatar::TryTS1ShortMotion(float targetX, float targetY, float heading) {
    if (!UsesTS1OriginalLocomotion()) return false;
    const float dx = targetX-TS1MotionX, dy = targetY-TS1MotionY;
    const float distance = std::sqrt(dx*dx+dy*dy);
    const bool pet = GetPersonData(VMPersonDataVariable::Gender)>1;
    const float minimum = pet ? 0.5f*0.2f : 0.5f;
    const float maximum = pet ? 3.1f*(1.f/12.f) : 3.1f;
    const float difference = WrappedHeading(heading-RadianDirection())/EighthTurn;
    const float threshold = (pet ? 0.0625f : 0.25f)/TS1MotionRate;
    if (distance < minimum || distance >= maximum || difference >= threshold || difference <= -threshold) return false;
    const float angle = std::atan2(dy,dx);
    const float sectors = angle/(2.f*3.1415927f)+0.0625f;
    const int sector = int(std::floor((sectors-std::floor(sectors))*8.f));
    static constexpr int Codes[8] = {15,13,11,12,14,16,18,17};
    auto clip = SelectTS1MotionAnimation(Codes[sector], false);
    float x=0.f,y=0.f;
    //588f2f reads the final ROOT translation, in the resource coordinate basis.
    if (clip->Anim->EnsureKeyframes()) for (const auto& motion : clip->Anim->Motions) {
        if (motion.BoneName != "ROOT" || !motion.HasTranslation || !motion.FrameCount) continue;
        const uint64_t index = uint64_t(motion.FirstTranslationIndex)+motion.FrameCount-1;
        if (index < clip->Anim->Translations.size()) {
            x = -clip->Anim->Translations[index].X; y = clip->Anim->Translations[index].Y;
        }
        break;
    }
    const float endX = targetX-(std::cos(angle)*x+std::sin(angle)*y);
    const float endY = targetY-(std::sin(angle)*x+std::cos(angle)*y);
    const float magnitude = std::sqrt(x*x+y*y);
    clip->Weight = distance < 0.1f ? 0.5f : distance >= 1.f ? 1.f
        : std::max(0.5f, distance/magnitude);
    BeginTS1MotionTransition(5,endX,endY,heading,targetX,targetY,heading);
    return true;
}

bool VMAvatar::TryTS1StopMotion() {
    auto& m = TS1ModelMotion; auto& c = TS1AnimationController;
    if (!UsesTS1OriginalLocomotion() || !m.StopRequested || m.Status != 1) return false;
    auto clip = CurrentAnimationState();
    if (clip && clip->TS1ScriptInterruptible == 0) return false;
    m.Outcome = 3;
    c.MotionStartX=c.MotionEndX=c.MotionSavedX=TS1MotionX;
    c.MotionStartY=c.MotionEndY=c.MotionSavedY=TS1MotionY;
    c.MotionStartHeading=c.MotionEndHeading=c.MotionSavedHeading=RadianDirection();
    if (!c.StepLatched) { m.Status=3;SetTS1MotionModelState(3);return true; }
    ClearTS1MotionAnimation(); SelectTS1MotionAnimation(19,false,true);
    BeginTS1MotionTransition(7,TS1MotionX,TS1MotionY,RadianDirection(),
        TS1MotionX,TS1MotionY,RadianDirection());
    return true;
}

int VMAvatar::PrepareTS1NoStopTerminal(float goalX, float goalY, float goalHeading) {
    auto& c = TS1AnimationController; auto& m = TS1ModelMotion;
    if (!UsesTS1OriginalLocomotion() || !c.MotionStepKnown
        || GetValue(VMStackObjectVariable::RoutePrefference) == 0) return 0;
    m.Outcome = 1;
    if (GetPersonData(VMPersonDataVariable::IsSwimming)) {
        //58e940/58ef92: this distinct producer uses the final goal only when
        //the model has a step to retire through the reversed start resource.
        if (c.StepLatched) {
            ClearTS1MotionAnimation(); SelectTS1MotionAnimation(19, false, true);
            BeginTS1MotionTransition(7, goalX, goalY, goalHeading, goalX, goalY, goalHeading);
            return 3;
        }
        FinalizeTS1NoStopTerminal();
        return 2;
    }
    m.TargetX = TS1MotionX; m.TargetY = TS1MotionY; m.TargetHeading = RadianDirection();
    m.Status = -1; m.SegmentProgress = 1.f;
    c.StopLatched = 1;
    SetTS1MotionModelState(3);
    if (CurrentAnimationState()) return 1;
    FinalizeTS1NoStopTerminal();
    return 2;
}

bool VMAvatar::AdvanceTS1NoStopTerminal() {
    auto& c = TS1AnimationController; auto& m = TS1ModelMotion;
    auto clip = CurrentAnimationState();
    if (!UsesTS1OriginalLocomotion() || !c.MotionStepKnown || !clip || m.Path.size() < 3) return false;
    //58e89b→58edf0 reuses the previous analytical direction at the current
    //foreground's resource/rate magnitude. It neither steers nor restarts FG.
    const float dx = c.MotionStepX, dy = c.MotionStepY;
    const float lengthSquared = dx*dx+dy*dy;
    const float denominator = std::abs(lengthSquared) <= 1e-6f ? 1.f : std::sqrt(lengthSquared);
    const float magnitude = Engine::Routing::TS1MotionStep(clip->Anim->Distance,clip->Anim->Duration,clip->Speed);
    const float stepX = magnitude*dx/denominator, stepY = magnitude*dy/denominator;
    const float x = TS1MotionX+stepX, y = TS1MotionY+stepY;
    if (!std::isfinite(x) || !std::isfinite(y) || x < -32768.f || x >= 32768.f
        || y < -32768.f || y >= 32768.f) return false;
    const auto& start = m.Path[m.Path.size()-3];
    const auto& end = m.Path[m.Path.size()-2];
    const auto point = Engine::Routing::TS1ConstrainToSegment(int(double(x)+0.5),int(double(y)+0.5),
        start.X,start.Y,end.X,end.Y);
    if (point.X < -32768 || point.X > 32767 || point.Y < -32768 || point.Y > 32767) return false;
    auto target = Position(); target.x=int16_t(point.X);target.y=int16_t(point.Y);
    //58ee50/5b writes precede the query, including a rejected proposal.
    c.MotionStepX=stepX;c.MotionStepY=stepY;c.MotionStepKnown=true;
    const auto heading = RadianDirection();
    const auto result = SetPosition(target,Direction(),Thread->Context);
    RadianDirection(heading);
    const bool accepted = result.Status == Model::VMPlacementError::Success
        || (m.Kind == Engine::Routing::TS1MotionKind::Route && result.Status == Model::VMPlacementError::CantBeThroughWall);
    if (!accepted) {
        if (m.Kind == Engine::Routing::TS1MotionKind::Route) {
            m.PendingCollisionStatus=int16_t(result.Status);
            m.PendingCollisionObject=result.Object ? result.Object->ObjectID : 0;
        } else { m.Status=3;m.Outcome=3;SetTS1MotionModelState(3); }
        return false;
    }
    Velocity=st::Vector3((x-TS1MotionX)/16.f,(y-TS1MotionY)/16.f,0);
    TS1MotionX=x;TS1MotionY=y;
    TS1MotionPublishedX=Position().x;TS1MotionPublishedY=Position().y;
    VisualPosition(st::Vector3(x/16.f,y/16.f,(Position().Level-1)*2.95f));
    FinalizeTS1NoStopTerminal();
    return true;
}

void VMAvatar::FinalizeTS1NoStopTerminal() {
    auto& c = TS1AnimationController; auto& m = TS1ModelMotion;
    //58f051 falls through to same-update state3 readiness. Keep the foreground,
    //its numeric code/phase and28c, including between successive portal paths.
    m.TargetX = TS1MotionX; m.TargetY = TS1MotionY; m.TargetHeading = RadianDirection();
    m.Outcome = 1; m.Status = 2; m.SegmentProgress = 1.f;
    c.Moving = c.StepLatched = 0;
    c.MotionTransitionKnown = false;
    SetTS1MotionModelState(3);
}

}} // namespace FSO::SimAntics
