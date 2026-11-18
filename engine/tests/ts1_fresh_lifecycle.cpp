// SPDX-License-Identifier: MPL-2.0
// Synthetic authored consumers of fresh object creation. No retail data or
// window; the original runtime comparison is a separate acceptance gate.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "simantics/utils/VMWorldActivator.h"
#include "simantics/primitives/VMSleep.h"
#include "simantics/primitives/VMRemoveObjectInstance.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/BHAV.h"
#include <cstdio>
#include <functional>
#include <stdexcept>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
using FSO::LotView::Model::Direction;
static int checks=0;
static void need(bool value,const char* why) {if(!value)throw std::runtime_error(why);++checks;}

template<class T,typename T::Type Member> struct Access {friend typename T::Type field(T){return Member;}};
struct CacheField {
    using Type=std::unordered_map<uint64_t,std::unique_ptr<FSO::Content::GameObject>> FSO::Content::WorldObjectProvider::*;
    friend Type field(CacheField);
};
template struct Access<CacheField,&FSO::Content::WorldObjectProvider::Cache>;
struct Handler:VMPrimitiveHandler {
    std::function<VMPrimitiveExitCode(VMStackFrame*)> Body;
    explicit Handler(std::function<VMPrimitiveExitCode(VMStackFrame*)> body):Body(std::move(body)){}
    VMPrimitiveExitCode Execute(VMStackFrame* frame,VMPrimitiveOperand*)override{return Body(frame);}
};
struct Fixture {
    IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"fresh-lifecycle",true};
    FSO::Content::Content* Content=FSO::Content::Content::Get();
    std::unique_ptr<FSO::Content::WorldObjectProvider> Previous=std::move(Content->WorldObjects);
    VMContext Context{nullptr};VM Machine{&Context,nullptr};
    std::function<VMPrimitiveExitCode(VMStackFrame*)> Init,Main;
    int InitCount=0,MainCount=0;
    std::vector<int> Order;
    explicit Fixture(bool ts1=true) {
        Content->WorldObjects=std::make_unique<FSO::Content::WorldObjectProvider>(Content);
        Machine.TS1=ts1;Machine.GlobalState.assign(64,0);
        Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
        Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);
        Context.Clock->Ticks=232560;
        for(auto entry:{std::make_pair(200,4096),std::make_pair(201,4097)}) {
            auto bhav=new BHAV();bhav->ChunkProcessed=true;bhav->ChunkID=entry.second;bhav->Args=4;
            BHAVInstruction instruction;instruction.Opcode=entry.first;
            instruction.TruePointer=254;instruction.FalsePointer=255;
            bhav->Instructions.push_back(instruction);Iff.AddChunk(bhav);
        }
        auto add=[&](int opcode,std::function<VMPrimitiveExitCode(VMStackFrame*)> body) {
            auto registration=new VMPrimitiveRegistration(new Handler(std::move(body)));
            registration->Opcode=opcode;Context.AddPrimitive(registration);
        };
        add(200,[&](VMStackFrame* frame){++InitCount;Order.push_back(frame->Caller->ObjectID*10);
            return Init?Init(frame):VMPrimitiveExitCode::GOTO_TRUE;});
        add(201,[&](VMStackFrame* frame){++MainCount;Order.push_back(frame->Caller->ObjectID*10+1);
            return Main?Main(frame):VMPrimitiveExitCode::CONTINUE_NEXT_TICK;});
    }
    ~Fixture(){Content->WorldObjects=std::move(Previous);}
    OBJD* define(uint32_t guid,uint16_t id=100,uint16_t master=0,int16_t part=0) {
        auto definition=new OBJD();definition->ChunkProcessed=true;definition->ChunkID=id;
        definition->GUID=guid;definition->ObjectType=OBJDType::Normal;definition->StackSize=16;
        definition->BHAV_Init=4096;definition->BHAV_MainID=4097;definition->Price=100;
        definition->MasterID=master;definition->SubIndex=part;Iff.AddChunk(definition);
        auto object=std::make_unique<FSO::Content::GameObject>();object->GUID=guid;
        object->OBJ=definition;object->Resource=&Resource;
        (Content->WorldObjects.get()->*field(CacheField{}))[guid]=std::move(object);return definition;
    }
    Entities::VMMultitileGroup* create(uint32_t guid) {
        return Context.CreateObjectInstance(guid,LotTilePos::OUT_OF_WORLD,Direction::NORTH,Machine.TS1);
    }
};

