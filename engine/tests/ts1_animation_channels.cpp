// SPDX-License-Identifier: MPL-2.0
// Real opcode44 channel admission/reset controls using synthetic resources only.
// Source channel roles are independent: foreground +0c, background +14, carry +13c.
#include "simantics/primitives/VMAnimateSim.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/model/VMPersonDataVariable.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/STR.h"
#include "content/ContentManager.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Primitives;
namespace VB=FSO::Vitaboy;
namespace IFF=FSO::Files::Formats::IFF;
namespace {
int checks=0;
void check(bool value,const char* name) { if(!value)throw std::runtime_error(name); ++checks; }
struct Fixture {
    IFF::IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"animation-channels",true};
    IFF::Chunks::OBJD Definition;
    FSO::Content::GameObject Object;
    VMContext Context{nullptr};
    VM Machine{&Context,nullptr};
    std::unique_ptr<VMAvatar> Actor;
    VMStackFrame Frame;
    VMAnimateSim Primitive;
    VB::Animation Foreground, Background, Carry;
    std::shared_ptr<VMAnimationState> ForegroundState, BackgroundState, CarryState;
    Fixture() {
        Machine.TS1=true; Context.Clock->Ticks=1234;
        Definition.AnimationTableID=129; Object.OBJ=&Definition; Object.Resource=&Resource;
        auto table=new IFF::Chunks::STR(); table->ChunkID=129; table->ChunkProcessed=true;
        table->LanguageSets.resize(20);
        table->LanguageSets[0].Strings.resize(2);
        table->LanguageSets[0].Strings[1].Value="fixture-channel-play";
        Iff.AddChunk(table);
        Actor=std::make_unique<VMAvatar>(&Object);
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        Actor->SetPersonData(VMPersonDataVariable::PersonsAge,30);
        Actor->SetPersonData(VMPersonDataVariable::Posture,0);
        Frame.Thread=Actor->Thread.get(); Frame.Caller=Frame.Callee=Frame.StackObject=Actor.get();
        Frame.CodeOwner=&Object; Frame.Args.resize(4); Frame.Locals.resize(4);
        for(auto* a:{&Foreground,&Background,&Carry}) {
            a->Duration=1000; a->Motions.resize(1); a->Motions[0].FrameCount=30;
        }
        Foreground.Name="fixture-foreground"; Background.Name="fixture-background"; Carry.Name="fixture-carry";
        // The ordinary provider owns these synthetic named clips; no installed game is required.
        auto* play=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-channel-play.anim");
        check(play!=nullptr,"synthetic animation provider resolves channel fixture");
        play->Duration=1000;
        auto* idle=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-channel-idle.anim");
        check(idle!=nullptr,"synthetic animation provider resolves external-controller idle fixture");
        idle->Duration=1000; Actor->WalkAnimations[3]="fixture-channel-idle";
        Seed();
    }
    void Seed() {
        ForegroundState=std::make_shared<VMAnimationState>(&Foreground,false);
        BackgroundState=std::make_shared<VMAnimationState>(&Background,false);
        CarryState=std::make_shared<VMAnimationState>(&Carry,false);
        Actor->StartTS1Animation(ForegroundState); Actor->StartTS1Animation(BackgroundState);
        Actor->StartTS1Animation(CarryState);
        ForegroundState->EventQueue={11}; BackgroundState->EventQueue={22}; CarryState->EventQueue={33};
        Actor->Animations={ForegroundState}; Actor->TS1BackgroundAnimationState=BackgroundState;
        Actor->TS1BackgroundPosture=0; Actor->CarryAnimationState=CarryState;
        Actor->SetPersonData(VMPersonDataVariable::UnusedAndDoNotUse,77);
        Frame.PrimitiveState=0;
    }
    VMPrimitiveExitCode Execute(unsigned mode,unsigned id=0,unsigned additionalFlags=0) {
        VMAnimateSimOperand operand; operand.AnimationID=id;
        operand.Source=FSO::SimAntics::Engine::Scopes::VMAnimationScope::Object;
        operand.Flags=(mode&1)|((mode&2)<<3)|additionalFlags;
        check(operand.Mode()==mode,"test mode uses original operand flag bit layout");
        return Primitive.Execute(&Frame,&operand);
    }
};
void ResetChannels() {
    Fixture f;
    for(unsigned mode:{0u,1u,2u,3u}) {
        f.Seed();
        check(f.Execute(mode)==VMPrimitiveExitCode::GOTO_TRUE,"zero-ID animation returns true immediately");
        check(f.Actor->TS1BackgroundAnimationState==(mode==1?nullptr:f.BackgroundState),
            "only mode1 zero clears background");
        check(f.Actor->CarryAnimationState==(mode==2?nullptr:f.CarryState),
            "only mode2 zero clears carry");
        if(mode==0||mode==3) {
            check(f.Actor->Animations.empty(),"default reset clears foreground without injecting standing loop");
            check(f.Actor->GetPersonData(VMPersonDataVariable::UnusedAndDoNotUse)==0,
                "default reset clears original person19");
        } else {
            check(f.Actor->Animations.size()==1&&f.Actor->Animations.front()==f.ForegroundState,
                "background/carry resets retain foreground identity");
            check(f.Actor->GetPersonData(VMPersonDataVariable::UnusedAndDoNotUse)==77,
                "background/carry resets retain person19");
        }
        if(mode!=1)check(f.BackgroundState->EventQueue==std::vector<int16_t>{22},
            "untouched background retains pending events");
        if(mode!=2)check(f.CarryState->EventQueue==std::vector<int16_t>{33},
            "untouched carry retains pending events");
    }
    // The actual listener reset operand observed in the DVD capture is mode1.
    f.Seed(); VMAnimateSimOperand raw;
    const uint8_t operand[8]={0,0,0,0,2,1,0,0}; raw.Read(operand);
    check(raw.Mode()==1&&raw.AnimationID==0,"recorded listener operand decodes as background reset");
    check(f.Primitive.Execute(&f.Frame,&raw)==VMPrimitiveExitCode::GOTO_TRUE,
        "recorded listener reset returns without waiting");
    check(!f.Actor->TS1BackgroundAnimationState&&f.Actor->Animations.front()==f.ForegroundState,
        "recorded listener reset does not replace foreground with a standing loop");
}
void StartChannels() {
    Fixture f;
    f.Seed();
    f.Actor->Avatar->LeftHandGesture=VB::SimHandGesture::Fist;
    f.Actor->Avatar->RightHandGesture=VB::SimHandGesture::Pointing;
    check(f.Execute(1,1,2|128)==VMPrimitiveExitCode::GOTO_TRUE,
        "nonzero background starts without waiting");
    auto background=f.Actor->TS1BackgroundAnimationState;
    check(background&&background!=f.BackgroundState&&background->Anim->Name=="fixture-channel-play",
        "mode1 resolves synthetic object-bank clip onto background");
    check(background->Loop&&background->PlayingBackwards&&background->TS1Clock,
        "background preserves looping backwards original-clock semantics");
    check(f.Actor->TS1RawAnimationClock&&background->TS1Phase==1.f,
        "background operand raw-clock flag and backwards initial phase are honored");
    check(f.Actor->Animations.front()==f.ForegroundState&&f.Actor->CarryAnimationState==f.CarryState,
        "starting background retains foreground and carry");
    check(f.Actor->Avatar->LeftHandGesture==VB::SimHandGesture::Fist&&
        f.Actor->Avatar->RightHandGesture==VB::SimHandGesture::Pointing,
        "starting original background retains the foreground hand gestures");
    f.Seed();
    check(f.Execute(2,1,2)==VMPrimitiveExitCode::GOTO_TRUE,
        "nonzero carry starts without waiting");
    auto carry=f.Actor->CarryAnimationState;
    check(carry&&carry!=f.CarryState&&carry->Anim->Name=="fixture-channel-play",
        "mode2 resolves synthetic object-bank clip onto carry");
    check(carry->Loop&&carry->PlayingBackwards&&carry->TS1Clock&&carry->TS1Phase==1.f,
        "carry respects original backwards operand rather than forcing forward playback");
    check(f.Actor->Animations.front()==f.ForegroundState&&f.Actor->TS1BackgroundAnimationState==f.BackgroundState,
        "starting carry retains foreground and background");
    f.Seed();
    check(f.Execute(3,1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
        "mode3 starts foreground and waits");
    check(!f.Actor->CarryAnimationState&&f.Actor->TS1BackgroundAnimationState==f.BackgroundState,
        "new mode3 foreground clears carry only and retains background");
    // A carry installed after initialization must survive the foreground poll.
    f.Actor->CarryAnimationState=f.CarryState;
    check(f.Execute(3,1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
        "mode3 polls the existing foreground");
    check(f.Actor->CarryAnimationState==f.CarryState,
        "mode3 poll does not repeat the new-foreground carry reset");
}
void DefaultBackground() {
    Fixture f;
    f.Actor->Animations.clear(); f.Actor->TS1BackgroundAnimationState.reset();
    f.Actor->TS1BackgroundPosture=-1;
    f.Actor->UpdateTS1BackgroundAnimation();
    auto idle=f.Actor->TS1BackgroundAnimationState;
    check(idle&&idle->Anim->Name=="fixture-channel-idle"&&idle->Loop&&idle->TS1Clock,
        "source prepass creates default standing background");
    check(!f.Actor->CurrentAnimationState()&&f.Actor->TS1BackgroundPosture==0,
        "default background does not become blocking foreground");
    f.Actor->UpdateTS1BackgroundAnimation();
    check(f.Actor->TS1BackgroundAnimationState==idle,"human unchanged posture retains background clock identity");
    f.Actor->SetPersonData(VMPersonDataVariable::Posture,7);
    f.Actor->UpdateTS1BackgroundAnimation();
    check(!f.Actor->TS1BackgroundAnimationState&&f.Actor->TS1BackgroundPosture==7,
        "unsupported source posture clears stale background and caches literal value");
    f.Actor->SetPersonData(VMPersonDataVariable::Posture,0);f.Definition.TS1ModelFlags=4;
    f.Actor->UpdateTS1BackgroundAnimation();
    check(!f.Actor->TS1BackgroundAnimationState&&f.Actor->TS1BackgroundPosture==7,
        "original OBJD0xcc mask04 excludes automatic background");
    f.Definition.TS1ModelFlags=0;f.Actor->UpdateTS1BackgroundAnimation();
    f.Actor->SetPersonData(VMPersonDataVariable::Gender,2);
    auto before=f.Actor->TS1BackgroundAnimationState;f.Actor->UpdateTS1BackgroundAnimation();
    check(f.Actor->TS1BackgroundAnimationState!=before,"source species branch refreshes even unchanged background");
    // Version138 can carry extension bytes past its old field table. The
    // original normalizer copies present bytes and zero-fills a short payload.
    std::vector<uint8_t> payload(216);payload[0]=138;payload[0xcc]=4;
    IFF::Chunks::OBJD definition;st::Reader reader(payload,st::Endian::Little);definition.Read(nullptr,reader);
    check(definition.TS1ModelFlags==4,"version138 extended OBJD preserves default-background exclusion byte");
    st::Writer written(st::Endian::Little);definition.Write(nullptr,written);
    check(written.ToArray().at(0xcc)==4,"OBJD export retains the consumed exclusion byte");
    payload.resize(190);st::Reader shortReader(payload,st::Endian::Little);definition.Read(nullptr,shortReader);
    check(definition.TS1ModelFlags==0,"short original OBJD extension is zero-filled on reread");
}
void PoseOrder() {
    Fixture f;
    VB::Skeleton skeleton;auto root=std::make_unique<VB::Bone>();root->Name="ROOT";root->CanTranslate=1;
    skeleton.RootBone=root.get();skeleton.Bones.push_back(std::move(root));
    f.Actor->Avatar=std::make_unique<VB::SimAvatar>(&skeleton);
    auto configure=[](VB::Animation& clip,float x) {
        clip.Duration=1000;clip.Motions.resize(1);auto& motion=clip.Motions[0];
        motion.BoneName="ROOT";motion.FrameCount=1;motion.HasTranslation=true;
        clip.TranslationCount=1;clip.Translations={VB::Vector3(x,0,0)};
    };
    configure(f.Background,1);configure(f.Foreground,2);configure(f.Carry,3);
    f.Seed();f.Actor->AdvanceAnimations(false);
    auto bone=f.Actor->Avatar->Skeleton->GetBone("ROOT");
    check(bone&&bone->Translation.X==3,"actual pose composition orders background before foreground before carry");
    f.Actor->CarryAnimationState.reset();f.Actor->AdvanceAnimations(false);
    check(bone->Translation.X==2,"foreground overrides the background in real pose sampling");
    f.Actor->Animations.clear();f.Actor->AdvanceAnimations(false);
    check(bone->Translation.X==1&&!f.Actor->CurrentAnimationState(),"background alone poses without becoming foreground");
    f.Actor->Animations={f.ForegroundState};f.Actor->CarryAnimationState=f.CarryState;
    f.Actor->FractionalAnim(.5f);
    check(bone->Translation.X==3,"fractional rendering retains background/foreground/carry order");
}
void ExternalControllerCompatibility() {
    Fixture f; f.Actor->TS1ExternalAnimationClock=true; f.Seed();
    check(f.Execute(1)==VMPrimitiveExitCode::GOTO_TRUE,"external-controller reset returns true");
    check(f.Actor->Animations.size()==1&&f.Actor->Animations.front()->Loop
        &&f.Actor->Animations.front()->Anim->Name=="fixture-channel-idle",
        "external CAS controller retains its existing foreground idle policy");
    f.Seed();
    check(f.Execute(3)==VMPrimitiveExitCode::GOTO_TRUE&&!f.Actor->CarryAnimationState,
        "external-controller mode3 reset retains legacy carry reset");
    check(f.Actor->Animations.size()==1&&f.Actor->Animations.front()==f.ForegroundState,
        "external-controller mode3 reset does not change its foreground");
}
}
int main() {
    try {
        const bool previous=VM::UseWorld();
        struct Restore { bool Previous; ~Restore(){VM::UseWorld(Previous);} } restore{previous};
        VM::UseWorld(false);
        ResetChannels(); StartChannels(); DefaultBackground(); PoseOrder(); ExternalControllerCompatibility();
        std::printf("PASS %d TS1 animation channel controls\n",checks); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL %s\n",e.what()); return 1; }
}
