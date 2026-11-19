// SPDX-License-Identifier: MPL-2.0
// Real avatar/controller transitions with isolated synthetic resource metadata.
// Original household timing and geometry remain separate paired runtime gates.
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "content/ContentManager.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Routing;
using namespace FSO::SimAntics::Model;
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
bool Near(float a,float b){return std::abs(a-b)<0.00005f;}
struct Fixture {
    VMContext Context{nullptr}; VM Machine{&Context,nullptr};
    FSO::Files::Formats::IFF::IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"locomotion-transition",true};
    FSO::Files::Formats::IFF::Chunks::OBJD Definition;
    FSO::Content::GameObject Object;
    std::unique_ptr<VMAvatar> Actor;
    VMRoutine Routine;std::shared_ptr<VMStackFrame> Frame;
    Fixture(){
        Machine.TS1=true;Machine.GlobalState.resize(38,0);Context.Clock->Ticks=600;
        Definition.GUID=0x12345678;Object.GUID=Definition.GUID;Object.OBJ=&Definition;Object.Resource=&Resource;
        Actor=std::make_unique<VMAvatar>(&Object);Actor->ObjectID=11;
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        Actor->Position(FSO::LotView::Model::LotTilePos(24,24,1));Actor->RadianDirection(0);
        Actor->TS1MotionX=24;Actor->TS1MotionY=24;Actor->TS1MotionRate=1;
        Actor->SetPersonData(VMPersonDataVariable::Posture,0);
        Actor->SetPersonData(VMPersonDataVariable::Gender,0);
        for(int code=0;code<32;++code){
            auto name="fixture-transition-"+std::to_string(code);
            Actor->WalkAnimations[code]=Actor->SwimAnimations[code]=name;
            auto* a=FSO::Content::Content::Get()->AvatarAnimations->Get(name+".anim");
            Check(a!=nullptr,"synthetic source selector resource unavailable");
            a->Duration=1000;a->Distance=3;a->Motions.clear();a->Motions.resize(1);
            a->Motions[0].FrameCount=30;a->Motions[0].BoneName="ROOT";
            a->Translations.clear();a->Rotations.clear();a->TranslationCount=a->RotationCount=0;
        }
        Routine.ID=4096;Routine.Instructions.resize(1);Routine.Instructions[0].Opcode=22;
        Frame=std::make_shared<VMStackFrame>();Frame->Thread=Actor->Thread.get();
        Frame->Caller=Frame->Callee=Frame->StackObject=Actor.get();Frame->CodeOwner=&Object;Frame->Routine=&Routine;
        Actor->Thread->Stack.push_back(Frame);
    }
    FSO::Vitaboy::Animation* Asset(int code){return FSO::Content::Content::Get()->AvatarAnimations->Get(Actor->WalkAnimations[code]+".anim");}
    uint64_t Request(){return Actor->StartTS1MotionRequest(Frame.get(),TS1MotionKind::Body,{{24,24},{24,24},{100,24}});}
};
void Admission(){
    Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;
    const auto unchanged=a.Position();
    Check(!a.TryTS1StartMotion(16,0,0),"source radius equality must continue loop directly");
    Check(a.TryTS1StartMotion(std::nextafter(16.f,17.f),0,0),"just outside resource radius must admit walking start");
    Check(c.ModelState==6&&c.MotionCode==19&&a.CurrentAnimationState()->Anim==f.Asset(19),"walking admission must select numeric19/state6");
    Check(a.Position()==unchanged,"start admission must not commit physical placement");
    a.ClearTS1MotionAnimation();c.MotionTransitionKnown=false;c.Moving=c.StopLatched=0;
    Check(a.TryTS1StartMotion(32,0,.2f),"strict angular equality must permit start");
    a.ClearTS1MotionAnimation();c.MotionTransitionKnown=false;c.Moving=c.StopLatched=0;
    Check(!a.TryTS1StartMotion(32,0,std::nextafter(.2f,1.f)),"angle just beyond source threshold must bypass start");
    for(int bypass=0;bypass<4;++bypass){
        c.MotionCode=bypass==2?20:bypass==3?21:-1;c.Moving=bypass==0;c.StopLatched=bypass==1;
        Check(!a.TryTS1StartMotion(32,0,0),"persistent moving/stop/loop-code history must bypass a fresh start");
    }
    c.Moving=c.StopLatched=0;c.MotionCode=-1;a.TS1MotionRate=.5f;
    Check(a.TryTS1StartMotion(32,0,.4f),"subunit rate scales angular equality threshold");
    a.ClearTS1MotionAnimation();c.MotionTransitionKnown=false;c.Moving=c.StopLatched=0;
    a.TS1MotionRate=1;a.SetPersonData(VMPersonDataVariable::IsSwimming,1);
    Check(a.TryTS1StartMotion(1,0,2),"swimming bypasses the ordinary near-distance/large-angle loop branch");
    a.ClearTS1MotionAnimation();c.MotionTransitionKnown=false;c.Moving=c.StopLatched=0;
    c.OriginalLocomotion=false;Check(!a.TryTS1StartMotion(32,0,0),"old adapter must not enter new FSM");
    c.OriginalLocomotion=true;f.Machine.TS1=false;
    Check(!a.TryTS1StartMotion(32,0,0),"TSO must preserve its separate locomotion policy");
}
void PreviousPhaseAndResources(){
    for(int style:{0,1}){
        Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;
        a.SetValue(VMStackObjectVariable::WalkStyle,style);auto* asset=f.Asset(style?22:19);
        asset->Distance=1.5f;asset->Duration=2300;
        Check(a.TryTS1StartMotion(32,0,0),"walk/run start not admitted");
        auto* clip=a.CurrentAnimationState();const auto physical=a.Position();
        Check(c.ModelState==(style?11:6)&&clip->Anim==asset,"walk/run resource/state selection differs");
        Check(Near(c.MotionEndX,24)&&Near(c.MotionEndY,16),"start endpoint must use resource displacement, not root path distance");
        Check(Near(clip->Speed,1.4f),"source walk/run START states both use1.4; hurry2.8 is limited to other source states");
        clip->TS1Phase=std::nextafter(.95f,0.f);
        Check(a.AdvanceTS1MotionTransition(),"phase below .95 must remain in transition");
        Check(a.Position()==physical&&Near(a.TS1MotionY,24-8*clip->TS1Phase),"interpolation must update model only, leaving physical position held");
        Check(clip->TS1Phase==std::nextafter(.95f,0.f),"transition must consume previous phase without advancing clip clock");
        clip->TS1Phase=.95f;
        Check(!a.AdvanceTS1MotionTransition()&&c.ModelState==(style?10:9),".95 equality must finish immediately into correct loop state");
        Check(c.Moving==1&&c.StepLatched==1&&!a.CurrentAnimationState()&&Near(a.TS1MotionY,16)&&a.Position()==physical,"start completion must preserve physical pose and publish endpoint/history once");
    }
}
void PersistentHistory(){
    Fixture f;auto& a=*f.Actor;auto& c=a.TS1AnimationController;
    c.MotionCode=20;c.Moving=1;c.StopLatched=7;c.StepLatched=9;
    auto first=f.Request();Check(first!=0,"real owner request not admitted");a.DetachTS1MotionRequest(first);
    auto second=f.Request();Check(second&&second!=first,"fresh owner token did not advance");
    Check(c.MotionCode==20&&c.Moving==1&&c.StopLatched==7&&c.StepLatched==9,"begin/detach must retain independent model history across requests");
    Check(!a.TryTS1StartMotion(80,0,0),"cross-request loop history must suppress unnecessary restart");
    // A completed180 turn is not itself a bypass: preserved markers decide.
    a.ClearTS1MotionAnimation();auto* turn=a.SelectTS1MotionAnimation(5,false);turn->TS1Phase=1;
    a.BeginTS1MotionTransition(4,24,24,float(M_PI),24,24,float(M_PI));
    Check(!a.AdvanceTS1MotionTransition(),"completed source turn must redispatch immediately");
    Check(!a.TryTS1StartMotion(80,0,0),"the same180turn must bypass start when persistent stop marker is set");
    c.StopLatched=0;Check(a.TryTS1StartMotion(80,0,0),"180-degree turn may legitimately require start with cleared admission history");
}
void ShortStepAndStop(){
    constexpr int expected[8]={15,13,11,12,14,16,18,17};
    for(int sector=0;sector<8;++sector){
        Fixture f;auto& a=*f.Actor;const float angle=float(sector)*float(M_PI/4.0);
        Check(a.TryTS1ShortMotion(24+2*std::cos(angle),24+2*std::sin(angle),0),"valid short directional movement not admitted");
        Check(a.TS1AnimationController.MotionCode==expected[sector]&&a.TS1AnimationController.ModelState==5,"source atan2 sector must select correct authored directional resource");
        Check(a.CurrentAnimationState()->Weight==1,"distance >=1 short movement must use full weight");
    }
    Fixture f;auto& a=*f.Actor;
    auto* shortAsset=f.Asset(15);shortAsset->Motions.resize(1);
    shortAsset->Motions[0].BoneName="ROOT";shortAsset->Motions[0].FrameCount=1;
    shortAsset->Motions[0].HasTranslation=true;shortAsset->Motions[0].FirstTranslationIndex=0;
    shortAsset->Translations={FSO::Vitaboy::Vector3(-.5f,.25f,0)};shortAsset->TranslationCount=1;
    Check(a.TryTS1ShortMotion(24.75f,24,0),"resource-root short step not admitted");
    Check(a.CurrentAnimationState()->Weight>1,"source weight ratio has no invented upper clamp for short displacement");
    Check(Near(a.TS1AnimationController.MotionEndX,24.25f)&&Near(a.TS1AnimationController.MotionEndY,23.75f),"ROOT translation must offset animated endpoint independently from saved target");
    auto physical=a.Position();a.CurrentAnimationState()->TS1Phase=1;
    Check(!a.AdvanceTS1MotionTransition()&&Near(a.TS1MotionX,24.75f)&&Near(a.TS1MotionY,24)&&a.Position()==physical,"state5 completion must restore saved goal without physical placement");
    auto token=f.Request();auto& m=a.TS1ModelMotion;auto& c=a.TS1AnimationController;
    m.StopRequested=true;m.StopDeadline=12345;c.StepLatched=1;
    auto* foreground=a.SelectTS1MotionAnimation(20,true);foreground->TS1ScriptInterruptible=0;
    Check(!a.TryTS1StopMotion()&&m.Status==1,"noninterruptible foreground must defer queued stop");
    foreground->TS1ScriptInterruptible=7;
    Check(a.TryTS1StopMotion()&&c.ModelState==7&&a.CurrentAnimationState()->Speed<0&&a.CurrentAnimationState()->TS1Phase==1,"admitted stop must reverse source19 rather than fabricate a wait");
    Check(m.Token==token&&m.StopDeadline==12345&&m.Outcome==3,"stop transition must retain owner/path/deadline and authored outcome");
    Check(a.AdvanceTS1MotionTransition(),"reverse clip begins at effective phase0 and remains pending");
    a.CurrentAnimationState()->TS1Phase=0;
    Check(!a.AdvanceTS1MotionTransition()&&m.Status==3&&c.ModelState==3,"reverse endpoint must finish with failed outcome and neutral state");
    for(int outcome=1;outcome<=3;++outcome){
        Fixture q;auto& actor=*q.Actor;auto& model=actor.TS1ModelMotion;
        const auto owned=q.Request();model.Outcome=outcome;model.StopDeadline=789;
        actor.SelectTS1MotionAnimation(19,false,true);
        actor.BeginTS1MotionTransition(7,24,24,0,24,24,0);
        actor.CurrentAnimationState()->TS1Phase=0;
        Check(!actor.AdvanceTS1MotionTransition(),"reverse endpoint did not finish its stored outcome");
        Check(model.Status==(outcome==1?-1:outcome==2?2:3)&&model.Token==owned&&model.PathCursor==0&&model.StopDeadline==789,
            "source stop outcome must publish pending/ready/failure without consuming ownership or cursor/deadline");
    }
}
}
int main(){try{VM::UseWorld(false);Admission();PreviousPhaseAndResources();PersistentHistory();ShortStepAndStop();std::cout<<"PASS source locomotion transitions: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