static void singleAndNestedCreation() {
    Fixture f;f.define(0xf0200001);f.define(0xf0200002,101);
    f.Context.RandomSeed=3784694001u;f.Context.TS1SimulationRandomSeed=1918150980u;
    f.Init=[&](VMStackFrame* frame) {
        auto& thread=*frame->Thread;
        need(&thread==frame->Caller->Thread.get()&&!thread.IsCheck,"Init ran on a cloned check machine");
        need(thread.Stack.size()==3&&thread.Stack.front()->Routine->ID==4097&&
            thread.Stack.front()->InstructionPointer==0,"Main was not prepared below Init's sentinel");
        thread.TempRegisters[3]=77;
        if(frame->Caller->Object->OBJ->GUID==0xf0200001) {
            need(f.Context.NextRandom(97)==2,"fresh Init shared RNG result differs");
            auto child=f.create(0xf0200002);
            need(child&&child->Objects.size()==1&&f.MainCount==1,"nested fresh child did not finish its initial Main");
            need(thread.Stack.size()==3&&thread.TempRegisters[3]==77,"nested creation corrupted parent Init");
        }
        return VMPrimitiveExitCode::GOTO_FALSE; // false Init does not suppress Main
    };
    f.Main=[&](VMStackFrame* frame) {
        need(frame->Thread==frame->Caller->Thread.get()&&frame->Thread->TempRegisters[3]==77,
            "Init temporaries did not reach the real Main machine");
        need(!f.Machine.EntityTickRunning(),"factory fabricated an owner dispatch scope");
        need(frame->Caller->GetValue(Model::VMStackObjectVariable::CurrentValue)==0,
            "single factory repriced the object before initial Main");
        frame->Caller->SetAttribute(0,23);return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    };
    auto group=f.create(0xf0200001);need(group&&f.InitCount==2&&f.MainCount==2,"fresh creation did not run Init and Main once");
    auto object=group->BaseObject();need(object->Thread->Stack.size()==1&&
        object->Thread->Stack.front()->Routine->ID==4097,"initializer leaked its sentinel or replaced Main");
    need(object->GetAttribute(0)==23&&object->GetValue(Model::VMStackObjectVariable::CurrentValue)==100,
        "initial Main write or final factory price was lost");
    need(f.Context.Clock->Ticks==232560&&f.Context.TS1SimulationRandomSeed==1918150980u&&
        f.Context.RandomSeed==2957835478u,"fresh lifecycle advanced time or selected the wrong RNG stream");
}

static void groupPhases() {
    Fixture f;f.define(0xf0200010,100,7,-1);f.define(0xf0200011,101,7,0);f.define(0xf0200012,102,7,1);
    f.Init=[&](VMStackFrame* frame) {
        auto group=frame->Caller->MultitileGroup;need(group->Objects.size()==2,"group membership missing during Init");
        for(auto part:group->Objects)need(part->Thread&&!part->Thread->Stack.empty()&&
            part->Thread->Stack.front()->Routine->ID==4097,"a sibling Main was not prepared before Init");
        return VMPrimitiveExitCode::GOTO_TRUE;
    };
    auto group=f.create(0xf0200010);need(group&&f.InitCount==2&&f.MainCount==2,"group fresh phases skipped a part");
    auto first=group->Objects[0]->ObjectID,second=group->Objects[1]->ObjectID;
    need(f.Order==std::vector<int>({first*10,first*10+1,second*10,second*10+1}),
        "group did not run each part Init then initial Main in part order");
}

static void blockingInitRestoresMain() {
    Fixture f;f.define(0xf0200020);
    auto object=f.Content->WorldObjects->Get(0xf0200020,true);
    VMGameObject entity(object,nullptr);Entities::VMMultitileGroup group;
    entity.MultitileGroup=&group;group.AddObject(&entity);entity.PrepareTS1Fresh(&f.Context);
    auto main=entity.Thread->Stack.front();entity.Thread->TicksThisFrame=31;
    entity.Thread->TempRegisters[1]=12;
    f.Init=[](VMStackFrame* frame) {frame->Thread->TempRegisters[1]=45;return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;};
    bool rejected=false;try{entity.CompleteTS1Fresh(&f.Context);}catch(const std::runtime_error& e){
        rejected=std::string(e.what()).find("attempted to suspend")!=std::string::npos;
    }
    need(rejected&&f.MainCount==0,"blocking Init was silently ticked until completion");
    need(entity.Thread->Stack.size()==1&&entity.Thread->Stack.front()==main&&main->InstructionPointer==0&&
        entity.Thread->TicksThisFrame==31&&entity.Thread->TempRegisters[1]==45,
        "rejected Init did not restore Main/bookkeeping while retaining executed writes");
    f.Init=[](VMStackFrame* frame)->VMPrimitiveExitCode {frame->Thread->TempRegisters[1]=56;throw std::runtime_error("authored initializer failure");};
    rejected=false;try{entity.CompleteTS1Fresh(&f.Context);}catch(const std::runtime_error& e){rejected=std::string(e.what())=="authored initializer failure";}
    need(rejected&&entity.Thread->Stack.size()==1&&entity.Thread->Stack.front()==main&&
        entity.Thread->TicksThisFrame==31&&entity.Thread->TempRegisters[1]==56,
        "throwing Init corrupted its prepared Main or swallowed the failure");
    // Real Sleep implements the source raw2 -> dispatcher1 yield boundary.
    // It may execute its authored decrement, but an immediate Init must not
    // keep ticking the argument down offscreen until the wait finishes.
    VMRoutine sleep;sleep.Instructions.resize(1);sleep.Instructions[0].Opcode=0;
    sleep.Instructions[0].TruePointer=254;sleep.Instructions[0].FalsePointer=255;
    sleep.Instructions[0].Operand=std::make_unique<Primitives::VMSleepOperand>();
    auto frame=std::make_shared<VMStackFrame>();frame->Caller=&entity;frame->Callee=&entity;
    frame->CodeOwner=object;frame->Routine=&sleep;frame->Args={3,0,0,0};
    rejected=false;try{entity.Thread->RunTS1Initializer(frame);}catch(const std::runtime_error& e){
        rejected=std::string(e.what()).find("attempted to suspend")!=std::string::npos;
    }
    need(rejected&&frame->Args[0]==2&&entity.Thread->Stack.size()==1&&entity.Thread->Stack.front()==main&&
        entity.Thread->TicksThisFrame==31,"real blocking Sleep did not reject at its first boundary and restore Main");
    f.Init=[](VMStackFrame*){return VMPrimitiveExitCode::GOTO_TRUE;};
    entity.CompleteTS1Fresh(&f.Context);need(f.MainCount==1,"initializer failure leaked its execution mode");
}

