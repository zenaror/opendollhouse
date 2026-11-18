/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims — Create-A-Sim live preview (see CASSimPreview.h). TS1(sims).
#include "app/cas/CASSimPreview.h"
#ifndef __3DS__
#include "app/ui/OriginalPCUI.h"
#endif
#include "app/cas/CASFamilyWriter.h"
#include "vitaboy/SimAvatar.h"
#include "vitaboy/Outfit.h"
#include "vitaboy/Animator.h"
#include "vitaboy/Skeleton.h"
#include "vitaboy/content/TS1AvatarProvider.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMAnimateSim.h"
#include "simantics/model/VMAnimationState.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/model/VMMotive.h"
#include "simantics/model/VMTS1Appearance.h"
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace simapp { namespace cas {

using namespace FSO::Vitaboy;
using FSO::Content::TS1::TS1AvatarContent;

// Standing idles of the TS1 Sims (Animation.far): a breathing loop with the arms down, and the look-off /
// fidget variations that start and end in the same pose, so they chain without a pop.
static const char* ADULT_BASE = "a2o-idle-armsdown-breathe";
static const char* ADULT_VAR[] = { "a2o-idle-armsdown-lookoff", "a2o-idle-armsdown-fidget" };
static const char* CHILD_BASE = "c2o-idle-armsdown-breathe";
static const char* CHILD_VAR[] = { "c2o-idle-armsdown-lookoff", "c2o-idle-armsdown-fidget" };

struct CASSimPreview::OriginalIdle {
    std::unique_ptr<FSO::SimAntics::VMContext> Context;
    std::unique_ptr<FSO::SimAntics::VM> Machine;
    FSO::SimAntics::VMAvatar* Actor = nullptr; // owned by Machine
    Animation* Base = nullptr;
    // DVD CAS schedules this program every 42ms. Renderer time is the same
    // counter times 50ms (or 42ms for Animate Sim operand flag 0x80).
    double Accum = 0;
    double DrawingTime = 0;
    bool RawAnimationClock = false;
    // The observed DVD default disables smoothing (global 0x6f6b80 == 0).
    // Retain the authored 200ms policy for an explicitly enabled consumer.
    bool SmoothTransitions = false;
    bool Child = false;
    struct Layer {
        std::shared_ptr<FSO::SimAntics::Model::VMAnimationState> State;
        double Started = 0;
        float AuthoredSpeed = 1;
        bool IsBase = false;
        float Frame(double now) const {
            auto animation = State->Anim;
            if (!animation || animation->Duration <= 0) return 0;
            double phase = std::max(0.0, (now - Started) * AuthoredSpeed / animation->Duration);
            if (State->Loop) phase = std::fmod(phase, 1.0);
            else phase = std::min(1.0, phase);
            return float((State->PlayingBackwards ? 1.0 - phase : phase) * animation->NumFrames());
        }
    };
    std::vector<Layer> Active;
    std::unique_ptr<Layer> Fading;
    double FadeStarted = 0;
    static constexpr double FadeMilliseconds = 200;

    uint64_t Ticks = 0;

    OriginalIdle(const CASSimDesc& sim, Skeleton* skeleton) {
        using namespace FSO::SimAntics;
        auto content = FSO::Content::Content::Get();
        auto object = content && content->WorldObjects ? content->WorldObjects->Get(uint32_t(0x7FD96B54), true) : nullptr;
        if (!object || !object->Resource || !object->OBJ) throw std::runtime_error("CAS person template unavailable");
        Context.reset(new VMContext(nullptr, nullptr, false)); // no architecture, house scheduler or platform state mutation
        // No preview sound is dispatched through the global HIT service.
        Context->Primitives[23].reset(); // play_sound; absent handler takes the normal true branch
        Machine.reset(new VM(Context.get(), nullptr));
        Machine->Init();
        Machine->FreeWillEnabled = false;
        Actor = static_cast<VMAvatar*>(Machine->OwnEntity(new VMAvatar(object)));
        Actor->TS1ExternalAnimationClock = true;
        Actor->MultitileGroup = Machine->OwnGroup(new Entities::VMMultitileGroup());
        Actor->MultitileGroup->AddObject(Actor);
        Machine->AddEntity(Actor);
        Actor->PrepareForRestore(Context.get()); // resource/thread only; no authored household Init/Main
        Actor->Avatar.reset(new SimAvatar(skeleton)); // animation carrier; preview owns its own dressed avatar
        Child = sim.Age == CASAge::Child;
        Actor->TS1Appearance.AgeType = Child ? "child" : "adult";
        const auto person = sim.PersonData(0);
        for (size_t i=0; i<person.size(); ++i) Actor->SetPersonData((Model::VMPersonDataVariable)i, person[i]);
        for (int i=0; i<16; ++i) Actor->SetMotiveData((Model::VMMotive)i, 75);
        Actor->InitBodyData(Context.get()); // global, child nonempty, object-specific nonempty overrides
        Base = content->AvatarAnimations->Get(Actor->WalkAnimations[3] + ".anim");
        if (!Base) throw std::runtime_error("CAS standing base unavailable");
        Machine->SetGlobalValue(3, Actor->ObjectID);
        // The original enters the named CAS program, not a guessed adult/
        // child idle entrypoint. Its authored initialization and loop remain
        // in the existing interpreter and winning local/semiglobal resources.
        Machine->OnTraceInstruction = [this](Engine::VMStackFrame* frame, uint16_t opcode, int) {
            if (opcode == 44) {
                const auto operand = frame->GetCurrentOperand<Primitives::VMAnimateSimOperand>();
                RawAnimationClock = (operand->Flags & 0x80) != 0;
                DrawingTime = Ticks * (RawAnimationClock ? 42.0 : 50.0);
                SyncLayers();
            }
            if (Machine->Entities.size()!=1 || Actor->Dead || Actor->Thread->Stack.size()>64 ||
                Actor->Thread->Queue.size()>1 || Actor->Animations.size()>8)
                throw std::runtime_error("CAS idle resource budget exceeded");
        };
    }
    void Inputs(const CASSimDesc& sim) {
        using FSO::SimAntics::Model::VMPersonDataVariable;
        Actor->SetPersonData(VMPersonDataVariable::Gender, sim.Gender == CASGender::Female ? 1 : 0);
        for (int i=0;i<TraitCount;++i) Actor->SetPersonData((VMPersonDataVariable)TraitPersonDataIndex(i), int16_t(sim.Traits[i]*100));
    }
    void SyncLayers() {
        using FSO::SimAntics::Model::VMAnimationState;
        // The original active slot retains only its immediately previous
        // layer. Stop increments that layer's priority: base, new, then old.
        const auto present = [&](const std::shared_ptr<VMAnimationState>& state) {
            return std::find(Actor->Animations.begin(), Actor->Animations.end(), state) != Actor->Animations.end();
        };
        for (auto it = Active.rbegin(); it != Active.rend(); ++it) if (!present(it->State)) {
            Fading.reset();
            if (SmoothTransitions && !it->IsBase) { Fading.reset(new Layer(*it)); FadeStarted = DrawingTime; }
            break;
        }
        std::vector<Layer> next;
        for (const auto& state : Actor->Animations) {
            auto old = std::find_if(Active.begin(), Active.end(), [&](const Layer& layer) { return layer.State == state; });
            if (old != Active.end()) next.push_back(*old);
            else {
                Layer layer;
                layer.State = state; layer.Started = DrawingTime;
                // VMAnimateSim's TS1 factor belongs to its ordinary tick rate.
                // The preview instead samples the original renderer clock.
                layer.AuthoredSpeed = state->Speed / (30.0f / 25.0f);
                layer.IsBase = state->Loop && state->Anim == Base;
                next.push_back(layer);
            }
        }
        Active.swap(next);
    }
    void Reset() {
        Actor->Thread->Stack.clear(); Actor->Thread->Queue.clear(); Actor->Animations.clear();
        Actor->SetAttribute(5,0); Actor->CarryAnimationState.reset();
        Active.clear(); Fading.reset(); Accum = 0; DrawingTime = 0; Ticks = 0; RawAnimationClock = false;
    }
    void Update(double ms) {
        using namespace FSO::SimAntics;
        Accum += std::min(250.0,std::max(0.0,ms));
        constexpr double step = 42.0; // DVD design-character scheduler, independent of draw rate
        while (Accum >= step) {
            Accum -= step;
            ++Ticks;
            DrawingTime = Ticks * (RawAnimationClock ? 42.0 : 50.0);
            auto thread = Actor->Thread.get();
            if (thread->Stack.empty()) {
                if (!Actor->TreeByName) throw std::runtime_error("CAS named-program table unavailable");
                auto named = Actor->TreeByName->find("design-a-person");
                if (named == Actor->TreeByName->end()) throw std::runtime_error("CAS design-a-person unavailable");
                auto frame = std::make_shared<Engine::VMStackFrame>();
                frame->Caller = frame->Callee = frame->StackObject = Actor;
                frame->CodeOwner = Actor->Object;
                frame->Routine = Machine->Assemble(named->second.bhav);
                frame->Args.assign(std::max<size_t>(4,frame->Routine->Arguments),0);
                if (!thread->Push(frame)) throw std::runtime_error("CAS authored program is empty");
            }
            thread->TicksThisFrame = 0;
            thread->Tick(2000,false); // private budget; never run a gameplay reset
            if (Machine->Entities.size()!=1 || Actor->Dead) throw std::runtime_error("CAS idle left its preview actor");
            SyncLayers();
            // Keep the interpreter's notification/end service, but feed its
            // current pose from the same duration-based clock as the renderer.
            // A newly started state samples frame 0 on this tick.
            std::vector<float> speeds;
            for (auto& layer : Active) {
                auto& state = layer.State;
                speeds.push_back(state->Speed);
                const float target = layer.Frame(DrawingTime);
                state->Speed = state->PlayingBackwards ? state->CurrentFrame - target : target - state->CurrentFrame;
            }
            Actor->AdvanceAnimations(false);
            for (size_t i=0; i<Active.size(); ++i) Active[i].State->Speed = speeds[i];
            if (Fading && DrawingTime - FadeStarted >= FadeMilliseconds) Fading.reset();
            Context->Clock->Tick();
        }
    }
    void Pose(SimAvatar* avatar) const {
        const auto render = [&](const Layer& layer, float weight) {
            if (!layer.State->Anim || weight == 0 || layer.IsBase) return;
            const float frame = layer.Frame(DrawingTime);
            Animator::RenderFrameTS1(avatar,layer.State->Anim,int(frame),std::fmod(frame,1.0f),weight,layer.State->Loop);
        };
        // Weights are absolute per bone. Normalizing the first layer to 1
        // discards the original 200ms outgoing envelope and creates a snap.
        for (const auto& layer : Active) render(layer,layer.State->Weight);
        if (Fading) {
            const float remaining = float(std::clamp(1.0-(DrawingTime-FadeStarted)/FadeMilliseconds,0.0,1.0));
            render(*Fading,Fading->State->Weight*remaining);
        }
        avatar->LeftHandGesture=Actor->Avatar->LeftHandGesture.Value();
        avatar->RightHandGesture=Actor->Avatar->RightHandGesture.Value();
    }
};

CASSimPreview::CASSimPreview() : _Rng(12345) {}
CASSimPreview::~CASSimPreview() {}

void CASSimPreview::Set(const CASSimDesc& sim, bool* headOk, bool* bodyOk) {
    if (headOk) *headOk = true;
    if (bodyOk) *bodyOk = true;
    auto strings = sim.BodyStrings();
    std::string headKey = strings[2];
    std::string bodyKey = strings[1];
    for (int i = 17; i <= 22; i++) bodyKey += "|" + strings[(size_t)i];
    std::string skelName = sim.Age == CASAge::Child ? "child.skel" : "adult.skel";
    auto content = TS1AvatarContent::Get();
    if (!content->EnsureInit()) { _Avatar.reset(); return; }

    bool rebuilt = false;
    if (!_Avatar || skelName != _SkelName) {
        auto skel = content->BCF.GetSkeleton(skelName);
        if (!skel) { _Avatar.reset(); return; }
        _Avatar.reset(); // outfits are referenced by the avatar: drop it first
        _Avatar.reset(new SimAvatar(skel));
        _SkelName = skelName;
        _Child = sim.Age == CASAge::Child;
        _HeadKey.clear(); _BodyKey.clear();
        rebuilt = true;
        _Anim = nullptr;
    }
    if (headKey != _HeadKey) {
        std::unique_ptr<Outfit> o(new Outfit());
        o->ReadHead(headKey);
        _Avatar->Head = o.get();
        _Head = std::move(o);
        _HeadKey = headKey;
        rebuilt = true;
    }
    if (bodyKey != _BodyKey) {
        std::string hands[6];
        for (int i = 0; i < 6; i++) hands[i] = strings[(size_t)(17 + i)];
        std::unique_ptr<Outfit> o(new Outfit());
        o->Read(strings[1], hands);
        _Avatar->Body = o.get();
        _Avatar->Handgroup = o.get();
        _Body = std::move(o);
        _BodyKey = bodyKey;
        rebuilt = true;
    }
    // which parts bound? (a body binding is anything that is not the head; heads bind to HEAD)
    bool haveHead = false, haveBody = false;
    for (auto& b : _Avatar->Bindings) {
        if (!b || !b->Mesh) continue;
        std::string n = b->Mesh->SkinName;
        for (auto& ch : n) ch = (char)tolower((unsigned char)ch);
        if (n.find("head") != std::string::npos) haveHead = true; else if (n.find("hand") == std::string::npos) haveBody = true;
    }
    if (headOk) *headOk = haveHead;
    if (bodyOk) *bodyOk = haveBody;
    _Key = headKey + "#" + bodyKey + "#" + skelName;
    if (_OriginalBodyPreview) {
        try {
            if (!_OriginalIdle || _OriginalIdle->Child != _Child) {
                _OriginalIdle.reset(new OriginalIdle(sim,_Avatar->BaseSkeleton));
                _Anim=_OriginalIdle->Base; _AnimMs=0; _OriginalIdleError.clear();
            }
            _OriginalIdle->Inputs(sim);
        } catch (const std::exception& error) {
            _OriginalIdleError=error.what(); _OriginalIdle.reset();
            std::fprintf(stderr,"[cas] original idle unavailable: %s\n",_OriginalIdleError.c_str());
        }
    }
    if (rebuilt) {
        if (!_Anim) ResetIdle();
        Measure();
        Pose();
    }
}

void CASSimPreview::SetOriginalBodyPreview(bool enabled) {
    if (_OriginalBodyPreview == enabled) return;
    _OriginalBodyPreview = enabled;
    _OriginalIdle.reset(); _OriginalIdleError.clear();
    if (_Avatar) { ResetIdle(); Pose(); }
}

void CASSimPreview::SetIdleInputs(const CASSimDesc& sim) {
    if (_OriginalIdle) _OriginalIdle->Inputs(sim);
}

void CASSimPreview::ResetIdle() {
    if (_OriginalIdle) _OriginalIdle->Reset();
    _Loops = 0;
    PickIdle(true);
}

void CASSimPreview::PickIdle(bool base) {
    auto content = TS1AvatarContent::Get();
    const char* name = _OriginalBodyPreview
        ? (_Child ? "c2o-standing-loop" : "a2o-standing-loop")
        : (_Child ? CHILD_BASE : ADULT_BASE);
    if (!_OriginalBodyPreview && !base) {
        int n = (int)(sizeof(ADULT_VAR) / sizeof(ADULT_VAR[0]));
        name = (_Child ? CHILD_VAR : ADULT_VAR)[_Rng() % (unsigned)n];
    }
    Animation* a = _OriginalBodyPreview && _OriginalIdle ? _OriginalIdle->Base
        : content->BCF.GetAnimation(std::string(name) + ".anim");
    if (!a) a = content->BCF.GetAnimation(_Child ? "c2o-standing-loop.anim" : "a2o-standing-loop.anim");
    _Anim = a;
    _AnimMs = 0;
}

void CASSimPreview::Pose() {
    if (!_Avatar) return;
    _Avatar->ResetPose();
    if (_Anim && _Anim->NumFrames() > 0) {
        float fps = _Anim->Duration > 0 ? (float)_Anim->NumFrames() * 1000.0f / _Anim->Duration : 30.0f;
        float f = (float)(_AnimMs * fps / 1000.0);
        int fi = (int)f;
        if (fi >= _Anim->NumFrames()) fi = _Anim->NumFrames() - 1;
        if (_OriginalBodyPreview) Animator::RenderFrameTS1(_Avatar.get(), _Anim, fi, f - (float)(int)f, 1.0f, true);
        else Animator::RenderFrame(_Avatar.get(), _Anim, fi, f - (float)(int)f, 1.0f);
    }
    if (_OriginalIdle) _OriginalIdle->Pose(_Avatar.get());
    _Avatar->ReloadSkeleton();
}

void CASSimPreview::Update(double ms) {
    if (!_Avatar) return;
    if (!_Anim) PickIdle(true);
    if (_OriginalIdle) {
        try { _OriginalIdle->Update(ms); }
        catch (const std::exception& error) {
            _OriginalIdleError=error.what(); _OriginalIdle.reset();
            std::fprintf(stderr,"[cas] original idle stopped: %s\n",_OriginalIdleError.c_str());
        }
    }
    if (_OriginalIdle) _AnimMs = _OriginalIdle->DrawingTime;
    else _AnimMs += ms;
    if (_OriginalBodyPreview && _Anim && _Anim->Duration > 0) {
        _AnimMs = std::fmod(_AnimMs, (double)_Anim->Duration);
    } else if (_Anim && _Anim->Duration > 0 && _AnimMs >= _Anim->Duration) {
        _Loops++;
        // breathe 2-4 times, then a look-off/fidget, then back to breathing
        bool wasBase = _Anim->Name.find("breathe") != std::string::npos;
        if (wasBase && _Loops >= 2 + (int)(_Rng() % 3)) { _Loops = 0; PickIdle(false); }
        else if (!wasBase) PickIdle(true);
        else _AnimMs = std::fmod(_AnimMs, (double)_Anim->Duration);
    }
    Pose();
}

void CASSimPreview::Measure() {
    if (!_Avatar || !_Avatar->Skeleton) return;
    _Avatar->ResetPose();
    _Avatar->ReloadSkeleton();
    float top = 0;
    for (auto& b : _Avatar->Skeleton->Bones) top = std::max(top, b->AbsolutePosition.Y);
    auto head = _Avatar->Skeleton->GetBone("HEAD");
    if (head) _HeadY = head->AbsolutePosition.Y;
    // the HEAD bone sits at the base of the skull; the crown is about 0.6 units higher for adults
    _HeadSize = _Child ? 0.75f : 0.95f;
    _Height = std::max(top, _HeadY + _HeadSize);
    if (_Height <= 0.5f) _Height = 5.4f;
}

// ---- tilt: the vitaboy camera is the game's 30-degree iso view; CAS looks at the Sim nearly level, so the whole
// skeleton is pitched towards the camera about the horizontal axis perpendicular to the view (around the feet).
static Quaternion QMul(const Quaternion& a, const Quaternion& b) {
    return Quaternion(a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
                      a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
                      a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
                      a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z);
}
static Quaternion QAxis(const Vector3& axis, float angle) {
    float s = std::sin(angle / 2);
    return Quaternion(axis.X * s, axis.Y * s, axis.Z * s, std::cos(angle / 2));
}
static float MDiff(const Matrix& a, const Matrix& b) {
    const float* x = &a.M11; const float* y = &b.M11;
    float d = 0;
    for (int i = 0; i < 16; i++) d += std::fabs(x[i] - y[i]);
    return d;
}

bool CASSimPreview::ApplyTilt(float tilt, float direction) {
    if (!_Avatar || !_Avatar->Skeleton || !_Avatar->Skeleton->RootBone || tilt == 0) return false;
    Bone* root = _Avatar->Skeleton->RootBone;
    _SavedRot = root->Rotation; _SavedTrans = root->Translation;
    // world horizontal axis perpendicular to the TopLeft camera's view direction, into avatar-local space
    Matrix toWorld = Matrix::CreateRotationY((float)(M_PI - direction));
    Vector3 w(0.70710678f * _TiltSign, 0, -0.70710678f * _TiltSign);
    Vector3 axis(w.X * toWorld.M11 + w.Y * toWorld.M12 + w.Z * toWorld.M13,
                 w.X * toWorld.M21 + w.Y * toWorld.M22 + w.Z * toWorld.M23,
                 w.X * toWorld.M31 + w.Y * toWorld.M32 + w.Z * toWorld.M33); // * transpose = inverse rotation
    Quaternion qt = QAxis(axis, tilt);
    Matrix tiltM = Matrix::CreateFromQuaternion(qt);
    _TiltM = tiltM;
    Matrix target = Matrix::CreateFromQuaternion(root->Rotation) * tiltM;
    Quaternion a = QMul(root->Rotation, qt), b = QMul(qt, root->Rotation);
    root->Rotation = MDiff(Matrix::CreateFromQuaternion(a), target) < MDiff(Matrix::CreateFromQuaternion(b), target) ? a : b;
    root->Translation = Transform(root->Translation, tiltM);
    _Avatar->ReloadSkeleton();
    return true;
}

void CASSimPreview::RestoreTilt() {
    if (!_Avatar || !_Avatar->Skeleton || !_Avatar->Skeleton->RootBone) return;
    _Avatar->Skeleton->RootBone->Rotation = _SavedRot;
    _Avatar->Skeleton->RootBone->Translation = _SavedTrans;
    _Avatar->ReloadSkeleton();
}

AvatarDrawResult CASSimPreview::DrawAnchored(Canvas& c, const Rect& clip, const st::Vector3& local, float sx, float sy,
                                             float pxPerUnit, float direction, bool bilinear) {
    AvatarDrawResult r;
    if (!_Avatar || c.Width <= 0) return r;
    if (_Z.size() < (size_t)c.Width * c.Height) _Z.assign((size_t)c.Width * c.Height, 1.0f);
    Rect cl(std::max(0, clip.X), std::max(0, clip.Y), 0, 0);
    cl.W = std::min(c.Width, clip.X + clip.W) - cl.X;
    cl.H = std::min(c.Height, clip.Y + clip.H) - cl.Y;
    if (cl.W <= 0 || cl.H <= 0) return r;
    for (int y = cl.Y; y < cl.Y + cl.H; y++) std::fill(&_Z[(size_t)y * c.Width + cl.X], &_Z[(size_t)y * c.Width + cl.X + cl.W], 1.0f);

    WorldCamera cam;
    cam.Zoom = WorldZoom::Near;
    // Near zoom: one world unit along a horizontal axis is 128/(3*sqrt2) px at PreciseZoom 1
    cam.PreciseZoom = pxPerUnit / (128.0f / (3.0f * 1.41421356f));
    Vector3 pos(10.5f, 10.5f, 0.0f);
    cam.CenterTile = Vector2(pos.X, pos.Y);
    cam.ViewDimensions = Vector2((float)c.Width, (float)c.Height);
    Matrix world = Matrix::CreateRotationY((float)(M_PI - direction)) * Matrix::CreateTranslation(WorldCamera::GetWorldFromTile(pos));
    if (_TiltSign == 0) {
        // which way is "towards the camera"? the tilted head must rise on screen
        auto head = _Avatar->Skeleton ? _Avatar->Skeleton->GetBone("HEAD") : nullptr;
        _TiltSign = 1;
        if (head) {
            float y0 = cam.ProjectWorld(Transform(head->AbsolutePosition, world)).Y;
            ApplyTilt(0.4f, direction);
            float y1 = cam.ProjectWorld(Transform(head->AbsolutePosition, world)).Y;
            RestoreTilt();
            if (y1 > y0) _TiltSign = -1;
        }
    }
    bool tilted = ApplyTilt(_TiltAngle, direction);
    // where does the (tilted) anchor land without offset? (same world transform as AvatarComponent.Draw)
    Vector3 anchor = tilted ? Transform(local, _TiltM) : local;
    Vector3 sp = cam.ProjectWorld(Transform(anchor, world));
    cam.PixelOffset = Vector2(sx - sp.X, sy - sp.Y);

    Framebuffer fb(c.Width, c.Height, (uint8_t*)c.Px.data());
    ZBuffer zb(c.Width, c.Height, _Z.data());
    AvatarDrawOptions o;
    o.Bilinear = bilinear;
    o.Clip = st::Rectangle(cl.X, cl.Y, cl.W, cl.H);
    r = DrawAvatar(fb, zb, cam, *_Avatar, pos, direction, o);
    if (tilted) RestoreTilt();
    return r;
}

// The renderer's convention: RadianDirection 0 faces away from the TopLeft camera; the CAS "front" is PI/4 turned
// so the Sim looks straight at the viewer.
static const float FRONT = (float)(M_PI * 0.75);

AvatarDrawResult CASSimPreview::Draw(Canvas& c, const Rect& clip, float footX, float footY, float heightPx, float direction, bool bilinear) {
    // vertical world units appear cos(30deg) shorter in the 30-degree camera
    float pxPerUnit = heightPx / (_Height * std::cos(0.5236f - _TiltAngle));
    return DrawAnchored(c, clip, st::Vector3(0, 0, 0), footX, footY, pxPerUnit, FRONT + direction, bilinear);
}

AvatarDrawResult CASSimPreview::DrawOriginalBody(Canvas& c, const Rect& clip, float turn, bool bilinear) {
    // The original's inverse screen matrix gives 32/(3*sqrt(2)) px/unit.
    // Its preview applies model scale 5 to the same authored skeleton units.
    // Keep the existing body-facing convention: changing it from only the
    // model's X reflection ignores the bone quaternion conversion.
    const float oldTilt = _TiltAngle;
    _TiltAngle = 0;
    struct Restore { float& Target; float Value; ~Restore() { Target = Value; } } restore{_TiltAngle, oldTilt};
    const float pxPerUnit = 5.0f * 32.0f / (3.0f * std::sqrt(2.0f));
    return DrawAnchored(c, clip, st::Vector3(0, 0, 0), clip.X + clip.W / 2.0f,
        float(clip.Bottom() - 20), pxPerUnit, FRONT + turn, bilinear);
}

AvatarDrawResult CASSimPreview::DrawHead(Canvas& c, const Rect& clip, float cx, float cy, float headPx, float direction, bool bilinear) {
    float pxPerUnit = headPx / (_HeadSize * 1.25f);
    // HEAD bone is at the neck; the face centre is ~0.45 of the head size above it
    return DrawAnchored(c, clip, st::Vector3(0, _HeadY + _HeadSize * 0.30f, 0), cx, cy, pxPerUnit, FRONT + direction, bilinear);
}

void CASSimPreview::KeepHeadOnly() {
    if(_Avatar) _Avatar->StripAllButHead();
}
void CASSimPreview::DrawPieHead(Canvas& canvas,int cx,int cy,float yaw,float pitch) {
    // Same 200px, PI/3 field-of-view, zoom5.12, distance13.5 as the
    // previously validated original_pie_portrait_camera UI profile.
    const float scale=100.f/std::tan(float(M_PI)/6.f)*5.12f/13.5f;
    const float savedTilt=_TiltAngle;
    _TiltAngle=float(M_PI)/6.f+pitch;
    DrawAnchored(canvas,Rect(cx-100,cy-100,200,200),st::Vector3(0,_HeadY,0),
                 float(cx),float(cy),scale,FRONT+yaw,true);
    _TiltAngle=savedTilt;
}

void CASSimPreview::RenderPortraits(CASPortraits& out, bool frontFacing) {
    if (!_Avatar) return;
    // neutral pose
    auto savedAnim = _Anim; double savedMs = _AnimMs;
    _Anim = nullptr;
    _Avatar->ResetPose();
    _Avatar->ReloadSkeleton();

    float savedTilt = _TiltAngle;
    _TiltAngle = 0.52f; // 30 degrees: a level, frontal portrait like the shipped ones
    auto fill = [](Canvas& cv, int w, int h, uint32_t col) { cv.Resize(w, h); cv.Clear(col); };
    // faces: three 35x41 views (turned right, front, turned left) on dark blue (0,0,80)
    fill(out.Faces, 105, 41, RGBA(0, 0, 80));
    const float views[3] = { -0.55f, 0.0f, 0.55f };
    for (int i = 0; i < 3; i++) DrawHead(out.Faces, Rect(i * 35, 0, 35, 41), i * 35 + 17.5f, 22.0f, 38.0f, frontFacing ? 0.0f : views[i], true);
    // relationship images: the face on the five relationship colours (enemy .. best friend)
    fill(out.RelImages, 200, 40, RGBA(0, 0, 0));
    const uint32_t relCol[5] = { RGBA(208, 0, 0), RGBA(136, 44, 40), RGBA(64, 92, 88), RGBA(32, 144, 72), RGBA(0, 200, 56) };
    for (int i = 0; i < 5; i++) {
        out.RelImages.FillRect(i * 40, 0, 40, 40, relCol[i]);
        DrawHead(out.RelImages, Rect(i * 40, 0, 40, 40), i * 40 + 20.0f, 22.0f, 40.0f, 0.0f, true);
    }
    fill(out.Thumbnail, 25, 25, RGBA(0, 0, 248));
    DrawHead(out.Thumbnail, Rect(0, 0, 25, 25), 12.5f, 13.5f, 24.0f, 0.0f, true);
    fill(out.SpeechMed, 16, 16, RGBA(248, 252, 248));
    DrawHead(out.SpeechMed, Rect(0, 0, 16, 16), 8.0f, 8.5f, 16.0f, -0.4f, true);
    fill(out.SpeechLarge, 32, 32, RGBA(248, 252, 248));
    DrawHead(out.SpeechLarge, Rect(0, 0, 32, 32), 16.0f, 17.0f, 32.0f, -0.4f, true);

    #ifndef __3DS__
    if(frontFacing) {
        // Generate new PC character faces through the SAME preserved provider
        // and frame-fitting consumer used by live HUD missing-face portraits.
        // Large body previews, action-menu heads and other authored resources
        // keep their own services; no existing character file is rewritten.
        odh::AvatarBodyStrings body;body.avatar_type=_Child?"child":"adult";body.head=_HeadKey;
        size_t start=0;
        for(size_t i=0;i<7;++i){const auto end=_BodyKey.find('|',start);const auto part=_BodyKey.substr(start,end-start);if(i==0)body.body=part;else body.hands[i-1]=part;if(end==std::string::npos)break;start=end+1;}
        OriginalPCUI::Get().GenerateFaces(body,out.Faces);
    }
    #endif
    _TiltAngle = savedTilt;
    _Anim = savedAnim; _AnimMs = savedMs;
    Pose();
}

}}
