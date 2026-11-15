/* SPDX-License-Identifier: MPL-2.0 */
// Contract fixture, not gameplay: real opcode51 dispatch and BHAV branches.
// Original FreeSO f86bbd34 throws for an invalid target; Alex a3c6c5aa
// returns false. M1 includes the Alex guard as well as the original behavior.
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/primitives/VMTS1InventoryOperations.h"
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
using Ledger=std::map<int16_t,std::vector<std::tuple<int32_t,uint32_t,uint16_t>>>;
static Ledger ledger(const NGBH& data){
    Ledger result;
    for(const auto& pair:data.InventoryByID){auto& out=result[pair.first];
        for(const auto& item:pair.second)out.emplace_back(item.Type,item.GUID,item.Count);}
    return result;
}
int main(){try{
    VMEntity::UseWorld=false;
    auto content=FSO::Content::Content::Get();
    auto oldProvider=std::move(content->Neighborhood);
    content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content,false);
    NGBH data;content->Neighborhood->Neighborhood=&data;
    IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"inventory-target-fixture",true);
    OBJD definition;definition.GUID=0x12345678;definition.ChunkLabel="inventory target";
    FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    VMGameObject caller(&object,nullptr),nonPerson(&object,nullptr);
    VMAvatar person(&object);
    person.SetPersonData(FSO::SimAntics::Model::VMPersonDataVariable::NeighborId,42);
    VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
    vm.AddEntity(&caller);vm.AddEntity(&nonPerson);vm.AddEntity(&person);
    // The actual Temp4 resolver is GetObjectById; no substitute target mapper.
    need(caller.ObjectID==1&&nonPerson.ObjectID==2&&person.ObjectID==3,
        "fixture IDs changed before target checks");
    need(dynamic_cast<VMTS1InventoryOperations*>(context.Primitives[51]->GetHandler())!=nullptr,
        "opcode51 must register the production TS1 handler");
    int exceptions=0,dialogs=0,dispatches=0;
    vm.OnSimanticsException=[&](VMEntity*,VMEntity*,const std::string&){++exceptions;};
    vm.OnDialog=[&](FSO::SimAntics::Model::VMDialogInfo&){++dialogs;};
    vm.OnTraceInstruction=[&](VMStackFrame*,uint16_t opcode,int){if(opcode==51)++dispatches;};
    auto personSnapshot=[&](){std::array<int16_t,101> result{};
        for(size_t i=0;i<result.size();++i)result[i]=person.GetPersonData(FSO::SimAntics::Model::VMPersonDataVariable(i));
        return result;};
    auto run=[&](VMTS1InventoryMode mode,bool tempTarget,int16_t targetID,bool expectedTrue,
                 int16_t count=1,uint32_t guid=0xaabbccdd,int type=7){
        caller.Thread=std::make_unique<VMThread>(&context,&caller,0);
        auto& thread=*caller.Thread;thread.TempRegisters.fill(777);
        thread.TempRegisters[0]=count;thread.TempRegisters[4]=targetID;
        thread.TempXL.fill(123456);
        auto beforeTemps=thread.TempRegisters;auto beforeXL=thread.TempXL;
        const auto before=ledger(data);const auto personData=personSnapshot();
        const auto objectData=caller.ObjectData;
        VMRoutine routine;routine.ID=4096;routine.Rti.Name="inventory target refusal/continuation";
        auto op=std::make_unique<VMTS1InventoryOperationsOperand>();
        op->Mode=mode;op->GUID=guid;op->TokenType=type;op->Flags=3;op->UseObjectInTemp4(tempTarget);
        VMInstruction instruction;instruction.Opcode=51;instruction.TruePointer=1;instruction.FalsePointer=2;
        instruction.Operand=std::move(op);routine.Instructions.push_back(std::move(instruction));
        // Observe the actual false/true continuation, not just handler return.
        for(int marker:{11,22}){
            auto assign=std::make_unique<VMExpressionOperand>();assign->Operator=VMExpressionOperator::Assign;
            assign->LhsOwner=FSO::SimAntics::Engine::Scopes::VMVariableScope(8);assign->LhsData=19;
            assign->RhsOwner=FSO::SimAntics::Engine::Scopes::VMVariableScope(7);assign->RhsData=marker;
            VMInstruction branch;branch.Opcode=2;branch.TruePointer=254;branch.FalsePointer=255;
            branch.Operand=std::move(assign);routine.Instructions.push_back(std::move(branch));
        }
        auto frame=std::make_shared<VMStackFrame>();frame->Routine=&routine;
        frame->Caller=&caller;frame->Callee=&caller;frame->StackObject=&nonPerson;frame->CodeOwner=&object;
        need(thread.Push(frame),"authored routine push failed");
        const int dispatchedBefore=dispatches;thread.Tick();
        need(!exceptions&&!dialogs&&!caller.Reseted&&!person.Reseted,"invalid target raised/reset/dialog");
        need(dispatches==dispatchedBefore+1,"production opcode51 was not dispatched exactly once");
        need(thread.Stack.empty()&&thread.LastStackExitCode==VMPrimitiveExitCode::RETURN_TRUE,
            "BHAV did not finish its normal continuation");
        need(thread.TempRegisters[19]==(expectedTrue?11:22),"wrong BHAV result branch");
        need(thread.TempXL==beforeXL&&personSnapshot()==personData&&caller.ObjectData==objectData,
            "target check changed unrelated VM state");
        need(thread.Queue.empty()&&!caller.Dead&&!person.Dead,"target check deleted actor or queued work");
        if(!expectedTrue){
            need(ledger(data)==before,"refusal changed any inventory");
            for(size_t i=0;i<beforeTemps.size();++i)if(i!=19)
                need(thread.TempRegisters[i]==beforeTemps[i],"refusal overwrote a temporary");
        }
        caller.Thread.reset();
    };
    // Guard must precede every operation, including the otherwise-successful
    // unknown mode; no missing-target Add may initialize an inventory.
    for(int mode:{0,1,2,3,4,5,6,8,255})for(int16_t target:{int16_t(0),int16_t(99),int16_t(2)})
        run(VMTS1InventoryMode(mode),true,target,false);
    run(VMTS1InventoryMode::AddToken,false,3,false); // caller is a game object, not Temp4 person
    need(data.InventoryByID.empty(),"invalid target materialized NGBH inventory");
    run(VMTS1InventoryMode::AddToken,true,3,true,3);
    auto& items=data.InventoryByID.at(42);
    need(items.size()==1&&items[0].GUID==0xaabbccdd&&items[0].Type==7&&items[0].Count==3,
        "valid Add no longer targets the selected person's inventory");
    run(VMTS1InventoryMode::AddToken,true,3,true,2);
    need(items.size()==1&&items[0].Count==5,"valid Add no longer merges same GUID/type");
    run(VMTS1InventoryMode::FindToken,true,3,true);
    run(VMTS1InventoryMode::RemoveToken,true,3,true,2);
    need(items.size()==1&&items[0].Count==3,"valid Remove changed its count semantics");
    run(VMTS1InventoryMode::RemoveToken,true,3,false,4);
    run(VMTS1InventoryMode::RemoveToken,true,3,true,-1);
    need(items.empty(),"remove-all no longer removes the token");
    content->Neighborhood=std::move(oldProvider);
    std::cout<<"PASS opcode51 invalid targets take false continuation without reset/mutation; valid add/find/remove preserved\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