static void creationInOwnerDispatch() {
    Fixture f;f.define(0xf0200040);f.define(0xf0200041,101);
    f.Context.RandomSeed=3784694001u;f.Context.TS1SimulationRandomSeed=1918150980u;
    struct Spawner:VMGameObject {
        std::function<void()> Spawn;explicit Spawner(FSO::Content::GameObject* object):VMGameObject(object,nullptr){}
        void Tick()override{if(Spawn){auto once=std::move(Spawn);once();}}
    };
    auto parent=static_cast<Spawner*>(f.Machine.OwnEntity(new Spawner(f.Content->WorldObjects->Get(0xf0200040,true))));
    auto parentGroup=f.Machine.OwnGroup(new Entities::VMMultitileGroup());parent->MultitileGroup=parentGroup;
    parentGroup->AddObject(parent);f.Machine.AddEntity(parent);
    f.Init=[&](VMStackFrame* frame){
        need(f.Machine.EntityTickRunning()&&!frame->Thread->IsCheck,"nested fresh Init lost its active owner scope");
        need(f.Context.NextRandom(97)==13,"nested fresh Init did not consume the owner stream");
        return VMPrimitiveExitCode::GOTO_TRUE;
    };
    f.Main=[&](VMStackFrame*){need(f.Machine.EntityTickRunning(),"nested initial Main lost its owner scope");return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;};
    parent->Spawn=[&]{need(f.create(0xf0200041)!=nullptr,"fresh create inside admitted Tick failed");};
    f.Machine.Ready=false;f.Machine.Update();
    need(f.InitCount==1&&f.MainCount==1&&f.Context.Clock->Ticks==232561,
        "new child was ticked twice in its creator's sweep or global time advanced during creation");
    need(!f.Machine.EntityTickRunning()&&f.Context.TS1SimulationRandomSeed==2822175277u&&
        f.Context.RandomSeed==3784694001u,"nested creation leaked dispatch scope or changed shared RNG");
}

static void groupMutationAndSelfDeletion() {
    {
        Fixture f;f.define(0xf0200050,100,7,-1);f.define(0xf0200051,101,7,0);f.define(0xf0200052,102,7,1);f.define(0xf0200053,103);
        f.Init=[&](VMStackFrame* frame){
            if(frame->Caller->Object->OBJ->GUID==0xf0200051){
                auto group=frame->Caller->MultitileGroup;
                group->Objects[1]->Delete(false,&f.Context);
                auto child=f.create(0xf0200053)->BaseObject();
                child->MultitileGroup->RemoveObject(child);group->AddObject(child);child->MultitileGroup=group;
            }
            return VMPrimitiveExitCode::GOTO_TRUE;
        };
        auto group=f.create(0xf0200050);
        need(group&&group->Objects.size()==2&&f.InitCount==2&&f.MainCount==2,
            "group mutation replayed a transferred child's Init/Main or ticked a deleted sibling");
    }
    need(VM::ReleaseAssembled(),"mutation fixture retained its VM");
    {
        Fixture f;f.define(0xf0200060);
        f.Init=[&](VMStackFrame* frame){frame->Caller->Delete(false,&f.Context);return VMPrimitiveExitCode::GOTO_TRUE;};
        auto group=f.create(0xf0200060);
        need(group&&group->Objects.empty()&&f.InitCount==1&&f.MainCount==0,"self-deleted fresh object still ran Main");
    }
}

