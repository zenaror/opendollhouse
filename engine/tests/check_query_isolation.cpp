// SPDX-License-Identifier: MPL-2.0
// Real check execution and VM entity ticks; synthetic resources, no game assets.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/primitives/VMExpression.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using Scope=FSO::SimAntics::Engine::Scopes::VMVariableScope;
static int failures=0,checks=0;
static void check(bool value,const char* label){++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
static VMInstruction assign(Scope scope,int index,int value,int next){
    VMInstruction ins;ins.Opcode=2;ins.TruePointer=next;ins.FalsePointer=255;
    auto arg=std::make_unique<VMExpressionOperand>();arg->Operator=VMExpressionOperator::Assign;
    arg->LhsOwner=scope;arg->LhsData=index;arg->RhsOwner=Scope::Literal;arg->RhsData=value;
    ins.Operand=std::move(arg);return ins;
}
static VMPrimitiveExitCode runCheck(VMContext& context,VMGameObject& actor,bool success=true){
    VMRoutine routine;routine.Arguments=4;
    routine.Instructions.push_back(assign(Scope::Temps,1,77,1));
    routine.Instructions.push_back(assign(Scope::TempXL,1,12345,2));
    auto last=assign(Scope::Temps,3,88,success?254:255);
    routine.Instructions.push_back(std::move(last));
    auto frame=std::make_shared<VMStackFrame>();frame->Caller=&actor;frame->Callee=&actor;
    frame->StackObject=&actor;frame->Routine=&routine;frame->Args.assign(4,0);
    return VMThread::EvaluateCheck(&context,&actor,frame);
}
// Test-only access skips room rebuilding in the empty architecture, without
// introducing a production diagnostic setter or changing the class definition.
template<class Tag,typename Tag::Type Member> struct FieldAccess{
    friend typename Tag::Type field(Tag){return Member;}
};
struct WallsField{using Type=bool VMArchitecture::*;friend Type field(WallsField);};
struct FloorsField{using Type=bool VMArchitecture::*;friend Type field(FloorsField);};
struct RedrawField{using Type=bool VMArchitecture::*;friend Type field(RedrawField);};
template struct FieldAccess<WallsField,&VMArchitecture::WallsDirty>;
template struct FieldAccess<FloorsField,&VMArchitecture::FloorsDirty>;
template struct FieldAccess<RedrawField,&VMArchitecture::Redraw>;
struct Actor:VMGameObject{
    VMContext& context;VM& vm;bool fail=false,nested=false;int depth=0,runs=0;
    Actor(VMContext& c,VM& v,FSO::Content::GameObject* object):VMGameObject(object,nullptr),context(c),vm(v){
        Thread=std::make_unique<VMThread>(&c,this,16);
    }
    void Tick() override{
        ++depth;++runs;
        if(nested&&depth==1){
            vm.InternalTick();
            // The inner check already wrote the expected result. Reset it so
            // the outer write proves that the surrounding tick is still active.
            Thread->TempRegisters.fill(31);Thread->TempXL.fill(90001);
        }
        runCheck(context,*this);
        check(Thread->TempRegisters[1]==77&&Thread->TempXL[1]==12345,"entity-tick checks retain their writes, including after nested tick");
        --depth;if(fail)throw std::runtime_error("fixture entity exception");
    }
};
int main(){try{
    VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);
    vm.TS1=false;vm.GlobalState.resize(38,0);
    auto arch=std::make_unique<VMArchitecture>(8,8,nullptr,&context);
    arch.get()->*field(WallsField{})=false;arch.get()->*field(FloorsField{})=false;
    arch.get()->*field(RedrawField{})=false;arch->TerrainDirty=false;
    context.Architecture(std::move(arch));
    FSO::Files::Formats::IFF::IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"check-fixture",true);
    FSO::Files::Formats::IFF::Chunks::OBJD definition;definition.GUID=0x12345678;
    FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    Actor actor(context,vm,&object);
    auto reset=[&](){actor.Thread->TempRegisters.fill(31);actor.Thread->TempXL.fill(90001);};
    auto outside=[&](bool result){
        reset();const auto temps=actor.Thread->TempRegisters;const auto xl=actor.Thread->TempXL;
        const auto code=runCheck(context,actor,result);
        check(code==(result?VMPrimitiveExitCode::RETURN_TRUE:VMPrimitiveExitCode::RETURN_FALSE),"query result survives temporary isolation");
        check(actor.Thread->TempRegisters==temps,"outside query preserves all short temporaries");
        check(actor.Thread->TempXL==xl,"outside query preserves all extended temporaries");
        check(actor.Thread->Queue.empty()&&actor.Thread->Stack.empty(),"query does not install a live action");
    };
    outside(true);outside(false);
    vm.Entities={&actor};reset();vm.InternalTick();check(actor.runs==1,"actual VM tick executes fixture entity");outside(true);
    actor.fail=true;bool caught=false;try{vm.InternalTick();}catch(const std::runtime_error&){caught=true;}
    check(caught,"fixture exception propagates from entity tick");actor.fail=false;outside(true);
    actor.nested=true;reset();const int before=actor.runs;vm.InternalTick();
    check(actor.runs==before+2,"nested actual tick and outer continuation execute");outside(true);
    vm.Entities.clear();std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}catch(const std::exception& e){std::cerr<<"FIXTURE_ERROR "<<e.what()<<'\n';return 2;}}
