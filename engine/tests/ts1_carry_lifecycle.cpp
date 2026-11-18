// SPDX-License-Identifier: MPL-2.0
// Original automatic/custom carry state transitions through the real avatar API.
// Synthetic clips/objects only; no original game or commercial data is needed.
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/IffFile.h"
#include "content/ContentManager.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
namespace VB=FSO::Vitaboy;
namespace IFF=FSO::Files::Formats::IFF;
namespace {
int checks=0;
void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why);++checks; }
struct Fixture {
    IFF::IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"carry-lifecycle",true};
    IFF::Chunks::OBJD Definition;
    FSO::Content::GameObject Object;
    VMContext Context{nullptr};
    VM Machine{&Context,nullptr};
    std::unique_ptr<VMAvatar> Actor;
    std::unique_ptr<VMGameObject> First, Second;
    VB::Animation SentinelClip;
    std::shared_ptr<VMAnimationState> Sentinel, Foreground, Background;
    Fixture() {
        Machine.TS1=true; Context.Clock->Ticks=500;
        Object.OBJ=&Definition; Object.Resource=&Resource;
        Actor=std::make_unique<VMAvatar>(&Object); Actor->ObjectID=1;
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        First=std::make_unique<VMGameObject>(&Object,nullptr);First->ObjectID=2;
        Second=std::make_unique<VMGameObject>(&Object,nullptr);Second->ObjectID=3;
        Actor->WalkAnimations[0x26]="fixture-specific-carry";
        auto asset=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-specific-carry.anim");
        check(asset!=nullptr,"synthetic carry resource resolves");asset->Duration=1200;
        SentinelClip.Name="fixture-custom-carry";SentinelClip.Duration=1000;
        SentinelClip.Motions.resize(1);SentinelClip.Motions[0].FrameCount=30;
        Sentinel=std::make_shared<VMAnimationState>(&SentinelClip,true);
        Foreground=std::make_shared<VMAnimationState>(&SentinelClip,false);
        Background=std::make_shared<VMAnimationState>(&SentinelClip,false);
        Actor->CarryAnimationState=Sentinel; Actor->Animations={Foreground};
        Actor->TS1BackgroundAnimationState=Background;
    }
    bool Place(VMEntity* obj,int slot) { return Actor->PlaceInSlot(obj,slot,false,nullptr); }
    void Update() { Actor->UpdateTS1CarryAnimation(); }
};
void DefaultSelection() {
    Fixture f;
    check(f.Place(f.First.get(),0),"valid first logical attachment accepts held object");
    check(f.Actor->CarryAnimationState==f.Sentinel,
        "placing held object defers original carry selection until model prepass");
    f.Update(); auto original=f.Actor->CarryAnimationState;
    check(original&&original!=f.Sentinel&&original->Anim->Name=="fixture-specific-carry",
        "automatic carry resolves actor locomotion code38 rather than a hardcoded adult clip");
    check(original->TS1Clock&&!original->PlayingBackwards&&!original->Loop&&original->Speed==1.f
        &&original->Weight==1.f&&original->TS1Phase==0.f,
        "automatic carry uses source forward rate1 weight1 loop0 initial phase");
    check(f.Actor->TS1AnimationController.CarryKind==1&&f.Actor->TS1AnimationController.CarryCachedIndex==0,
        "occupied first logical attachment selects default carry kind1 cache0");
    check(f.Actor->Animations.front()==f.Foreground&&f.Actor->TS1BackgroundAnimationState==f.Background,
        "carry prepass preserves foreground and background channel identity");
    original->TS1Phase=.25f;original->EventQueue={17};
    ++f.Context.Clock->Ticks;f.Update();
    check(f.Actor->CarryAnimationState==original&&original->TS1Phase==.25f
        &&original->EventQueue==std::vector<int16_t>{17},
        "unchanged desired carry index does not restart animation or discard events");
    f.Actor->ClearSlot(0);
    check(f.Actor->CarryAnimationState==original,"clearing hand defers original carry transition");
    check(f.Place(f.Second.get(),0),"another held object enters the same logical attachment");
    f.Update();
    check(f.Actor->CarryAnimationState==original,"held-object identity change alone does not restart default carry");
    f.Actor->ClearSlot(0);f.Update();
    check(!f.Actor->CarryAnimationState&&f.Actor->TS1AnimationController.CarryKind==0
        &&f.Actor->TS1AnimationController.CarryCachedIndex==-1,
        "empty first attachment clears default carry at next prepass");
}
void CustomAndSuppressed() {
    Fixture f;check(f.Place(f.First.get(),0),"custom carry fixture holds an object");
    auto& control=f.Actor->TS1AnimationController; control.CarryKind=6;control.CarryCachedIndex=-1;
    f.Update();check(f.Actor->CarryAnimationState==f.Sentinel&&control.CarryKind==6,
        "custom kind6 bypasses automatic carry replacement");
    f.Actor->CarryAnimationState.reset();f.Update();
    check(!f.Actor->CarryAnimationState&&control.CarryKind==6&&control.CarryCachedIndex==-1,
        "custom kind6 with null animation does not recreate automatic carry");
    f.Actor->CarryAnimationState=f.Sentinel;f.Actor->ClearSlot(0);
    check(f.Actor->CarryAnimationState==f.Sentinel,"clearing original hand does not erase custom carry eagerly");
    control.CarryKind=1;control.CarrySuppressed=1;control.CarryCachedIndex=0;
    f.Update();
    check(!f.Actor->CarryAnimationState&&control.CarryKind==1&&control.CarryCachedIndex==-1,
        "suppression clears on cached transition but preserves previous carry kind");
    f.Actor->CarryAnimationState=f.Sentinel;f.Update();
    check(f.Actor->CarryAnimationState==f.Sentinel,
        "unchanged suppressed cache does not repeatedly clear another producer's carry");
    check(f.Place(f.First.get(),0),"suppressed fixture can hold an object without changing carry");
    control.CarrySuppressed=0;f.Update();
    check(f.Actor->CarryAnimationState&&f.Actor->CarryAnimationState!=f.Sentinel
        &&control.CarryKind==1&&control.CarryCachedIndex==0,
        "ending suppression creates automatic carry from real hand occupancy");
}
void BoundsAndMissingClip() {
    Fixture f;
    check(!f.Place(f.First.get(),-1)&&!f.Place(f.First.get(),f.Actor->TotalSlots()),
        "invalid attachment indices reject without animation mutation");
    check(f.Actor->CarryAnimationState==f.Sentinel&&f.First->Container==nullptr,
        "rejected attachment leaves carry and containment unchanged");
    check(f.Place(f.First.get(),1),"independent effect attachment accepts an object");
    f.Actor->TS1AnimationController.CarryCachedIndex=0;f.Update();
    check(!f.Actor->CarryAnimationState&&f.Actor->TS1AnimationController.CarryKind==0,
        "effect attachment does not count as the held-object attachment");
    f.Actor->ClearSlot(1);check(f.Place(f.First.get(),0),"hand fixture accepts object after effect-slot removal");
    f.Actor->WalkAnimations[0x26].clear();f.Update();
    check(!f.Actor->CarryAnimationState&&f.Actor->TS1AnimationController.CarryCachedIndex==0,
        "missing default clip still consumes the original selection transition");
    f.Actor->WalkAnimations[0x26]="fixture-specific-carry";f.Update();
    check(!f.Actor->CarryAnimationState,"missing-clip recovery waits for desired-index change rather than retrying each tick");
    f.Actor->ClearSlot(0);f.Update();check(f.Place(f.First.get(),0),"default carry fixture can reattach");
    f.Update();check(bool(f.Actor->CarryAnimationState),"next actual selection transition retries the restored default clip");
    // Source589fd2 tests nonzero attachment OID, not every nonnull native pointer.
    f.Actor->ClearSlot(0);f.Update();f.First->ObjectID=0;check(f.Place(f.First.get(),0),"synthetic zero-ID pointer can exercise source identity boundary");
    f.Update();check(f.Actor->TS1AnimationController.CarryKind==0&&!f.Actor->CarryAnimationState,
        "zero raw object identity is not a source occupied attachment");
}
void LegacyGuards() {
    Fixture f;f.Actor->TS1ExternalAnimationClock=true;
    check(f.Place(f.First.get(),0),"external-controller hand attachment succeeds");
    check(f.Actor->CarryAnimationState&&f.Actor->CarryAnimationState->Anim->Name=="a2o-rarm-carry-loop",
        "external CAS controller retains its legacy immediate carry attachment policy");
    auto external=f.Actor->CarryAnimationState;f.Update();
    check(f.Actor->CarryAnimationState==external,"original carry prepass is inactive for external CAS clock");
    f.Actor->ClearSlot(0);check(!f.Actor->CarryAnimationState,"external controller retains immediate carry removal");
    f.Actor->TS1ExternalAnimationClock=false;f.Machine.TS1=false;
    check(f.Place(f.First.get(),0),"non-TS1 legacy attachment succeeds");
    auto online=f.Actor->CarryAnimationState;f.Update();
    check(online&&f.Actor->CarryAnimationState==online,"original carry prepass is inactive for non-TS1 simulation");
    f.Actor->ClearSlot(0);check(!f.Actor->CarryAnimationState,"non-TS1 legacy removal remains immediate");
    f.Machine.TS1=true;f.Actor->TS1AnimationController.OriginalLifecycle=false;
    check(f.Place(f.First.get(),0),"historic native continuation can attach through its retained policy");
    auto historic=f.Actor->CarryAnimationState;f.Update();
    check(historic&&historic->Anim->Name=="a2o-rarm-carry-loop"&&f.Actor->CarryAnimationState==historic,
        "unpromoted historic native continuation preserves legacy carry selection");
    f.Actor->ClearSlot(0);check(!f.Actor->CarryAnimationState,"unpromoted historic continuation keeps immediate carry removal");
    f.Actor->TS1AnimationController.OriginalLifecycle=true;
    f.Actor->CarryAnimationState=f.Sentinel;f.Actor->Thread.reset();f.Update();
    check(f.Actor->CarryAnimationState==f.Sentinel,"no-thread preview does not run original carry selection");
}
}
int main() {
    try {
        const bool previous=VM::UseWorld();struct Restore{bool Value;~Restore(){VM::UseWorld(Value);}}restore{previous};
        VM::UseWorld(false);DefaultSelection();CustomAndSuppressed();BoundsAndMissingClip();LegacyGuards();
        std::printf("PASS %d TS1 carry lifecycle controls\n",checks);return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