static void realNestedSubroutines() {
    // A normal false return remains distinct from a missing callee fault.
    for(bool returnTrue:{false,true}) {
        {
            Fixture f;f.define(0xf0200070);
            auto parent=f.Iff.Get<BHAV>(4096);parent->Instructions[0].Opcode=4098;
            auto child=new BHAV();child->ChunkProcessed=true;child->ChunkID=4098;child->Args=4;
            BHAVInstruction instruction;instruction.Opcode=200;instruction.TruePointer=254;instruction.FalsePointer=255;
            child->Instructions.push_back(instruction);f.Iff.AddChunk(child);
            f.Init=[&](VMStackFrame* frame){
                need(frame->Routine->ID==4098&&frame->Thread->Stack.size()==4&&
                    frame->Thread->Stack[2]->Routine->ID==4096,
                    "real nested Init call lost its caller or sentinel");
                frame->Thread->TempRegisters[3]=117;
                return returnTrue?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_FALSE;
            };
            f.Main=[](VMStackFrame* frame){
                need(frame->Thread->Stack.size()==1&&frame->Thread->TempRegisters[3]==117,
                    "normal nested return lost Init temporaries or changed Main");
                return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
            };
            auto group=f.create(0xf0200070);
            need(group&&f.InitCount==1&&f.MainCount==1,"normal nested true/false Init did not continue to Main");
        }
        need(VM::ReleaseAssembled(),"nested-return fixture retained its VM");
    }
    Fixture f;f.define(0xf0200071);
    f.Iff.Get<BHAV>(4096)->Instructions[0].Opcode=4098; // no such private BHAV
    bool rejected=false;try{f.create(0xf0200071);}catch(const std::runtime_error& e){
        rejected=std::string(e.what())=="TS1 initializer interpreter error";
    }
    need(rejected&&f.InitCount==0&&f.MainCount==0,"missing nested Init callee was treated as an authored false return");
    need(f.Machine.Entities.size()==1&&f.Machine.Entities[0]->Thread->Stack.size()==1&&
        f.Machine.Entities[0]->Thread->Stack.front()->Routine->ID==4097&&
        f.Machine.Entities[0]->Thread->Stack.front()->InstructionPointer==0,
        "missing nested callee corrupted the prepared Main during exception cleanup");
}

static void realRemovalDuringInit() {
    for(bool self:{true,false}) {
        {
            Fixture f;f.define(0xf0200080);
            auto parent=f.Iff.Get<BHAV>(4096);parent->Instructions[0].Opcode=4098;
            auto child=new BHAV();child->ChunkProcessed=true;child->ChunkID=4098;child->Args=4;
            BHAVInstruction remove;remove.Opcode=18;remove.TruePointer=1;remove.FalsePointer=2;
            Primitives::VMRemoveObjectInstanceOperand operand;operand.Target=self?0:1;
            operand.Write(remove.Operand.data());child->Instructions.push_back(remove);
            BHAVInstruction branch;branch.Opcode=200;branch.TruePointer=254;branch.FalsePointer=255;
            child->Instructions.push_back(branch);child->Instructions.push_back(branch);f.Iff.AddChunk(child);
            f.Init=[&](VMStackFrame* frame){
                need(frame->Routine->ID==4098&&frame->InstructionPointer==(self?1:2),
                    "real nested Remove did not follow its original true/false edge");
                need(frame->Thread->TS1InitializerRunning()&&!frame->Thread->IsCheck,
                    "real removal lost its immediate Init execution mode");
                need(!frame->Caller->Dead&&f.Machine.GetObjectById(frame->Caller->ObjectID)==frame->Caller,
                    "self-removing Init deleted its live identity before its authored return");
                need(f.Machine.HasTS1PendingRemovals()==self,"immediate removal request was not queued exactly when a target existed");
                frame->Thread->TempRegisters[4]=119;return VMPrimitiveExitCode::GOTO_TRUE;
            };
            f.Main=[&](VMStackFrame* frame){
                need(!frame->Thread->TS1InitializerRunning()&&frame->Thread->TempRegisters[4]==119,
                    "normal initial Main retained Init mode or lost post-removal writes");
                need(!frame->Caller->Dead,"initial Main observed an already destroyed queued identity");
                return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
            };
            auto group=f.create(0xf0200080);auto object=group->BaseObject();
            need(object&&f.InitCount==1&&f.MainCount==1&&f.Context.Clock->Ticks==232560,
                "fresh real self-removal interrupted load or advanced time before the first sweep");
            if(self) {
                need(f.Machine.CanCaptureTS1RemovalState()&&
                    f.Machine.GetTS1PendingRemovalIDs()==std::vector<int16_t>({object->ObjectID}),
                    "completed Init did not retain a quiescent serializable removal request");
                const auto oid=object->ObjectID;f.Machine.InternalTick();
                need(object->Dead&&!f.Machine.GetObjectById(oid)&&!f.Machine.HasTS1PendingRemovals()&&
                    f.Context.Clock->Ticks==232561,"first normal sweep did not drain the queued Init removal");
            } else need(!f.Machine.HasTS1PendingRemovals(),"missing target removal queued a phantom identity");
        }
        need(VM::ReleaseAssembled(),"removal Init fixture retained its VM");
    }
    // Outside immediate Init, bit0 alone controls the self-removal yield.
    for(bool immediately:{false,true}) {
        {
            Fixture f;f.define(0xf0200081);auto group=f.create(0xf0200081);auto object=group->BaseObject();
            f.Main=[&](VMStackFrame* frame){
                Primitives::VMRemoveObjectInstance handler;Primitives::VMRemoveObjectInstanceOperand operand;
                operand.Target=0;operand.ReturnImmediately(immediately);
                const auto result=handler.Execute(frame,&operand);
                need(result==(immediately?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_TRUE_NEXT_TICK),
                    "ordinary self-removal lost its source flag0 yield policy");
                need(!object->Dead&&f.Machine.HasTS1PendingRemovals(),"normal removal did not retain identity until drain");
                return result;
            };
            f.Machine.InternalTick();need(object->Dead&&!f.Machine.HasTS1PendingRemovals(),"flagged normal removal did not drain");
        }
        need(VM::ReleaseAssembled(),"flagged removal fixture retained its VM");
    }
}

