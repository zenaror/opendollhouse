/* SPDX-License-Identifier: MPL-2.0 */
// Original Made Spell/Recipe emits StackObjectAttributes SetFlag operations.
// Exercise that real writer and the shared reader without commercial resources.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMTS1Travel.h"
#include "simantics/primitives/VMExpression.h"
#include "content/ContentManager.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/OBJD.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using FSO::SimAntics::Engine::Utils::VMTS1Travel;
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
 VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
 FSO::Files::Formats::IFF::IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"spellbook-contract",true);
 FSO::Files::Formats::IFF::Chunks::OBJD definition;definition.NumAttributes=6;definition.GUID=VMTS1Travel::MAGIC_CONTROLLER_GUID;
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 VMGameObject controller(&object,nullptr);VMThread thread(&context,&controller,16);VMStackFrame frame;frame.Thread=&thread;frame.Caller=frame.Callee=frame.StackObject=&controller;
 VMExpression expression;
 auto clear=[&](){for(int i=0;i<6;++i)controller.SetAttribute(i,0);};
 auto emit=[&](int bank,int flag){thread.TempRegisters[1]=int16_t(flag);VMExpressionOperand operand;operand.LhsOwner=Scopes::VMVariableScope::StackObjectAttributes;operand.LhsData=int16_t(bank);operand.RhsOwner=Scopes::VMVariableScope::Temps;operand.RhsData=1;operand.Operator=VMExpressionOperator::SetFlag;Need(expression.Execute(&frame,&operand)==VMPrimitiveExitCode::GOTO_TRUE,"actual one-based SetFlag writer failed");};
 const auto known=[&](bool child,int recipe){const auto before=controller.AttributeData();const bool value=VMTS1Travel::SpellbookRecipeKnown(controller.AttributeData(),child,recipe);Need(controller.AttributeData()==before,"spellbook reader mutated/resized attributes");return value;};
 clear();emit(0,1);Need(known(false,1)&&!known(true,1),"adult first flag used child bank");
 clear();emit(0,15);Need(uint16_t(controller.AttributeData()[0])==0x4000&&known(false,15)&&!known(false,16),"adult15 boundary differs from original producer");
 clear();emit(1,1);Need(uint16_t(controller.AttributeData()[1])==1&&known(false,16)&&!known(false,15),"adult16 was read from the unused first-bank high bit");
 clear();emit(1,16);Need(uint16_t(controller.AttributeData()[1])==0x8000&&known(false,31)&&!known(false,32),"signed second-bank upper flag lost recipe31 or admitted32");
 clear();emit(2,1);Need(known(true,1)&&!known(false,1),"child first flag read adult progress");
 clear();emit(0,16);Need(!known(false,16),"unused first-bank bit15 aliased adult16");
 Need(!known(false,0)&&!known(false,-1)&&!known(true,17),"out-of-bank recipe accepted");std::vector<int16_t> empty;Need(!VMTS1Travel::SpellbookRecipeKnown(empty,false,1)&&empty.empty(),"empty flag input was mutated or invented progress");
 std::cout<<"PASS shared original spellbook banks1/15/16/31/32/child + actual SetFlag producer + immutable reads\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
