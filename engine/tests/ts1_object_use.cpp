// SPDX-License-Identifier: MPL-2.0
// Original person-use lifecycle through real frame returns and functional calls.
// Synthetic BHAVs isolate ownership; actual lot/save acceptance is separate.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/primitives/VMRunFunctionalTree.h"
#include "simantics/primitives/VMExpression.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BHAV.h"
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::LotView::Model;
static int checks=0;
static void need(bool ok,const char* message) { if(!ok)throw std::runtime_error(message);++checks; }
// Drive the actual private dispatch boundary without adding a product test API.
template<class T,typename T::Type Member> struct Access {
    friend typename T::Type field(T) {return Member;}
};
struct ResultDispatch {
    using Type=void (VMThread::*)(VMStackFrame*,VMInstruction*,VMPrimitiveExitCode);
    friend Type field(ResultDispatch);
};
template struct Access<ResultDispatch,&VMThread::HandleResult>;
struct RouteFailure {
    using Type=void (VMRoutingFrame::*)(Routing::VMRouteFailCode,VMEntity*);
    friend Type field(RouteFailure);
};
template struct Access<RouteFailure,&VMRoutingFrame::HardFail>;

struct Fixture {
    VMContext context{nullptr}; VM vm{&context,nullptr};
    IffFile iff;
    FSO::Content::GameObjectResource resource{&iff,nullptr,nullptr,"use-contract",true};
    OBJD personDef,objectDef;
    FSO::Content::GameObject person,object;
    std::unique_ptr<VMAvatar> actor;
    std::unique_ptr<VMGameObject> target;
    Entities::VMMultitileGroup people,objects;
    VMRoutine parent;
    Fixture() {
        vm.GlobalState.resize(64,0);
        context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
        context.Architecture()->Rooms[0].Map.assign(64,0);context.RoomInfo.resize(1);
        personDef.GUID=0x11223344;personDef.ObjectType=OBJDType::Person;
        objectDef.GUID=0x55667788;objectDef.ObjectType=OBJDType::Normal;
        person.OBJ=&personDef;person.GUID=personDef.GUID;person.Resource=&resource;
        object.OBJ=&objectDef;object.GUID=objectDef.GUID;object.Resource=&resource;
        actor=std::make_unique<VMAvatar>(&person);target=std::make_unique<VMGameObject>(&object,nullptr);
        actor->MultitileGroup=&people;people.AddObject(actor.get());
        target->MultitileGroup=&objects;objects.AddObject(target.get());
        vm.AddEntity(actor.get());vm.AddEntity(target.get());
        actor->Thread=std::make_unique<VMThread>(&context,actor.get(),16);
        target->Thread=std::make_unique<VMThread>(&context,target.get(),16);
        actor->Position(LotTilePos(64,64,1));actor->Direction(Direction::NORTH);
        parent.Instructions.resize(1);parent.Instructions[0].TruePointer=0;parent.Instructions[0].FalsePointer=0;
    }
    VMThread& thread(){return *actor->Thread;}
    void masks() {
        actor->SetPersonData(VMPersonDataVariable::RouteFootprintMask,8);
        actor->SetPersonData(VMPersonDataVariable::FootprintExtension,8);
        actor->SetPersonData(VMPersonDataVariable::NonInterruptable,1);
        actor->RefreshTS1Footprint();
    }
    void frame(bool retry=false) {
        auto f=std::make_shared<VMStackFrame>();f->Routine=&parent;
        f->Caller=actor.get();f->Callee=target.get();f->CodeOwner=&object;
        f->SpecialResult=retry?VMSpecialResult::Retry:VMSpecialResult::Normal;
        need(thread().Push(f),"synthetic authored frame push failed");
    }
    int count(){return target->GetValue(VMStackObjectVariable::UseCount);}
};