static void realRemovalDuringInitialMain() {
    Fixture f;f.define(0xf0200082);
    auto main=f.Iff.Get<BHAV>(4097);main->Instructions.clear();
    BHAVInstruction remove;remove.Opcode=18;remove.TruePointer=1;remove.FalsePointer=2;
    Primitives::VMRemoveObjectInstanceOperand operand;operand.Target=0;operand.Write(remove.Operand.data());
    main->Instructions.push_back(remove);
    BHAVInstruction branch;branch.Opcode=201;branch.TruePointer=254;branch.FalsePointer=255;
    main->Instructions.push_back(branch);main->Instructions.push_back(branch);
    f.Main=[&](VMStackFrame* frame){
        need(frame->InstructionPointer==1&&f.Machine.TS1ScriptImmediateMode()&&!f.Machine.EntityTickRunning()&&
            !frame->Thread->TS1InitializerRunning(),"initial Main used the ordinary sweep's self-removal yield policy");
        need(!frame->Caller->Dead&&f.Machine.HasTS1PendingRemovals(),"initial Main removed its target inline");
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    };
    auto group=f.create(0xf0200082);auto object=group->BaseObject();
    need(object&&f.InitCount==1&&f.MainCount==1&&f.Context.Clock->Ticks==232560&&
        f.Machine.GetTS1PendingRemovalIDs()==std::vector<int16_t>({object->ObjectID}),
        "initial Main failed to execute its true branch before ordinary simulation began");
    f.Main=[](VMStackFrame*){return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;};
    f.Machine.InternalTick();need(object->Dead&&!f.Machine.HasTS1PendingRemovals(),
        "initial Main's deferred request did not drain at the next ordinary boundary");
}

static void nestedInitializerOverridesOwnerMode() {
    Fixture f;f.define(0xf0200090);auto childDefinition=f.define(0xf0200091,101);
    f.define(0xf0200092,102);childDefinition->BHAV_MainID=4099;
    auto childMain=new BHAV();childMain->ChunkProcessed=true;childMain->ChunkID=4099;childMain->Args=4;
    BHAVInstruction remove;remove.Opcode=18;remove.TruePointer=1;remove.FalsePointer=2;
    Primitives::VMRemoveObjectInstanceOperand operand;operand.Target=0;operand.Write(remove.Operand.data());
    childMain->Instructions.push_back(remove);
    BHAVInstruction branch;branch.Opcode=201;branch.TruePointer=254;branch.FalsePointer=255;
    childMain->Instructions.push_back(branch);childMain->Instructions.push_back(branch);f.Iff.AddChunk(childMain);
    struct Spawner:VMGameObject {
        std::function<void()> Spawn;explicit Spawner(FSO::Content::GameObject* object):VMGameObject(object,nullptr){}
        void Tick()override{if(Spawn){auto once=std::move(Spawn);once();}}
    };
    auto spawner=static_cast<Spawner*>(f.Machine.OwnEntity(new Spawner(f.Content->WorldObjects->Get(0xf0200092,true))));
    auto spawnerGroup=f.Machine.OwnGroup(new Entities::VMMultitileGroup());
    spawner->MultitileGroup=spawnerGroup;spawnerGroup->AddObject(spawner);f.Machine.AddEntity(spawner);
    VMEntity* child=nullptr;bool childBranch=false,parentMain=false;
    f.Init=[&](VMStackFrame* frame){
        if(frame->Caller->Object->OBJ->GUID==0xf0200090){
            need(f.Machine.EntityTickRunning()&&frame->Thread->TS1InitializerRunning()&&
                f.Machine.TS1ScriptImmediateMode(),"nested parent Init did not override the active sweep's mode");
            child=f.create(0xf0200091)->BaseObject();
            need(child&&childBranch&&!child->Dead&&frame->Thread->TS1InitializerRunning(),
                "nested child Main yielded early or destroyed its caller's Init context");
        }
        return VMPrimitiveExitCode::GOTO_TRUE;
    };
    f.Main=[&](VMStackFrame* frame){
        if(frame->Routine->ID==4099){
            childBranch=true;
            need(frame->InstructionPointer==1&&f.Machine.EntityTickRunning()&&
                !frame->Thread->TS1InitializerRunning()&&f.Machine.TS1ScriptImmediateMode(),
                "child Main failed to inherit the parent's global immediate override");
            need(!f.Machine.CanCaptureTS1RemovalState()&&!frame->Caller->Dead,
                "nested active Init exposed a quiescent save point or deleted the child inline");
        }else{
            parentMain=true;
            need(f.Machine.EntityTickRunning()&&!f.Machine.TS1ScriptImmediateMode(),
                "return from parent Init did not restore the sweep's ordinary mode");
        }
        return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;
    };
    spawner->Spawn=[&]{need(f.create(0xf0200090)!=nullptr,"parent creation inside owner sweep failed");};
    f.Machine.InternalTick();
    need(childBranch&&parentMain&&child&&child->Dead&&!f.Machine.HasTS1PendingRemovals()&&
        f.Context.Clock->Ticks==232561&&!f.Machine.EntityTickRunning(),
        "nested global mode was not restored or its queued child survived the ordinary drain");
}

