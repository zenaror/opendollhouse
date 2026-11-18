// SPDX-License-Identifier: MPL-2.0
// Real interpreter frame removal must release the matching shared model owner
// before its parent can request another motion. No model tick or game assets.
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
int checks=0;
void check(bool value,const char* reason) { ++checks;if (!value) throw std::runtime_error(reason); }
struct Fixture {
    VMContext Context{nullptr};
    VM Machine{&Context,nullptr};
    IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"motion-lifecycle",true};
    OBJD Definition;
    FSO::Content::GameObject Object;
    VMRoutine Routine;
    std::unique_ptr<VMAvatar> Actor;
    std::unique_ptr<VMGameObject> Target;
    Fixture() {
        Machine.TS1=true;Definition.GUID=0x12345678;
        Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;
        Routine.ID=0x1000;Routine.Instructions.resize(1);
        Routine.Instructions[0].Opcode=255;Routine.Instructions[0].TruePointer=254;
        Context.Primitives[255].reset();
        Actor=std::make_unique<VMAvatar>(&Object);Actor->ObjectID=1;Actor->GhostImage=true;
        Target=std::make_unique<VMGameObject>(&Object,nullptr);Target->ObjectID=2;
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        Actor->Position(FSO::LotView::Model::LotTilePos(72,72,1));
    }
    std::shared_ptr<VMStackFrame> frame() {
        auto result=std::make_shared<VMStackFrame>();result->Thread=Actor->Thread.get();
        result->Caller=Actor.get();result->Callee=Actor.get();result->CodeOwner=&Object;result->Routine=&Routine;
        return result;
    }
    uint64_t start(const std::shared_ptr<VMStackFrame>& owner) {
        Actor->Thread->Stack.push_back(owner);
        const auto token=Actor->StartTS1MotionRequest(owner.get(),Routing::TS1MotionKind::Body,
            {{72,72},{72,72},{88,72}});
        check(token!=0,"fixture starts an actual avatar model request");return token;
    }
    bool clear() const { return Actor->TS1ModelMotion.Kind==Routing::TS1MotionKind::None && Actor->TS1ModelMotion.Token==0; }
};
void popAndImmediateReuse() {
    Fixture f;const auto owner=f.frame();const auto token=f.start(owner);bool traced=false;
    f.Machine.OnTraceFrameReturn=[&](const VMStackFrame* frame,int) {
        traced=frame==owner.get() && frame->TS1MotionToken==token && f.Actor->TS1ModelMotion.Token==token;
    };
    f.Actor->Thread->Pop(VMPrimitiveExitCode::RETURN_TRUE);
    check(traced,"return callback keeps its original observation before destructive pop");
    check(f.clear() && owner->TS1MotionToken==0 && f.Actor->Thread->Stack.empty(),
        "normal Pop detaches matching token before destroying its owning frame");
    auto next=f.frame();const auto newer=f.start(next);
    check(newer!=token && f.Actor->OwnsTS1MotionRequest(next.get()),
        "a new request can start immediately without waiting for a model update");
    auto stale=f.frame();stale->TS1MotionToken=token;
    f.Actor->Thread->DetachTS1FrameMotion(stale.get());
    check(f.Actor->TS1ModelMotion.Token==newer,"discarding a stale token cannot detach the newer model request");
}
void parentAndCheckPreservation() {
    Fixture f;auto parent=f.frame();const auto token=f.start(parent);
    auto child=f.frame();f.Actor->Thread->Stack.push_back(child);
    f.Actor->Thread->Pop(VMPrimitiveExitCode::CONTINUE);
    check(f.Actor->TS1ModelMotion.Token==token && f.Actor->Thread->Stack.back()==parent,
        "popping an unrelated child preserves its parent's motion request");
    auto checkFrame=f.frame();checkFrame->TS1MotionToken=token;
    check(VMThread::EvaluateCheck(&f.Context,f.Actor.get(),checkFrame)==VMPrimitiveExitCode::RETURN_TRUE,
        "temporary check tree executes and returns normally");
    check(f.Actor->TS1ModelMotion.Token==token && parent->TS1MotionToken==token,
        "check-tree frame removal does not detach the live interpreter's model");
    f.Actor->Thread->IsCheck=true;f.Actor->Thread->DetachTS1StackMotion();f.Actor->Thread->IsCheck=false;
    check(f.Actor->TS1ModelMotion.Token==token,"same-machine temporary check stack is explicitly protected");
    VMThread other(&f.Context,f.Actor.get(),4);other.DetachTS1FrameMotion(parent.get());
    check(f.Actor->TS1ModelMotion.Token==token,"a different thread cannot detach this avatar's owning frame");
}
void destructiveUnwinds() {
    {
        Fixture f;auto owner=f.frame();owner->SpecialResult=VMSpecialResult::Interaction;f.start(owner);
        f.Actor->Thread->AbortCurrentInteraction();
        check(f.clear() && f.Actor->Thread->Stack.empty() && owner->TS1MotionToken==0,
            "aborted interaction releases model before callback/queue cleanup");
    }
    {
        Fixture f;auto owner=f.frame();f.start(owner);f.Actor->Reset(&f.Context);
        check(f.clear() && owner->TS1MotionToken==0 && f.Actor->Thread->Stack.empty(),
            "entity reset releases model before discarding interpreter frames");
    }
    {
        Fixture f;auto owner=f.frame();f.start(owner);
        f.Actor->Thread->TS1FlaggedUseTarget=f.Target.get();
        f.Actor->Thread->TS1ObjectUseTargetRemoved(f.Target.get());
        check(f.clear() && owner->TS1MotionToken==0 && f.Actor->Thread->Stack.empty(),
            "deleted active target releases model during selective interpreter restart");
    }
}
}
int main() {
    try { popAndImmediateReuse();parentAndCheckPreservation();destructiveUnwinds();
        std::cout << checks << " model-owner lifecycle checks passed\n";return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