static void configureResetEntries(Fixture& f) {
    auto main=new BHAV();main->ChunkID=4096;main->ChunkProcessed=true;main->Args=4;
    BHAVInstruction mainInstruction;mainInstruction.Opcode=2;mainInstruction.TruePointer=254;mainInstruction.FalsePointer=255;
    Primitives::VMExpressionOperand mainOperand;mainOperand.Operator=Primitives::VMExpressionOperator::Assign;
    mainOperand.LhsOwner=Scopes::VMVariableScope::MyObjectAttributes;mainOperand.LhsData=1;
    mainOperand.RhsOwner=Scopes::VMVariableScope::Literal;mainOperand.RhsData=88;
    mainOperand.Write(mainInstruction.Operand.data());main->Instructions.push_back(mainInstruction);
    f.iff.AddChunk(main);
    auto reset3=new BHAV();reset3->ChunkID=4097;reset3->ChunkProcessed=true;reset3->Args=4;
    BHAVInstruction writeAttribute;writeAttribute.Opcode=2;writeAttribute.TruePointer=254;writeAttribute.FalsePointer=255;
    Primitives::VMExpressionOperand operand;operand.Operator=Primitives::VMExpressionOperator::Assign;
    operand.LhsOwner=Scopes::VMVariableScope::MyObjectAttributes;operand.LhsData=0;
    operand.RhsOwner=Scopes::VMVariableScope::Literal;operand.RhsData=77;
    operand.Write(writeAttribute.Operand.data());reset3->Instructions.push_back(writeAttribute);f.iff.AddChunk(reset3);
    f.actor->EntryPoints[1].ActionFunction=4096;
    f.actor->EntryPoints[3].ActionFunction=4097;
    f.actor->SetPersonData(VMPersonDataVariable::Gender,2);
}

static void duplicateReturns() {
    Fixture f;f.frame();f.frame(true);f.thread().AcquireTS1ObjectUse(f.target.get());
    f.frame(true);f.thread().AcquireTS1ObjectUse(f.target.get());f.masks();
    need(f.count()==2&&f.thread().TS1ObjectUses.size()==2,"duplicate uses were deduplicated");
    std::unordered_set<VMEntity*> users;f.target->GetUsers(&f.context,&users);
    need(users.size()==1&&users.count(f.actor.get()),"users do not follow actual use ownership");
    f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
    need(f.count()==1&&f.thread().TS1ObjectUses.size()==1,"child return did not retire exactly one use");
    need(f.actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==8,"nested return cleared outer use footprint");
    f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
    need(f.count()==0&&f.thread().TS1ObjectUses.empty(),"last return retained object ownership");
    need(f.actor->GetPersonData(VMPersonDataVariable::RouteFootprintMask)==0&&
         f.actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0&&
         f.actor->GetPersonData(VMPersonDataVariable::NonInterruptable)==0,"last use failed person cleanup");
    need(f.actor->Footprint&&f.actor->Footprint->y1==61,"last use left extended cached bounds");
    f.masks();f.thread().RetireTS1ObjectUse();
    need(f.actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==8,"already-empty use list spuriously cleaned masks");
}

static void functionalFlags() {
    for(int mode=0;mode<4;++mode)
      for(uint8_t flag:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(255)}) {
        Fixture f;auto bhav=new BHAV();bhav->ChunkID=4096;bhav->ChunkProcessed=true;
        BHAVInstruction instruction;instruction.Opcode=2;instruction.TruePointer=254;instruction.FalsePointer=255;
        if(mode!=3)bhav->Instructions.push_back(instruction);f.iff.AddChunk(bhav);
        f.target->EntryPoints[26].ActionFunction=4096;
        f.frame();auto caller=f.thread().Stack.back();caller->StackObject=f.target.get();
        f.thread().IsCheck=mode==1;f.vm.TS1=mode!=2;caller->ActionTree=true;
        auto active=std::make_shared<VMQueuedAction>();active->Callee=f.target.get();
        active->ActionRoutine=&f.parent;active->IconOwner(f.actor.get());
        f.thread().Queue.push_back(active);f.thread().ActiveQueueBlock=0;
        Primitives::VMRunFunctionalTreeOperand operand;operand.Function=8;operand.Flags=flag;
        Primitives::VMRunFunctionalTree primitive;
        need(primitive.Execute(caller.get(),&operand)==VMPrimitiveExitCode::CONTINUE,"functional sit did not push child");
        if(mode==3) {
            need(f.thread().Stack.size()==1&&f.thread().TS1ObjectUses.empty()&&active->IconOwner()==f.actor.get(),
                 "failed functional push changed current icon or acquired ownership");
            continue;
        }
        if(mode!=0) {
            need(f.thread().Stack.size()==2&&f.thread().TS1ObjectUses.empty()&&
                 f.target->ObjectData[size_t(VMStackObjectVariable::UseCount)]==0,
                 "functional check/TSO consumer changed real TS1 ownership");
            if(mode==1)need(active->IconOwner()==f.actor.get(),"check functional call changed a live action icon");
            f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
            continue;
        }
        need(f.thread().Stack.size()==2&&f.thread().TS1ObjectUses.size()==1&&f.count()==1,"functional child failed use acquisition");
        const auto& use=f.thread().TS1ObjectUses.front();
        need(use.Target==f.target.get()&&use.StackDepth==2&&use.Flag==(flag&1),"functional use lost full depth or source flag bit");
        need(active->IconOwner()==((flag&1)?static_cast<VMEntity*>(f.target.get()):f.actor.get()),
             "functional icon ignored the source flag bit");
        f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
        need(f.count()==0&&f.thread().TS1ObjectUses.empty(),"functional return retained a use");
    }
}