static void sameMachineLoad() {
    Fixture f;f.define(0xf0200090);auto entity=f.create(0xf0200090)->BaseObject();auto& thread=*entity->Thread;
    auto main=thread.Stack.front();thread.TicksThisFrame=31;thread.TempRegisters[3]=9;thread.TempXL[1]=123456;
    auto action=std::make_shared<VMQueuedAction>();action->UID=77;action->Callee=entity;thread.Queue={action};thread.ActiveQueueBlock=0;
    const auto beforeInit=f.InitCount,beforeMain=f.MainCount;
    entity->EntryPoints[2].ActionFunction=0;
    need(!entity->ExecuteEntryPoint(2,&f.Context,true)&&f.InitCount==beforeInit,"missing Load replayed an entry tree");
    entity->EntryPoints[2].ActionFunction=4100;
    need(!entity->ExecuteEntryPoint(2,&f.Context,true)&&thread.Stack.front()==main,
         "unresolved Load routine disturbed prepared Main");
    entity->EntryPoints[2].ActionFunction=4096;
    int depth=0;
    f.Init=[&](VMStackFrame* frame){
        need(frame->Thread==&thread&&!thread.IsCheck&&thread.TS1ImmediateCallRunning()&&!thread.TS1InitializerRunning(),"Load used a check/fresh machine instead of its immediate context");
        need(frame->Args.size()==4&&frame->Args[3]==thread.TempRegisters[3],"Load null arguments did not copy the existing temporary bank");
        need(thread.Stack.front()==main&&main->InstructionPointer==0&&thread.ActiveAction()==action&&thread.Queue.size()==1,"Load replaced prepared Main or its active queue");
        need(!f.Machine.CanCaptureTS1RemovalState(),"capture admitted an incomplete synchronous callback");
        thread.TempRegisters[3]+=1;thread.TempXL[1]+=2;
        if(depth++==0){need(entity->ExecuteEntryPoint(2,&f.Context,true),"nested same-machine Load returned false");need(thread.Stack.size()==3&&thread.TS1ImmediateCallRunning(),"nested Load did not restore outer sentinel");}
        return VMPrimitiveExitCode::GOTO_TRUE;
    };
    need(entity->ExecuteEntryPoint(2,&f.Context,true),"normal Load true result lost");
    need(thread.Stack.size()==1&&thread.Stack.front()==main&&thread.TicksThisFrame==31&&thread.Queue.size()==1&&thread.ActiveAction()==action,"Load failed to restore Main/bookkeeping/queue");
    need(thread.TempRegisters[3]==11&&thread.TempXL[1]==123460&&f.MainCount==beforeMain&&!thread.TS1SynchronousCallRunning(),"Load lost shared temporaries or ticked Main");
    bool rejected=false;auto frame=std::make_shared<VMStackFrame>();try{thread.RunTS1Initializer(frame);}catch(const std::runtime_error&){rejected=true;}
    need(rejected,"generic Load weakened fresh Init queue guard");
    f.Init=[](VMStackFrame* frame)->VMPrimitiveExitCode{frame->Thread->TempRegisters[3]=44;throw std::runtime_error("authored Load failure");};
    rejected=false;try{entity->ExecuteEntryPoint(2,&f.Context,true);}catch(const std::runtime_error& e){rejected=std::string(e.what())=="authored Load failure";}
    need(rejected&&thread.Stack.size()==1&&thread.Stack.front()==main&&thread.TempRegisters[3]==44&&thread.ActiveAction()==action&&!thread.TS1SynchronousCallRunning(),"throwing Load lost prepared state or swallowed authored writes");
    f.Init=[](VMStackFrame*){return VMPrimitiveExitCode::CONTINUE_NEXT_TICK;};
    rejected=false;try{entity->ExecuteEntryPoint(2,&f.Context,true);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("attempted to suspend")!=std::string::npos;}
    need(rejected&&thread.Stack.size()==1&&thread.Stack.front()==main&&thread.TicksThisFrame==31&&thread.ActiveAction()==action&&!thread.TS1SynchronousCallRunning(),"suspending Load crossed ordinary tick boundary or leaked sentinel");
    f.Init=[](VMStackFrame*){return VMPrimitiveExitCode::GOTO_FALSE;};need(!entity->ExecuteEntryPoint(2,&f.Context,true),"Load false result became true");
    VMRoutine sleep;sleep.Instructions.resize(1);sleep.Instructions[0].Opcode=0;
    sleep.Instructions[0].TruePointer=254;sleep.Instructions[0].FalsePointer=255;
    sleep.Instructions[0].Operand=std::make_unique<Primitives::VMSleepOperand>();
    auto waiting=std::make_shared<VMStackFrame>();waiting->Caller=entity;waiting->Callee=entity;
    waiting->CodeOwner=entity->Object;waiting->Routine=&sleep;waiting->Args={3,0,0,0};
    rejected=false;try{thread.RunTS1Load(waiting);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("attempted to suspend")!=std::string::npos;}
    need(rejected&&waiting->Args[0]==2&&thread.Stack.size()==1&&thread.Stack.front()==main&&
         thread.ActiveAction()==action&&f.Context.Clock->Ticks==232560,
         "Load ticked through a real Sleep boundary or damaged live Main/action state");
}

