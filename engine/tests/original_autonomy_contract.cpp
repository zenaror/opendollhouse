// SPDX-License-Identifier: MPL-2.0
// Synthetic resources exercise the real TS1 chooser without a game installation.
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMQueuedAction.h"
#include "simantics/engine/VMSimanticsException.h"
#include "simantics/primitives/VMFindBestAction.h"
#include "simantics/primitives/VMTS1MiscPrimitives.h"
#include "simantics/primitives/VMRandomNumber.h"
#include "simantics/primitives/VMExpression.h"
#include "files/formats/iff/chunks/TTAs.h"
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static int checks=0;
static int failures=0;
static void Require(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
static void Scenario(const std::string& name,const std::function<void()>& run){
    try{run();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
}
// Assembled routines are process-wide and keyed by BHAV address. Clear them
// after the fixture VM dies, before another fixture can reuse freed addresses.
struct AssemblyCacheLifetime { ~AssemblyCacheLifetime(){VM::ReleaseAssembled();} };
struct Fixture {
    AssemblyCacheLifetime assemblyLifetime;
    VMContext context{nullptr}; VM vm{&context,nullptr};
    IffFile iff;
    FSO::Content::GameObjectResource resource{&iff,nullptr,nullptr,"autonomy-fixture",true};
    OBJD definition;
    FSO::Content::GameObject object;
    std::unique_ptr<VMAvatar> actor;
    std::unique_ptr<VMGameObject> target;
    Entities::VMMultitileGroup actorGroup,targetGroup;
    TTAB table; TTAs strings;
    std::vector<int> order;
    VMStackFrame frame;
    Fixture(){
        vm.GlobalState.resize(64,0);
        definition.GUID=0x12345678;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
        actor=std::make_unique<VMAvatar>(&object);target=std::make_unique<VMGameObject>(&object,nullptr);
        actor->ObjectID=1;target->ObjectID=2;
        actor->MultitileGroup=&actorGroup;target->MultitileGroup=&targetGroup;
        actorGroup.AddObject(actor.get());targetGroup.AddObject(target.get());
        actor->Position(FSO::LotView::Model::LotTilePos(64,64,1));
        target->Position(FSO::LotView::Model::LotTilePos(80,64,1));
        actor->Thread=std::make_unique<VMThread>(&context,actor.get(),16);
        target->Thread=std::make_unique<VMThread>(&context,target.get(),16);
        actor->SetPersonData(VMPersonDataVariable::PersonsAge,30);
        actor->SetPersonData(VMPersonDataVariable::AutonomyLevel,50);
        target->TreeTable=&table;target->TreeTableStrings=&strings;vm.Entities={target.get()};
        frame.Thread=actor->Thread.get();frame.Caller=actor.get();frame.Callee=actor.get();frame.CodeOwner=&object;
        auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();global->CurvesLoaded=true;
        global->InteractionScore.assign(9,FSO::Common::TS1::TS1Curve("(-100;-100) (100;100)"));
        global->InteractionScoreChild=global->InteractionScore;
        global->Autonomy.FamilyMinimum=global->Autonomy.VisitorMinimum=0.0000001f;
        global->Autonomy.SittingMinimum=0.000001f;global->Autonomy.RandomSelectionCount=4;
        vm.OnTraceInstruction=[this](VMStackFrame* f,uint16_t,int){if(f->Thread->IsCheck)order.push_back(f->Routine->ID);};
    }
    void Add(int id,int threshold=50,int ad=9000,bool randomCheck=false,bool allowed=true){
        auto action=new BHAV();action->ChunkID=4096+id;action->Args=4;action->ChunkProcessed=true;
        BHAVInstruction instruction;instruction.Opcode=0;instruction.TruePointer=instruction.FalsePointer=254;
        action->Instructions.push_back(instruction);iff.AddChunk(action);
        auto check=new BHAV();check->ChunkID=4200+id;check->Args=4;check->ChunkProcessed=true;
        // One random draw per evaluated row makes traversal order observable.
        instruction.Opcode=8;
        VMRandomNumberOperand random;random.DestinationScope=Scopes::VMVariableScope::Temps;
        random.DestinationData=0;random.RangeScope=Scopes::VMVariableScope::Literal;random.RangeData=id+11;
        random.Write(instruction.Operand.data());
        if(!randomCheck){
            // Autonomous checks receive parameter0=1. Sleep on that parameter
            // would yield and execute twice, obscuring the traversal trace.
            instruction.Opcode=2;VMExpressionOperand equal;equal.Operator=VMExpressionOperator::Equals;
            equal.LhsOwner=equal.RhsOwner=Scopes::VMVariableScope::Literal;
            equal.Write(instruction.Operand.data());
        }
        instruction.TruePointer=allowed?254:255;
        check->Instructions.push_back(instruction);iff.AddChunk(check);
        auto row=std::make_unique<TTABInteraction>();row->TTAIndex=id;row->ActionFunction=4096+id;row->TestFunction=4200+id;
        row->AutonomyThreshold=threshold;row->MotiveEntries.resize(16);row->MotiveEntries[7].EffectRangeMinimum=ad;
        table.InteractionByIndex[id]=row.get();table.Interactions.push_back(std::move(row));table.InitAutoInteractions();
        STRItem label;label.Value="row"+std::to_string(id);strings.AppendString(label);
    }
    VMPrimitiveExitCode Choose(uint64_t seed=1234){actor->Thread->Queue.clear();order.clear();context.RandomSeed=seed;return VMFindBestAction().Execute(&frame,nullptr);}
};
// Inspect the score produced by the actual chooser, including candidates that
// fail its positive-score admission. Restore the process-wide observer on exit.
static std::pair<VMPrimitiveExitCode,float> ObserveChooserScore(Fixture& fixture){
    struct ObserverLifetime {
        decltype(VMFindBestAction::DebugCandidate) previous=VMFindBestAction::DebugCandidate;
        ~ObserverLifetime(){VMFindBestAction::DebugCandidate=previous;}
    } observer;
    int seen=0;float actual=0;
    VMFindBestAction::DebugCandidate=[&](VMEntity* actor,VMEntity* target,const std::string&,bool allowed,float score){
        Require(actor==fixture.actor.get()&&target==fixture.target.get(),"score observation belongs to the fixture actors");
        Require(allowed,"the authored eligibility check passed before scoring");
        ++seen;actual=score;
    };
    const auto result=fixture.Choose();
    Require(seen==1,"the real chooser scored the single eligible TTAB row");
    return {result,actual};
}
static void RequireChooserScore(Fixture& fixture,float expected){
    const auto observation=ObserveChooserScore(fixture);
    const auto result=observation.first;const float actual=observation.second;
    Require(std::fabs(actual-expected)<0.00001f,"chooser score matches the literal DVD advertisement result");
    Require(result==(expected>0?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_FALSE),
        "the real chooser admits positive gains and excludes zero or negative gains");
}
static BHAVInstruction AssignCheckValue(Scopes::VMVariableScope scope,int index,int value,int next){
    BHAVInstruction instruction;instruction.Opcode=2;instruction.TruePointer=next;instruction.FalsePointer=255;
    VMExpressionOperand operand;operand.Operator=VMExpressionOperator::Assign;
    operand.LhsOwner=scope;operand.LhsData=index;operand.RhsOwner=Scopes::VMVariableScope::Literal;operand.RhsData=value;
    operand.Write(instruction.Operand.data());return instruction;
}
int main(){try{
    VM::UseWorld(false);
    Scenario("check-order-and-lockout",[]{
        Fixture f;f.Add(0,50,9000,true);f.Add(1,50,9000,true);f.Add(2,50,9000,true);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"real chooser must select a candidate");
        Require(f.order==std::vector<int>({4202,4201,4200}),"autonomous checks must traverse TTAB in reverse table order");
        auto after=f.context.RandomSeed;f.context.RandomSeed=1234;f.context.NextRandom(13);f.context.NextRandom(12);f.context.NextRandom(11);f.context.NextRandom(3);
        Require(f.context.RandomSeed==after,"three row checks precede the selection draw");
        Require(f.actor->Thread->Queue.empty() && f.frame.StackObject==f.target.get() &&
                f.actor->GetValue(VMStackObjectVariable::TreeTableEntry)>=0,
            "selection records its target and index without enqueueing an action");
        f.target->SetValue(VMStackObjectVariable::LockoutCount,1);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE&&f.order.empty(),"locked objects are rejected before checks");
    });
    for(bool bit5: {false,true})for(bool bit9: {false,true})for(bool joining: {false,true})for(bool allowed: {false,true})Scenario("flags b5="+std::to_string(bit5)+" b9="+std::to_string(bit9)+" join="+std::to_string(joining)+" allowed="+std::to_string(allowed),[=]{
        Fixture f;f.Add(0,50,9000,false,allowed);f.table.Interactions[0]->JoiningIndex=joining?0:-1;
        // Raw masks independently guard against confusing engine enum names.
        f.target->SetValue(VMStackObjectVariable::Flags,(bit5?0x20:0)|(bit9?0x200:0));
        Require(f.Choose()==(!bit9&&allowed?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_FALSE),
            "raw bit9 excludes a target; raw bit5 and join index leave authored eligibility in control");
        Require(f.order.empty()==bit9,"raw bit9 rejects before the authored check; bit5 does not skip checks");
    });
    Scenario("nine-candidate-ties",[]{
        Fixture f;for(int i=0;i<9;++i)f.Add(i);
        auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();global->Autonomy.RandomSelectionCount=9;
        std::vector<int> ranked;
        f.vm.OnAutonomyChoice=[&](VMEntity*,const std::vector<VMPieMenuInteraction>& candidates,const VMPieMenuInteraction&){
            ranked.clear();for(auto& candidate:candidates)ranked.push_back(candidate.ID);
        };
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"nine tied action candidates are selected");
        // Literal DVD qsort excerpt output indices [4,1,2,3,0,5,6,7,8],
        // applied to the reverse TTAB candidate order [8,7,6,5,4,3,2,1,0].
        Require(ranked==std::vector<int>({4,7,6,5,8,3,2,1,0}),"action ties preserve the DVD partition permutation above eight records");
    });
    for(int personType: {0,1,2})for(bool autoFirst: {false,true})for(int lot: {0,1,2,3})Scenario(
        "lot="+std::to_string(lot)+" person="+std::to_string(personType)+" AutoFirst="+std::to_string(autoFirst),[=]{
        Fixture f;f.Add(0);f.Add(1);f.Add(2);
        auto content=FSO::Content::Content::Get();
        content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content,false);
        content->Neighborhood->ZoningDictionary={{1,0},{2,1}};
        f.vm.SetGlobalValue(10,lot);f.actor->SetPersonData(VMPersonDataVariable::PersonType,personType);
        for(auto& row:f.table.Interactions){row->AllowVisitors(true);row->AutoFirst(autoFirst);}
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"lot/person/AutoFirst matrix selects an eligible candidate");
        const bool deterministic=(lot==0||lot==1)&&(personType!=0||autoFirst);
        const auto after=f.context.RandomSeed;
        f.context.RandomSeed=1234;
        const auto index=deterministic?0:f.context.NextRandom(3);
        Require(after==f.context.RandomSeed,"residential AutoFirst and nonfamily paths alone omit the selection draw");
        const int ids[3]={1,0,2};
        Require(f.actor->GetValue(VMStackObjectVariable::TreeTableEntry)==ids[index],"lottery result records the selected action with original tie order");
        content->Neighborhood.reset();
    });
    Scenario("age-curves-and-sitting",[]{
        Fixture f;f.Add(0);
        // Original age zero follows the adult curve path; children are 1..17.
        auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();
        global->InteractionScoreChild.assign(9,FSO::Common::TS1::TS1Curve("(-100;0) (100;0)"));
        f.actor->SetPersonData(VMPersonDataVariable::PersonsAge,0);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"age zero uses adult interaction curves");
        f.actor->SetPersonData(VMPersonDataVariable::PersonsAge,17);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE,"age seventeen uses child interaction curves");
        f.actor->SetPersonData(VMPersonDataVariable::PersonsAge,18);
        f.actor->SetPersonData(VMPersonDataVariable::Posture,1);global->Autonomy.SittingMinimum=10;
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE,"sitting minimum can reject the selected candidate");
        auto after=f.context.RandomSeed;f.context.RandomSeed=1234;f.context.NextRandom(1);
        Require(f.context.RandomSeed==after,"even a one-candidate lottery draws before the sitting rejection");
    });
    Scenario("registered-motive-ranking",[]{
        Fixture f;
        auto registration=f.context.Primitives[33].get();
        std::unique_ptr<VMPrimitiveOperand> parsed(registration->OperandModel());
        std::array<uint8_t,8> raw{};parsed->Read(raw.data());
        auto operand=dynamic_cast<VMFind5WorstMotivesOperand*>(parsed.get());
        Require(operand!=nullptr,"opcode33 decodes its actual operand");
        auto run=[&]{return registration->GetHandler()->Execute(&f.frame,operand);};
        auto firstFive=[&]{return std::vector<int>(f.actor->Thread->TempRegisters.begin(),f.actor->Thread->TempRegisters.begin()+5);};
        Require(run()==VMPrimitiveExitCode::GOTO_TRUE,"registered motive ranking runs");
        Require(firstFive()==std::vector<int>({6,7,8,9,13}),"eight equal motives use the DVD swap permutation");
        f.actor->SetMotiveDataFloat(VMMotive::Comfort,-10.4f);
        f.actor->SetMotiveDataFloat(VMMotive::Hunger,-10.49f);
        run();Require(firstFive()[0]==7&&firstFive()[1]==6,"ranking preserves motive fractions within one script integer");
        f.actor->SetMotiveDataFloat(VMMotive::Comfort,0);f.actor->SetMotiveDataFloat(VMMotive::Hunger,.005f);
        run();Require(firstFive()==std::vector<int>({6,7,8,9,13}),"motive differences below0.01 remain ties");
        VMAvatar other(&f.object);other.SetMotiveDataFloat(VMMotive::Fun,-90);f.frame.StackObject=&other;
        raw[4]=1;parsed->Read(raw.data());run();Require(firstFive()[0]==15,"operand selects the stack Sim while writing the caller thread temps");
        raw[4]=255;raw[5]=255;parsed->Read(raw.data());run();Require(firstFive()[0]!=15,"other target selectors fall back to the caller");
        for(auto invalid: {std::array<uint8_t,8>{0,0,0,0,2,0,0,0},std::array<uint8_t,8>{0,0,0,0,0,0,3,0}}){
            bool threw=false;parsed->Read(invalid.data());try{run();}catch(const VMSimanticsException&){threw=true;}
            Require(threw,"invalid original motive selectors are rejected");
        }
        raw={0,0,0,0,1,0,0,0};parsed->Read(raw.data());
        for(auto invalidTarget: {static_cast<VMEntity*>(nullptr),static_cast<VMEntity*>(f.target.get())}){
            f.frame.StackObject=invalidTarget;bool threw=false;try{run();}catch(const VMSimanticsException&){threw=true;}
            Require(threw,"null and nonperson stack targets raise the original invalid-target error");
        }
    });
    Scenario("autonomy-thresholds",[]{
        Fixture f;f.Add(0,50);f.Add(1,51);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"threshold equal to autonomy level remains eligible");
        Require(f.order==std::vector<int>({4200}),"threshold above the Sim autonomy level is rejected before checks");
        f.actor->SetPersonData(VMPersonDataVariable::AutonomyLevel,0);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE&&f.order.empty(),"zero autonomy level cannot admit a positive threshold");
    });
    Scenario("person-and-species",[]{
        Fixture f;f.Add(0);f.table.Interactions[0]->AllowVisitors(true);
        auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();global->Autonomy.FamilyMinimum=100;
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE,"family minimum applies to person type zero");
        f.actor->SetPersonData(VMPersonDataVariable::PersonType,2);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"all nonzero person types use the visitor minimum");
        f.actor->TreeTable=&f.table;f.actor->TreeTableStrings=&f.strings;f.vm.Entities={f.actor.get()};
        f.actor->SetPersonData(VMPersonDataVariable::PersonType,1);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE&&f.order.empty(),"ordinary visitors do not choose self rows");
        f.actor->SetPersonData(VMPersonDataVariable::PersonType,2);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE&&f.order.empty(),"nonfamily human type does not bypass the species self gate");
        f.actor->SetPersonData(VMPersonDataVariable::Gender,1);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_FALSE&&f.order.empty(),"female humans share the self-interaction exclusion");
    });
    for(int species: {2,3,4,5})for(int personType: {0,1,2})Scenario(
        "self species="+std::to_string(species)+" person="+std::to_string(personType),[=]{
        Fixture f;f.Add(0);f.actor->TreeTable=&f.table;f.actor->TreeTableStrings=&f.strings;f.vm.Entities={f.actor.get()};
        f.actor->SetPersonData(VMPersonDataVariable::GreetStatus,1);
        f.actor->SetPersonData(VMPersonDataVariable::PersonType,personType);
        f.actor->SetPersonData(VMPersonDataVariable::Gender,species);
        f.table.Interactions[0]->Flags=TTABFlags::AllowVisitors|(species<4?TTABFlags::TS1AllowDogs:TTABFlags::TS1AllowCats);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE,"dog and cat species retain self rows independently of person type");
    });
    // Literal outputs from the unchanged DVD4167d4/421481/4214d4 machine
    // functions, executed with the observed chooser control word0x007f. These
    // synthetic inputs require no game installation. They are not calculated
    // by a duplicate scoring model inside this test.
    struct NumericCase {
        const char* name;bool retail;std::array<float,9> motives;
        std::vector<std::array<int16_t,4>> ads;int personality;
        float attenuation,distance,threshold;uint32_t scoreBits;bool admitted;
        int mutateMotive;int mutateValue;
    };
    const NumericCase numericCases[]={
        {"room_gain_18_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{13,0,9000,9000}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x3ffffff9u,true,-1,0},
        {"room_gain_cancelled_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{13,0,9000,-9000}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x00000000u,false,-1,0},
        {"room_personality_preserved_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{13,1,9000,-9000}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x3effffe4u,true,-1,0},
        {"zero_projected_mean_positive_gain",false,
            {-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f,-0x1.2000000000000p+3f},
            {{3,0,9000,0},{5,0,9000,0},{6,0,9000,0},{7,0,9000,0},{8,0,9000,0},{9,0,9000,0},{13,0,9000,0},{14,0,9000,0},{15,0,9000,0}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x41100000u,false,-1,0},
        {"room_negative_wrap_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{13,0,-32768,-32768}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0xc0e90454u,false,-1,0},
        {"identity_upper_cap",false,
            {0x0.0p+0f,0x0.0p+0f,0x1.6800000000000p+6f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{7,0,32767,32767}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x3f8e38e0u,true,-1,0},
        {"identity_lower_cap",false,
            {0x0.0p+0f,0x0.0p+0f,-0x1.6800000000000p+6f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{7,0,-32768,-32768}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0xbf8e38e0u,false,-1,0},
        {"signed_projection_fraction_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{7,1,-100,-100}},1,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0xbc3638e4u,false,-1,0},
        {"inverse_personality_fraction_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{7,2,-100,-100}},1,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0xbcb5f555u,false,-1,0},
        {"attenuated_identity",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {{7,1,9000,13000}},500,
            0x1.99999a0000000p-4f,0x1.4000000000000p+1f,0x1.ad7f2a0000000p-24f,0x3fb05b06u,true,-1,0},
        {"retail_fractional_mixed",true,
            {-0x1.5000000000000p+2f,0x1.2200000000000p+4f,-0x1.4800000000000p+4f,-0x1.5800000000000p+3f,0x1.0000000000000p-2f,-0x1.0400000000000p+6f,-0x1.e800000000000p+4f,-0x1.4800000000000p+3f,0x1.5800000000000p+3f},
            {{7,1,4000,27000},{13,0,9000,-9000},{15,7,0,13000}},500,
            0x1.99999a0000000p-4f,0x1.4000000000000p+1f,0x1.ad7f2a0000000p-24f,0x3e91a29au,true,-1,0},
        {"check_mutates_hunger_after_baseline",false,
            {0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f,0x0.0p+0f},
            {},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x3f800000u,true,7,9},
        {"retail_all_unchanged",true,
            {-0x1.5000000000000p+2f,0x1.2200000000000p+4f,-0x1.4800000000000p+4f,-0x1.5800000000000p+3f,0x1.0000000000000p-2f,-0x1.0400000000000p+6f,-0x1.e800000000000p+4f,-0x1.4800000000000p+3f,0x1.5800000000000p+3f},
            {},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x00000000u,false,-1,0},
        {"retail_exact_knots",true,
            {-0x1.2000000000000p+6f,-0x1.4000000000000p+6f,-0x1.9000000000000p+4f,-0x1.1800000000000p+5f,-0x1.4000000000000p+4f,-0x1.9000000000000p+5f,-0x1.9000000000000p+5f,-0x1.9000000000000p+4f,0x0.0p+0f},
            {{5,0,0,100},{7,0,0,100},{15,0,0,100}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x3b424000u,true,-1,0},
        {"retail_small_gain",true,
            {-0x1.5000000000000p+2f,0x1.2200000000000p+4f,-0x1.4800000000000p+4f,-0x1.5800000000000p+3f,0x1.0000000000000p-2f,-0x1.0400000000000p+6f,-0x1.e800000000000p+4f,-0x1.4800000000000p+3f,0x1.5800000000000p+3f},
            {{15,0,1,0}},500,
            0x0.0p+0f,0x1.0000000000000p+0f,0x1.ad7f2a0000000p-24f,0x37600000u,true,-1,0},
        {"retail_threshold_seed",true,
            {-0x1.5000000000000p+2f,0x1.2200000000000p+4f,-0x1.4800000000000p+4f,-0x1.5800000000000p+3f,0x1.0000000000000p-2f,-0x1.0400000000000p+6f,-0x1.e800000000000p+4f,-0x1.4800000000000p+3f,0x1.5800000000000p+3f},
            {{7,1,4000,27000}},500,
            0x1.99999a0000000p-4f,0x1.4000000000000p+1f,0x1.ad7f2a0000000p-24f,0x3e546267u,true,-1,0},
        {"retail_threshold_at_stored_score",true,
            {-0x1.5000000000000p+2f,0x1.2200000000000p+4f,-0x1.4800000000000p+4f,-0x1.5800000000000p+3f,0x1.0000000000000p-2f,-0x1.0400000000000p+6f,-0x1.e800000000000p+4f,-0x1.4800000000000p+3f,0x1.5800000000000p+3f},
            {{7,1,4000,27000}},500,
            0x1.99999a0000000p-4f,0x1.4000000000000p+1f,0x1.a8c4ce0000000p-3f,0x3e546267u,true,-1,0},
    };
    const char* retailCurves[]={
        "(-100;-100) (-72;90) (100;90)",
        "(-100;-100) (-80;-30) (10;10) (-45;0)",
        "(-100;-100) (50;50) (-25;40)",
        "(-100;-100) (25;25) (-35;15)",
        "(-100;-100) (50;50) (-20;40)",
        "(-100;-100) (-50;-80)",
        "(-100;-100) (0;0) (-50;-10)",
        "(-100;-100) (50;50) (-25;40)",
        "(-100;-100) (75;75) (0;65)"
    };
    for(const auto& sample:numericCases)Scenario(std::string("DVD-binary32-")+sample.name,[&]{
        Fixture f;f.Add(0,50,0);
        auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();
        if(sample.retail){global->InteractionScore.clear();for(auto str:retailCurves)global->InteractionScore.emplace_back(str);}
        global->Autonomy.FamilyMinimum=sample.threshold;
        for(int i=1;i<23;++i)f.actor->SetPersonData(VMFindBestAction::VaryByTypes[i],sample.personality);
        for(int i=0;i<9;++i)f.actor->SetMotiveDataFloat(VMFindBestAction::WeightMotives[i],sample.motives[i]);
        auto& entry=*f.table.Interactions[0];entry.AttenuationCode=0;entry.AttenuationValue=sample.attenuation;
        for(const auto& ad:sample.ads){auto& item=entry.MotiveEntries[ad[0]];
            item.PersonalityModifier=ad[1];item.EffectRangeMinimum=ad[2];item.EffectRangeMaximum=ad[3];}
        f.target->Position(FSO::LotView::Model::LotTilePos(64+int(sample.distance*16),64,1));
        if(sample.mutateMotive>=0){
            auto check=f.iff.Get<BHAV>(4200);check->Instructions={
                AssignCheckValue(Scopes::VMVariableScope::MyMotives,sample.mutateMotive,sample.mutateValue,254)};
        }
        const auto observed=ObserveChooserScore(f);uint32_t bits;
        std::memcpy(&bits,&observed.second,sizeof(bits));
        Require(bits==sample.scoreBits,"actual chooser score equals the literal DVD binary32 output bits");
        Require(observed.first==(sample.admitted?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_FALSE),
            "actual chooser admission matches the literal DVD result, including zero projected mean");
    });
    struct RoomAdvertisementCase {int minimum,delta,selector;float sameRoom,differentRoom;};
    // Literal source results with nine identity curves, not a copy of the
    // implementation: cross-room delta becomes -minimum, selector stays intact.
    const RoomAdvertisementCase roomCases[]={
        {9000,9000,0,2.0f,0.0f},
        {9000,9000,1,1.5f,0.5f},
        {-9000,18000,0,1.0f,0.0f},
        {-9000,18000,1,0.0f,-0.5f},
        {0,9000,0,1.0f,0.0f},
        // DVD NEG CX wraps the minimum signed word; it does not saturate or
        // produce a positive32768 advertisement outside the word range.
        {-32768,0,0,-3.640888889f,-7.281777778f}
    };
    for(bool sameRoom: {false,true})Scenario("room-only-ad together="+std::to_string(sameRoom),[=]{
        Fixture f;f.Add(0,50,0);
        f.table.Interactions[0]->MotiveEntries[13].EffectRangeMinimum=9000;
        f.table.Interactions[0]->MotiveEntries[13].EffectRangeMaximum=9000;
        for(auto motive: VMFindBestAction::WeightMotives)f.actor->SetMotiveDataFloat(motive,0);
        f.actor->SetValue(VMStackObjectVariable::Room,3);f.target->SetValue(VMStackObjectVariable::Room,sameRoom?3:4);
        RequireChooserScore(f,sameRoom?2.0f:0.0f);
    });
    for(const auto& ad:roomCases)for(const auto& rooms: {
        std::array<int,2>{3,3},std::array<int,2>{3,4},
        std::array<int,2>{-5,-5},std::array<int,2>{-5,0}})Scenario(
        "postcheck-room-ad min="+std::to_string(ad.minimum)+" delta="+std::to_string(ad.delta)+
        " selector="+std::to_string(ad.selector)+" rooms="+std::to_string(rooms[0])+","+std::to_string(rooms[1]),[=]{
        Fixture f;f.Add(0,50,9000); // Unrelated Hunger gain remains exactly1.
        auto& entry=*f.table.Interactions[0];
        entry.MotiveEntries[13].EffectRangeMinimum=ad.minimum;
        entry.MotiveEntries[13].EffectRangeMaximum=ad.delta;
        entry.MotiveEntries[13].PersonalityModifier=ad.selector;
        f.actor->SetPersonData(VMPersonDataVariable::NicePersonality,500);
        for(auto motive: VMFindBestAction::WeightMotives)f.actor->SetMotiveDataFloat(motive,0);
        f.actor->SetValue(VMStackObjectVariable::Room,rooms[0]);
        f.target->SetValue(VMStackObjectVariable::Room,rooms[1]);
        RequireChooserScore(f,1.0f+(rooms[0]==rooms[1]?ad.sameRoom:ad.differentRoom));
        Require(entry.MotiveEntries[13].EffectRangeMinimum==ad.minimum&&
                entry.MotiveEntries[13].EffectRangeMaximum==ad.delta&&
                entry.MotiveEntries[13].PersonalityModifier==ad.selector&&
                entry.MotiveEntries[7].EffectRangeMinimum==9000,
            "postcheck scoring leaves shared TTAB advertisements and unrelated motives unchanged");
    });
    for(bool initiallyTogether: {false,true})for(bool moveCaller: {false,true})Scenario(
        "check-edits-room-and-ad together="+std::to_string(initiallyTogether)+" move-caller="+std::to_string(moveCaller),[=]{
        Fixture f;f.Add(0,50,9000);
        auto& entry=*f.table.Interactions[0];
        entry.MotiveEntries[13].EffectRangeMinimum=1000;
        entry.MotiveEntries[13].EffectRangeMaximum=2000;
        f.actor->SetPersonData(VMPersonDataVariable::NicePersonality,500);
        for(auto motive: VMFindBestAction::WeightMotives)f.actor->SetMotiveDataFloat(motive,0);
        f.actor->SetValue(VMStackObjectVariable::Room,initiallyTogether?3:2);
        f.target->SetValue(VMStackObjectVariable::Room,3);
        using Scope=Scopes::VMVariableScope;
        auto check=f.iff.Get<BHAV>(4200);check->Instructions={
            AssignCheckValue(Scope::TreeAdMin,13,9000,1),
            AssignCheckValue(Scope::TreeAdRange,13,18000,2),
            AssignCheckValue(Scope::TreeAdPersonalityVar,13,1,3),
            AssignCheckValue(moveCaller?Scope::MyObject:Scope::StackObject,int(VMStackObjectVariable::Room),
                moveCaller?(initiallyTogether?2:3):2,254)
        };
        RequireChooserScore(f,initiallyTogether?1.5f:3.0f);
        Require((f.actor->GetValue(VMStackObjectVariable::Room)==f.target->GetValue(VMStackObjectVariable::Room))==!initiallyTogether,
            "the real authored check changes the rooms before the scoring comparison");
        Require(entry.MotiveEntries[13].EffectRangeMinimum==1000&&entry.MotiveEntries[13].EffectRangeMaximum==2000&&
                entry.MotiveEntries[13].PersonalityModifier==0,
            "check-generated advertisements remain temporary rather than changing the resource");
    });
    Scenario("cross-room-advertisement-changes-choice",[]{
        Fixture f;f.Add(0,50,9000);f.Add(1,50,13500);
        f.table.Interactions[0]->MotiveEntries[13].EffectRangeMinimum=9000;
        f.table.Interactions[0]->MotiveEntries[13].EffectRangeMaximum=9000;
        for(auto motive: VMFindBestAction::WeightMotives)f.actor->SetMotiveDataFloat(motive,0);
        auto global=FSO::Content::Content::Get()->WorldObjectGlobals.get();global->Autonomy.RandomSelectionCount=1;
        f.actor->SetValue(VMStackObjectVariable::Room,3);f.target->SetValue(VMStackObjectVariable::Room,3);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE&&f.actor->GetValue(VMStackObjectVariable::TreeTableEntry)==0,
            "same-room candidate with Room and Hunger gains outranks the Hunger-only row");
        f.target->SetValue(VMStackObjectVariable::Room,4);
        Require(f.Choose()==VMPrimitiveExitCode::GOTO_TRUE&&f.actor->GetValue(VMStackObjectVariable::TreeTableEntry)==1,
            "cross-room Room adjustment makes the unchanged Hunger-only row win");
    });
    std::cout<<"original autonomy contract: "<<checks<<" checks, "<<failures<<" failed scenarios\n";return failures?1:0;
}catch(const std::exception& e){std::cerr<<"original autonomy contract: "<<e.what()<<'\n';return 1;}}