static void retainedContextIcon() {
    Fixture f;f.frame();f.frame(true);f.thread().AcquireTS1ObjectUse(f.target.get(),0);
    f.frame(true);f.thread().AcquireTS1ObjectUse(f.actor.get(),1);
    f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
    need(f.thread().TS1FlaggedUseTarget==f.actor.get(),"pre-erase scan lost the removed flagged tail while an outer use remains");
    f.frame(true);f.thread().AcquireTS1ObjectUse(f.target.get(),0);
    f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
    need(f.thread().TS1FlaggedUseTarget==f.actor.get(),"unflagged child return erased the retained current icon");
    f.thread().Pop(VMPrimitiveExitCode::RETURN_TRUE);
    need(f.thread().TS1FlaggedUseTarget==nullptr,"last use did not install the empty current context");
}

static void isolationAndAbandonment() {
    Fixture f;f.frame();f.masks();f.thread().IsCheck=true;
    f.thread().AcquireTS1ObjectUse(f.target.get(),255);f.thread().RetireTS1ObjectUse();
    need(f.count()==0&&f.thread().TS1ObjectUses.empty(),"check interpreter acquired real ownership");
    f.thread().IsCheck=false;f.vm.TS1=false;f.thread().AcquireTS1ObjectUse(f.target.get());
    need(f.thread().TS1ObjectUses.empty(),"TSO interpreter acquired TS1 ownership");
    f.vm.TS1=true;f.frame();f.thread().AcquireTS1ObjectUse(f.target.get());
    f.thread().AcquireTS1ObjectUse(f.actor.get());
    need(f.count()==1&&f.actor->GetValue(VMStackObjectVariable::UseCount)==1,"self use was discarded");
    f.actor->Reset(&f.context);
    need(f.count()==0&&f.actor->GetValue(VMStackObjectVariable::UseCount)==0&&f.thread().TS1ObjectUses.empty(),"Reset abandoned use counts");
    auto resetActive=std::make_shared<VMQueuedAction>();
    auto resetPending=std::make_shared<VMQueuedAction>();
    f.thread().Queue={resetActive,resetPending};f.thread().ActiveQueueBlock=0;
    configureResetEntries(f);
    f.actor->SetAttribute(0,5);
    f.actor->Reset(&f.context);
    need(f.thread().Queue.empty()&&f.thread().ActiveQueueBlock==-1,
         "general Reset did not clear active and pending actions");
    need(f.actor->GetAttribute(0)==77&&f.thread().Stack.size()==1&&
         f.thread().Stack.back()->Routine&&f.thread().Stack.back()->Routine->ID==4096&&
         f.actor->GetAttribute(1)==0,
         "general pet Reset stopped running authored Reset3 or Main");
    f.frame();f.thread().AcquireTS1ObjectUse(f.target.get());
    (f.thread().*field(ResultDispatch{}))(f.thread().Stack.back().get(),nullptr,VMPrimitiveExitCode::INTERRUPT);
    need(f.count()==0&&f.thread().Stack.empty()&&f.thread().TS1ObjectUses.empty(),"interrupt abandoned use state");
}