static void loadArgumentBank() {
    Fixture f;f.define(0xf0200092);auto entity=f.create(0xf0200092)->BaseObject();auto& thread=*entity->Thread;
    entity->EntryPoints[2].ActionFunction=4096;auto main=thread.Stack.front();auto* routine=f.Machine.Assemble(f.Iff.Get<BHAV>(4096));
    routine->Arguments=6;thread.TempRegisters[0]=70;thread.TempRegisters[3]=73;thread.TempRegisters[5]=75;
    std::vector<int16_t> expected(thread.TempRegisters.begin(),thread.TempRegisters.begin()+6);
    f.Init=[&](VMStackFrame* frame){need(frame->Args==expected,"declared Load arguments beyond four were truncated or replaced");return VMPrimitiveExitCode::GOTO_TRUE;};
    need(entity->ExecuteEntryPoint(2,&f.Context,true),"six-argument Load failed");
    const std::vector<int16_t> explicitArgs={90,91,92,93,94,95};expected=explicitArgs;
    need(entity->ExecuteEntryPoint(2,&f.Context,true,nullptr,&explicitArgs),"explicit Load argument bank was replaced by temporaries");
    routine->Arguments=21;bool rejected=false;auto before=f.InitCount;
    try{entity->ExecuteEntryPoint(2,&f.Context,true);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("argument bank")!=std::string::npos;}
    need(rejected&&f.InitCount==before&&thread.Stack.size()==1&&thread.Stack.front()==main&&thread.TempRegisters[3]==73&&!thread.TS1SynchronousCallRunning(),"oversized Load argument declaration changed Main or executed a partial callback");
}

static void loadRemovalAndLiveQueueMutation() {
    Fixture f;f.define(0xf0200091);auto entity=f.create(0xf0200091)->BaseObject();auto& thread=*entity->Thread;
    entity->EntryPoints[2].ActionFunction=4096;auto main=thread.Stack.front();
    auto existing=std::make_shared<VMQueuedAction>();existing->UID=17;thread.Queue={existing};
    auto appended=std::make_shared<VMQueuedAction>();appended->UID=18;
    f.Init=[&](VMStackFrame* frame){
        frame->Thread->Queue.push_back(appended);Primitives::VMRemoveObjectInstance remove;Primitives::VMRemoveObjectInstanceOperand operand;
        operand.Target=0;operand.Flags=0;auto result=remove.Execute(frame,&operand);
        need(result==VMPrimitiveExitCode::GOTO_TRUE&&!entity->Dead&&f.Machine.HasTS1PendingRemovals(),"Load self-removal suspended or deleted inline");
        return result;
    };
    need(entity->ExecuteEntryPoint(2,&f.Context,true),"Load self-removal did not complete its sentinel");
    need(!entity->Dead&&thread.Stack.size()==1&&thread.Stack.front()==main&&thread.Queue==std::vector<std::shared_ptr<VMQueuedAction>>({existing,appended}),"Load rolled back authored queue mutation or lost Main");
    need(f.Machine.GetTS1PendingRemovalIDs()==std::vector<int16_t>({entity->ObjectID})&&!thread.TS1SynchronousCallRunning(),"Load removal request was lost after immediate return");
}

