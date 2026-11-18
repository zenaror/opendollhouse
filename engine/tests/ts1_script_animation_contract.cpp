// SPDX-License-Identifier: MPL-2.0
// Actual opcode44 with synthetic assets: model-owned admission, FIFO and terminal state.
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
namespace IFF=FSO::Files::Formats::IFF;
namespace VB=FSO::Vitaboy;
namespace {
int checks=0;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
struct Fixture {
    IFF::IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"script-animation",true};
    IFF::Chunks::OBJD Definition;
    FSO::Content::GameObject Object;
    VMContext Context{nullptr};
    VM Machine{&Context,nullptr};
    std::unique_ptr<VMAvatar> Actor;
    VMStackFrame Frame;
    VMAnimateSim Primitive;
    VB::Animation* A=nullptr;
    VB::Animation* B=nullptr;
    std::shared_ptr<VMAnimationState> Foreground,Background,Carry;
    Fixture() {
        Machine.TS1=true;Machine.GlobalState.resize(38,0);Context.Clock->Ticks=600;
        Definition.AnimationTableID=129;Object.OBJ=&Definition;Object.Resource=&Resource;
        auto table=new IFF::Chunks::STR();table->ChunkID=129;table->ChunkProcessed=true;
        table->LanguageSets.resize(20);table->LanguageSets[0].Strings.resize(3);
        table->LanguageSets[0].Strings[1].Value="fixture-script-a";
        table->LanguageSets[0].Strings[2].Value="fixture-script-b";Iff.AddChunk(table);
        Actor=std::make_unique<VMAvatar>(&Object);Actor->ObjectID=1;
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        Actor->SetPersonData(VMPersonDataVariable::PersonsAge,30);
        Frame.Thread=Actor->Thread.get();Frame.Caller=Frame.Callee=Frame.StackObject=Actor.get();
        Frame.CodeOwner=&Object;Frame.Args.resize(4,55);Frame.Locals.resize(4,66);
        A=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-script-a.anim");
        B=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-script-b.anim");
        check(A&&B&&A!=B,"two distinct synthetic script animation resources resolve");
        A->Duration=B->Duration=1000;
        Foreground=std::make_shared<VMAnimationState>(A,false);Foreground->TS1ScriptAnimation=true;
        Background=std::make_shared<VMAnimationState>(B,false);Background->TS1ScriptAnimation=true;
        Carry=std::make_shared<VMAnimationState>(B,false);Carry->TS1ScriptAnimation=true;
        Actor->StartTS1Animation(Foreground);Actor->StartTS1Animation(Background);Actor->StartTS1Animation(Carry);
        Actor->Animations={Foreground};Actor->TS1BackgroundAnimationState=Background;
        Actor->TS1BackgroundPosture=0;Actor->CarryAnimationState=Carry;
        Actor->TS1AnimationController.ScriptActive=1;Actor->TS1AnimationController.ModelState=0;
    }
    VMPrimitiveExitCode Execute(unsigned mode=0,unsigned id=1,unsigned count=0,unsigned flags=0) {
        VMAnimateSimOperand operand;operand.AnimationID=id;operand.ExpectedEventCount=count;
        operand.Source=FSO::SimAntics::Engine::Scopes::VMAnimationScope::Object;
        operand.Flags=(mode&1)|((mode&2)<<3)|flags;operand.LocalEventNumber=1;
        return Primitive.Execute(&Frame,&operand);
    }
};
void Admission() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    state.ScriptActive=0;state.EventsRun=9;state.Events={123,456};f.Foreground->EndReached=true;
    check(f.Execute()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"new foreground admission waits on newly started clip");
    auto started=f.Actor->CurrentAnimationState();
    check(started&&started!=f.Foreground.get()&&started->Anim==f.A&&!started->EndReached,
        "inactive script starts anew even when foreground resource pointer already matches");
    check(state.ScriptActive==1&&state.EventsRun==0&&state.Events.empty()&&state.ModelState==0,
        "new script admission resets expected counter and shared FIFO exactly once");
    check(f.Actor->TS1BackgroundAnimationState==f.Background&&f.Actor->CarryAnimationState==f.Carry,
        "ordinary foreground admission preserves background and carry");
    state.Events={0};
    check(f.Execute(0,2,1)==VMPrimitiveExitCode::GOTO_FALSE,
        "active script polls FIFO when the current operand names another resource");
    check(f.Actor->CurrentAnimationState()==started&&state.EventsRun==1&&f.Frame.Args[0]==0,
        "active script does not restart due to asset mismatch");
    f.Frame.CodeOwner=nullptr;
    check(f.Execute(0,65535)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
        "active foreground poll does not dereference an asset owner or resolve an invalid clip ID");
    check(f.Actor->CurrentAnimationState()==started,"asset-free active poll retains foreground identity");
}
void SharedEvents() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    state.Events={1,0,65536,100,-1,-65536};
    check(f.Execute(0,1,1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
        "out-of-order sequential xevt is consumed and waits");
    check(state.EventsRun==0&&state.Events.front()==0&&f.Frame.Args[0]==55,
        "invalid extra event does not increment expected count or write the script variable");
    check(f.Execute(0,1,1)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Args[0]==0&&state.EventsRun==1,
        "matching sequential xevt advances expected count and writes parameter0");
    check(f.Execute(0,1,1,32)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Locals[1]==0,
        "wide positive xevt is classified as special before final int16 local narrowing");
    check(state.EventsRun==1&&state.Events.front()==100,
        "special event does not change sequential count and FIFO order is retained");
    check(f.Execute(0,1,1)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Args[0]==100&&state.EventsRun==1,
        "event100 is forwarded without consuming a sequential expected index");
    check(f.Execute(0,1,1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&state.EventsRun==1,
        "negative xevt is not a special event");
    check(f.Execute(0,1,1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&state.Events.empty()&&f.Frame.Args[0]==100,
        "wide negative xevt is not prematurely narrowed into a matching index");
    state.Events={INT32_MAX};
    check(f.Execute(0,1,1)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Args[0]==-1&&state.EventsRun==1,
        "special signed32 maximum narrows only at the script variable store");
    // Deliberately model-owned events: no per-foreground queue has been populated.
    check(f.Foreground->EventQueue.empty()&&f.Background->EventQueue.empty()&&f.Carry->EventQueue.empty(),
        "script FIFO processing is independent of legacy per-clip queues");
}
void ReverseAndSignedCounts() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    state.Events={2,1,0};f.Foreground->PlayingBackwards=true;
    for(int expected:{2,1,0})
        check(f.Execute(0,1,3,2)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Args[0]==expected,
            "backwards script accepts expected xevts in reverse order");
    check(state.EventsRun==3&&state.Events.empty(),"reverse accepted count increments independently of event index");
    for(unsigned raw:{128u,255u}) {
        state.ScriptActive=1;state.ModelState=0;state.EventsRun=0;state.Events={0};
        f.Foreground->EndReached=true;
        check(f.Execute(0,1,raw)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&state.Events.empty()&&state.EventsRun==0,
            "expected-event count is signed int8 so raw128/255 reject sequential event0");
        check(f.Execute(0,1,raw)==VMPrimitiveExitCode::GOTO_TRUE&&state.EventsRun==0,
            "negative signed expected count produces no fabricated missing events");
    }
}
void TerminalAndReset() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    f.Actor->Animations.clear();state.EventsRun=0;
    check(f.Execute(0,2,2)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Args[0]==0,
        "active script with null foreground synthesizes first missing event without restarting a clip");
    check(f.Execute(0,2,2)==VMPrimitiveExitCode::GOTO_FALSE&&f.Frame.Args[0]==1&&state.EventsRun==2,
        "null foreground emits one synthetic event per poll");
    check(f.Execute(0,2,2)==VMPrimitiveExitCode::GOTO_TRUE&&state.ScriptActive==0&&f.Actor->Animations.empty(),
        "null foreground completes after expected events are exhausted");
    for(unsigned mode:{0u,1u,2u,3u}) {
        state.ScriptActive=1;state.ModelState=0;state.EventsRun=7;state.Events={0,123456};
        f.Actor->Animations={f.Foreground};f.Actor->TS1BackgroundAnimationState=f.Background;
        f.Actor->CarryAnimationState=f.Carry;
        check(f.Execute(mode,0)==VMPrimitiveExitCode::GOTO_TRUE,
            "zero-ID reset is immediate in every channel mode");
        check(state.ScriptActive==1&&state.EventsRun==7&&state.Events==std::vector<int32_t>({0,123456}),
            "zero-ID reset preserves model admission, expected-event counter and shared FIFO");
    }
    state.ScriptActive=1;state.ModelState=0;state.EventsRun=0;state.Events.clear();state.CarrySuppressed=1;
    f.Actor->Animations={f.Foreground};f.Foreground->EndReached=true;
    f.Actor->SetPersonData(VMPersonDataVariable::UnusedAndDoNotUse,77);
    check(f.Execute()==VMPrimitiveExitCode::GOTO_TRUE,"ended foreground finishes normal script");
    check(f.Actor->CurrentAnimationState()==f.Foreground.get()&&f.Foreground->EndReached,
        "normal completion retains terminal foreground pose rather than clearing the channel");
    check(state.ScriptActive==0&&state.CarrySuppressed==0&&state.ModelState==3
        &&f.Actor->GetPersonData(VMPersonDataVariable::UnusedAndDoNotUse)==0,
        "normal completion resets admission, suppression, posture state and person19");
}
void CustomCarryAndGate() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    state.ScriptActive=0;state.CarryKind=6;state.CarryCachedIndex=0;
    check(f.Execute(3)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"mode3 admits new waiting foreground");
    check(!f.Actor->CarryAnimationState&&state.CarryKind==6&&state.CarryCachedIndex==-1&&state.CarrySuppressed==1,
        "mode3 clears carry and sets suppression/cache without erasing custom carry kind6");
    f.Actor->CarryAnimationState=f.Carry;
    check(f.Execute(3)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Actor->CarryAnimationState==f.Carry,
        "mode3 active poll does not repeat initialization or clear a later carry producer");
    f.Frame.Args.clear();state.Events={100};
    for(int badState:{-1,4,9}) {
        state.ModelState=badState;
        check(f.Execute(0,65535,0,4)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
            "model-state gate runs before invalid parameter animation ID lookup");
        check(state.Events==std::vector<int32_t>{100},"rejected model state does not poll or mutate shared events");
    }
}
void FallbackAndPromotion() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;state.ScriptActive=0;
    f.Actor->WalkAnimations[0x25]="fixture-script-b";
    check(f.Execute(0,99,0,2)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
        "missing requested clip starts source fallback");
    check(f.Actor->CurrentAnimationState()->Anim==f.B&&!f.Actor->CurrentAnimationState()->PlayingBackwards,
        "fallback explicitly plays forward despite reverse operand");
    state=VMTS1AnimationControllerState{};state.OriginalLifecycle=false;
    f.Actor->Animations.clear();f.Actor->CarryAnimationState.reset();
    f.Background->TS1ScriptAnimation=false;f.Background->TS1Phase=.4f;
    f.Actor->TS1BackgroundAnimationState=f.Background;
    f.Execute(1,0);
    check(!state.OriginalLifecycle&&!f.Actor->TS1BackgroundAnimationState,
        "legacy background is reset by authored command without promotion or event replay");
    check(f.Execute()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&state.OriginalLifecycle&&state.ScriptActive==1,
        "complete channel quiescence permits new source admission without guessed event history");
}
void InterruptibleAndPosture() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    VB::TimePropertyListItem property;VB::PropertyListItem values;
    values.KeyPairs={{"interruptable","7"},{"interruptible","-2"}};
    property.Properties.Items={values};
    f.Actor->HandleTimePropsEvent(property,false,f.Background.get());
    check(f.Background->TS1ScriptInterruptible==-2&&f.Foreground->TS1ScriptInterruptible==0,
        "both spellings update emitting animation's raw signed field without changing unrelated foreground");
    f.Actor->HandleTimePropsEvent(property,false,f.Foreground.get());
    f.Actor->SetPersonData(VMPersonDataVariable::NonInterruptable,1);
    check(f.Execute()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"interruptible clip still waits when original person predicate refuses");
    f.Actor->SetPersonData(VMPersonDataVariable::NonInterruptable,0);
    f.Machine.SetGlobalValue(10,0);f.Actor->SetMotiveDataFloat(VMMotive::Hunger,-100);
    check(f.Execute()==VMPrimitiveExitCode::GOTO_TRUE&&state.ScriptActive==0,
        "callback-enabled interruptible clip finishes through proven original person predicate");
    for(int modelState:{1,2})for(int decoration:{0,1}) {
        state.ModelState=modelState;f.Actor->Animations={f.Foreground};
        f.Actor->SetPersonData(VMPersonDataVariable::DecorationFlags,decoration);
        f.Actor->UpdateTS1ModelMotion();
        check((f.Actor->CurrentAnimationState()==nullptr)==(modelState==2||decoration==0),
            "source posture states clear foreground with exact DecorationFlags condition");
        check(state.ModelState==modelState,"posture foreground cleanup preserves original model state");
    }
}
void SharedOwnedModel() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;state.ScriptActive=0;state.ModelState=3;
    VMRoutine routine;routine.ID=4096;routine.Instructions.resize(1);routine.Instructions[0].Opcode=22;
    auto parent=std::make_shared<VMStackFrame>(f.Frame);parent->Routine=&routine;parent->PrimitiveState=1;
    f.Actor->Thread->Stack={parent};
    const auto token=f.Actor->StartTS1MotionRequest(parent.get(),FSO::SimAntics::Engine::Routing::TS1MotionKind::Body,{{1,1},{2,1},{3,1}});
    check(token!=0,"source child script fixture owns a real body motion request");
    f.Actor->TS1ModelMotion.PathCursor=0;const auto path=f.Actor->TS1ModelMotion.Path;
    check(f.Execute()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&state.ModelState==0&&f.Actor->TS1ModelMotion.ModelState==0,
        "script admission writes the one effective model state while parent retains ownership");
    f.Actor->UpdateTS1ModelMotion();
    check(f.Actor->TS1ModelMotion.Token==token&&f.Actor->TS1ModelMotion.Status==1&&
        f.Actor->TS1ModelMotion.PathCursor==0&&f.Actor->TS1ModelMotion.Path.size()==path.size(),
        "script state pauses kinematics without failing or consuming pending request");
    f.Actor->CurrentAnimationState()->EndReached=true;
    check(f.Execute()==VMPrimitiveExitCode::GOTO_TRUE&&state.ModelState==3&&f.Actor->TS1ModelMotion.ModelState==3,
        "script completion restores posture state in both native representations of source model190");
}
void LegacyStillInFlight() {
    Fixture f;auto& state=f.Actor->TS1AnimationController;
    state.OriginalLifecycle=false;state.Events={77777};
    check(f.Execute()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,
        "historic continuation with authored channels in flight still uses its legacy poll");
    check(!state.OriginalLifecycle&&state.Events==std::vector<int32_t>{77777}
        &&f.Actor->CurrentAnimationState()==f.Foreground.get(),
        "in-flight historic adapter is not promoted and does not consume the new model FIFO");
}
}
int main(){try{
    const bool previous=VM::UseWorld();struct Restore{bool Value;~Restore(){VM::UseWorld(Value);}}restore{previous};
    VM::UseWorld(false);Admission();SharedEvents();ReverseAndSignedCounts();TerminalAndReset();CustomCarryAndGate();FallbackAndPromotion();InterruptibleAndPosture();SharedOwnedModel();LegacyStillInFlight();
    std::printf("PASS %d TS1 script animation controls\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