static void removedTarget() {
    Fixture f;f.frame();f.frame(true);
    auto active=std::make_shared<VMQueuedAction>();active->Callee=f.target.get();
    active->ActionRoutine=&f.parent;active->IconOwner(f.target.get());
    active->UID=41;active->Flags=TTABFlags::MustRun;active->Args={11,12,13,14};
    auto retainedFirst=std::make_shared<VMQueuedAction>();retainedFirst->UID=42;
    retainedFirst->Callee=f.actor.get();retainedFirst->IconOwner(f.actor.get());
    retainedFirst->Flags=TTABFlags::Joinable;retainedFirst->Args={21,22,23,24};
    auto removedTargetAction=std::make_shared<VMQueuedAction>();removedTargetAction->UID=43;
    removedTargetAction->Callee=f.target.get();removedTargetAction->IconOwner(f.actor.get());
    auto removedIconAction=std::make_shared<VMQueuedAction>();removedIconAction->UID=44;
    removedIconAction->Callee=f.actor.get();removedIconAction->IconOwner(f.target.get());
    auto retainedLast=std::make_shared<VMQueuedAction>();retainedLast->UID=45;
    retainedLast->Callee=f.actor.get();retainedLast->IconOwner(f.actor.get());
    retainedLast->Flags=TTABFlags::Joinable;retainedLast->Args={31,32,33,34};
    f.thread().Queue={active,retainedFirst,removedTargetAction,removedIconAction,retainedLast};
    f.thread().ActiveQueueBlock=0;
    f.actor->SetPersonData(VMPersonDataVariable::Priority,50);
    f.thread().AcquireTS1ObjectUse(f.target.get(),1);f.masks();
    const auto id=f.target->ObjectID;f.vm.RemoveEntity(f.target.get());
    need(f.thread().TS1ObjectUses.empty()&&f.count()==0,"removed target kept ownership");
    need(f.actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0,"removed target did not clean person footprint");
    need(f.thread().TS1LastInteraction.Icon==0,"removed target was recreated as the last interaction icon during unwind");
    need(f.thread().TS1LastInteraction.TargetID==0&&!f.thread().TS1LastInteraction.IsValid(),
         "removed last target did not clear the entire last interaction");
    need(f.thread().Queue.size()==2&&f.thread().Queue[0]==retainedFirst&&f.thread().Queue[1]==retainedLast,
         "matched-use removal did not preserve unrelated pending actions in order or retained target/icon actions");
    need(f.thread().Queue[0]->UID==42&&f.thread().Queue[0]->Flags==TTABFlags::Joinable&&
         f.thread().Queue[0]->Args==std::vector<int16_t>({21,22,23,24})&&
         f.thread().Queue[1]->UID==45&&f.thread().Queue[1]->Flags==TTABFlags::Joinable&&
         f.thread().Queue[1]->Args==std::vector<int16_t>({31,32,33,34}),
         "matched-use pending actions lost UID, flags, or arguments");
    need(f.thread().ActiveQueueBlock==-1&&f.thread().QueueDirty&&
         f.actor->GetPersonData(VMPersonDataVariable::Priority)==0,
         "matched-use removal did not leave reset Main/queue state and priority zero");
    need(f.thread().Queue[0]->Callee==f.actor.get()&&f.thread().Queue[0]->IconOwner()==f.actor.get()&&
         f.thread().Queue[1]->Callee==f.actor.get()&&f.thread().Queue[1]->IconOwner()==f.actor.get(),
         "matched-use removal altered retained pending action targets");
    VMGameObject replacement(&f.object,nullptr);Entities::VMMultitileGroup group;
    replacement.Thread=std::make_unique<VMThread>(&f.context,&replacement,16);
    replacement.MultitileGroup=&group;group.AddObject(&replacement);f.vm.AddEntity(&replacement);
    need(replacement.ObjectID==id,"fixture did not exercise recycled object ID");
    need(replacement.GetValue(VMStackObjectVariable::UseCount)==0,"recycled ID inherited old ownership");
}

static void removedPetRestartsMainWithoutReset3() {
    Fixture f;configureResetEntries(f);f.actor->SetAttribute(0,5);
    f.actor->Reseted=true;f.actor->TS1SetSchedulerDelay(9);
    f.frame();f.frame(true);
    auto active=std::make_shared<VMQueuedAction>();active->UID=51;active->Callee=f.target.get();
    active->ActionRoutine=&f.parent;active->IconOwner(f.target.get());
    auto pending=std::make_shared<VMQueuedAction>();pending->UID=52;
    pending->Callee=f.actor.get();pending->IconOwner(f.actor.get());
    f.thread().Queue={active,pending};f.thread().ActiveQueueBlock=0;
    f.thread().AcquireTS1ObjectUse(f.target.get(),1);
    f.vm.RemoveEntity(f.target.get());
    need(f.actor->GetAttribute(0)==5&&f.actor->GetAttribute(1)==0,
         "matched pet target removal executed authored Reset3 or advanced Main instead of stacking it");
    need(f.thread().Stack.size()==1&&f.thread().Stack.back()->Routine&&
         f.thread().Stack.back()->Routine->ID==4096,"matched pet target removal did not stack authored Main");
    need(f.thread().Queue.size()==1&&f.thread().Queue.front()==pending&&f.thread().ActiveQueueBlock==-1,
         "matched pet target removal failed to retain native pending record while restarting Main");
    need(!f.actor->Reseted&&f.actor->TS1SchedulerDelay()==-1&&
         f.actor->GetPersonData(VMPersonDataVariable::Priority)==0,
         "matched pet target removal omitted scheduler/reset/priority cleanup");
}