static void restoredLoadPhaseOrder() {
    Fixture f;f.Machine.TS1ObjectManagerOrderKnown=true;
    for(uint32_t i=0;i<5;++i)f.define(0xf02000a0+i,uint16_t(100+i));
    auto first=f.create(0xf02000a0)->BaseObject();auto second=f.create(0xf02000a1)->BaseObject();
    auto person=f.create(0xf02000a2)->BaseObject();auto missing=f.create(0xf02000a3)->BaseObject();
    person->Object->OBJ->ObjectType=OBJDType::Person;
    for(auto* entity:{first,second,person})entity->EntryPoints[2].ActionFunction=4096;
    missing->EntryPoints[2].ActionFunction=0;
    std::vector<std::pair<VMEntity*,std::shared_ptr<VMStackFrame>>> mains;
    for(auto* entity:{first,second,person,missing}){
        mains.emplace_back(entity,entity->Thread->Stack.front());auto action=std::make_shared<VMQueuedAction>();
        action->UID=uint16_t(entity->ObjectID);action->Callee=entity;entity->Thread->Queue={action};entity->Thread->ActiveQueueBlock=0;
    }
    need(f.Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>({missing,person,second,first}),"Load fixture birth order was sorted by object ID");
    std::vector<int16_t> order;VMEntity* child=nullptr;
    f.Init=[&](VMStackFrame* frame){
        if(frame->Thread->TS1InitializerRunning())return VMPrimitiveExitCode::GOTO_TRUE;
        order.push_back(frame->Caller->ObjectID);
        if(frame->Caller==second){child=f.create(0xf02000a4)->BaseObject();child->EntryPoints[2].ActionFunction=4096;}
        if(frame->Caller==first){Primitives::VMRemoveObjectInstance remove;Primitives::VMRemoveObjectInstanceOperand operand;operand.Target=0;operand.Flags=0;return remove.Execute(frame,&operand);}
        return VMPrimitiveExitCode::GOTO_TRUE;
    };
    VMWorldActivator::RunTS1LoadCallbacks(&f.Machine);
    need(order==std::vector<int16_t>({second->ObjectID,first->ObjectID}),"Load phase missed birth order, skipped person/missing entry incorrectly, or revisited a created head");
    need(child&&f.Machine.TS1ObjectManagerOrder.front()==child&&!first->Dead&&f.Machine.GetTS1PendingRemovalIDs()==std::vector<int16_t>({first->ObjectID}),"Load phase drained removal or lost newly created manager head");
    for(auto& saved:mains)need(saved.first->Thread->Stack.size()==1&&saved.first->Thread->Stack.front()==saved.second&&saved.first->Thread->ActiveQueueBlock==0&&saved.first->Thread->Queue.size()==1,"restored Load phase replaced prepared Main/action state");
    need(f.Context.Clock->Ticks==232560,"Load phase advanced simulation time");
    f.Machine.Ready=false;f.Machine.Update();
    need(first->Dead&&!f.Machine.HasTS1PendingRemovals()&&f.Context.Clock->Ticks==232561,"Load removal did not wait for the first ordinary sweep");
}

static void legacyAndRestoreBoundaries() {
    Fixture f(false);f.define(0xf0200030);
    f.Init=[](VMStackFrame* frame) {need(frame->Thread->IsCheck,"TS1 initializer mode leaked into TSO");return VMPrimitiveExitCode::GOTO_TRUE;};
    auto group=f.create(0xf0200030);need(group&&f.InitCount==1&&f.MainCount==0,"TSO creation acquired a fresh Main tick");
    f.Machine.TS1=true;
    auto object=f.Content->WorldObjects->Get(0xf0200030,true);
    VMGameObject restored(object,nullptr);Entities::VMMultitileGroup restoredGroup;
    restored.MultitileGroup=&restoredGroup;restoredGroup.AddObject(&restored);
    restored.PrepareForRestore(&f.Context);restored.SetAttribute(0,99);
    need(f.InitCount==1&&f.MainCount==0&&restored.Thread->Stack.empty()&&restored.GetAttribute(0)==99,
        "saved-instance preparation replayed fresh Init/Main");
    f.Init=[](VMStackFrame* frame) {need(frame->Thread->IsCheck,"preview acquired real initialization mode");return VMPrimitiveExitCode::GOTO_TRUE;};
    auto preview=f.Context.CreateObjectInstance(0xf0200030,LotTilePos::OUT_OF_WORLD,Direction::NORTH,true,true);
    need(preview&&preview->BaseObject()->GhostImage&&f.MainCount==0,"ghost preview acquired an initial Main tick");
}

int main(){try{
    VM::UseWorld(false);
    for(auto run:{singleAndNestedCreation,groupPhases,blockingInitRestoresMain,creationInOwnerDispatch,
                 groupMutationAndSelfDeletion,realNestedSubroutines,realRemovalDuringInit,
                 realRemovalDuringInitialMain,nestedInitializerOverridesOwnerMode,sameMachineLoad,loadArgumentBank,loadRemovalAndLiveQueueMutation,restoredLoadPhaseOrder,legacyAndRestoreBoundaries}){
        run();need(VM::ReleaseAssembled(),"fixture leaked its VM into routine cache cleanup");
    }
    std::printf("PASS %d TS1 fresh-object lifecycle checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