static void removedIconOnly() {
    Fixture f;f.frame();f.frame(true);
    auto active=std::make_shared<VMQueuedAction>();active->UID=77;active->Callee=f.actor.get();
    active->InteractionNumber=2;active->ActionRoutine=&f.parent;active->IconOwner(f.target.get());
    f.thread().Queue.push_back(active);f.thread().ActiveQueueBlock=0;
    f.thread().AcquireTS1ObjectUse(f.target.get(),1);f.vm.RemoveEntity(f.target.get());
    need(f.thread().TS1LastInteraction.Icon==0&&f.thread().TS1LastInteraction.TargetID==f.actor->ObjectID&&
         f.thread().TS1LastInteraction.UID==77,"removing only the last icon erased the surviving interaction context");
}

static void removedPendingIcon() {
    Fixture f;f.frame();f.thread().Stack.back()->Callee=f.actor.get();
    auto stale=std::make_shared<VMQueuedAction>();stale->Callee=f.actor.get();stale->IconOwner(f.target.get());
    auto retained=std::make_shared<VMQueuedAction>();retained->Callee=f.actor.get();retained->IconOwner(f.actor.get());
    f.thread().Queue={stale,retained};f.vm.RemoveEntity(f.target.get());
    need(f.thread().Queue.size()==1&&f.thread().Queue.front()==retained,
         "removed pending icon retained stale action or deleted unrelated action");
    need(f.thread().Stack.size()==1,"pending icon removal reset an unrelated running stack");
}

static void failureUsesStackObject() {
    Fixture f;
    auto bhav=new BHAV();bhav->ChunkID=398;bhav->ChunkProcessed=true;bhav->Args=4;
    BHAVInstruction instruction;instruction.Opcode=2;instruction.TruePointer=254;instruction.FalsePointer=255;
    bhav->Instructions.push_back(instruction);f.iff.AddChunk(bhav);
    FSO::Content::GameGlobalResource globals(&f.iff,nullptr);FSO::Content::GameGlobal global;global.Resource=&globals;
    f.context.Globals=&global;f.frame();
    auto route=std::make_shared<VMRoutingFrame>();route->Thread=&f.thread();route->Caller=f.actor.get();
    route->Callee=f.actor.get();route->StackObject=f.target.get();route->CodeOwner=&f.object;
    route->CallFailureTrees=true;f.thread().Stack.push_back(route);
    (route.get()->*field(RouteFailure{}))(Routing::VMRouteFailCode::NoPath,nullptr);
    need(f.thread().Stack.size()==3&&f.thread().TS1ObjectUses.size()==1,"failure feedback did not acquire an authored child");
    const auto& use=f.thread().TS1ObjectUses.front();
    need(use.Target==f.target.get()&&use.StackDepth==3&&use.Flag==0,"failure feedback acquired Callee instead of routed StackObject");
    need(f.thread().Stack.back()->Callee==f.target.get()&&f.thread().Stack.back()->StackObject==f.target.get(),
         "failure feedback child did not resolve its routed target");
    need(f.count()==1&&f.actor->GetValue(VMStackObjectVariable::UseCount)==0,"failure feedback changed the wrong object's counter");
}

int main() {
    try {VM::UseWorld(false);duplicateReturns();functionalFlags();retainedContextIcon();isolationAndAbandonment();
        removedTarget();removedPetRestartsMainWithoutReset3();removedIconOnly();removedPendingIcon();failureUsesStackObject();
        std::printf("TS1 object-use lifecycle: %d checks passed\n",checks);return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"TS1 object-use: %s\n",e.what());return 1;}
}
