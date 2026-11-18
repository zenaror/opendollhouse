// SPDX-License-Identifier: MPL-2.0
// Real entry5 BHAV dispatch plus production placement primitives. No game assets.
// DVD481947/482558 admits before the callback, then consumes literal numeric
// bounds after FALSE; it does not repeat room/container/physical admission.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/Routing/VMTS1RouteWorld.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/primitives/VMSnap.h"
#include "simantics/primitives/VMFindLocationFor.h"
#include "files/formats/iff/chunks/BHAV.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Routing;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::LotView::Model;
using Scope=Scopes::VMVariableScope;
namespace {
int Checks=0;
void Check(bool ok,const char* why){++Checks;if(!ok)throw std::runtime_error(why);}
bool Equal(const TS1RouteRect& a,const TS1RouteRect& b){return a.X1==b.X1&&a.Y1==b.Y1&&a.X2==b.X2&&a.Y2==b.Y2&&a.Weight==b.Weight;}
BHAVInstruction Expression(Scope lhs,int16_t li,Scope rhs,int16_t ri,VMExpressionOperator op=VMExpressionOperator::Assign){
 BHAVInstruction i;i.Opcode=2;VMExpressionOperand p;p.LhsOwner=lhs;p.LhsData=li;p.RhsOwner=rhs;p.RhsData=ri;p.Operator=op;p.Write(i.Operand.data());return i;
}
enum class Mutation{None,Contain,OutOfWorld,Penalty};
struct Fixture {
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"route-callback-contract",true};
 std::array<OBJD,4> Definitions;std::array<FSO::Content::GameObject,4> Objects;
 std::array<Entities::VMMultitileGroup,4> Groups;
 std::array<std::unique_ptr<VMGameObject>,4> Owned;
 VMGameObject *Caller,*Support,*Candidate,*Tail;
 std::vector<int16_t> CallbackOrder;int Exceptions=0,Dialogs=0,Snaps=0,OutOfWorldCalls=0;
 Fixture(){
  Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
  Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);Context.RebuildEnvironmentGeometry();
  // Explicit fresh manager provenance, with actual AddEntity prepend lifecycle.
  Machine.TS1ObjectManagerOrderKnown=true;
  for(size_t n=0;n<Owned.size();++n){
   Definitions[n].GUID=0xf0310500+uint32_t(n);Definitions[n].ObjectType=OBJDType::Normal;Definitions[n].TileWidth=n==0?1:16;
   Objects[n].OBJ=&Definitions[n];Objects[n].GUID=Definitions[n].GUID;Objects[n].Resource=&Resource;
   Owned[n]=std::make_unique<VMGameObject>(&Objects[n],nullptr);auto* e=Owned[n].get();
   e->MultitileGroup=&Groups[n];Groups[n].AddObject(e);Machine.AddEntity(e);
   e->Thread=std::make_unique<VMThread>(&Context,e,16);e->Direction(Direction::NORTH);
   for(size_t k=0;k<5;++k)e->Thread->TempRegisters[k]=int16_t(100+n*10+k);
  }
  Caller=Owned[0].get();Support=Owned[1].get();Candidate=Owned[2].get();Tail=Owned[3].get();
  Support->Contained.resize(1,nullptr);Support->SetValue(VMStackObjectVariable::Flags,16);
  Position(*Caller,40,40);Position(*Support,96,96);Position(*Candidate,72,72);Position(*Tail,96,48);
  Check(Machine.TS1ObjectManagerOrder==std::vector<VMEntity*>{Tail,Candidate,Support,Caller},"fixture manager birth order differs");
  Check(Caller->TS1NumericFootprint==std::array<int32_t,4>{40,40,40,40}&&Caller->Footprint&&Caller->Footprint->x1==32,
        "caller width1 did not distinguish source numeric from physical rectangle");
  Machine.OnSimanticsException=[&](VMEntity*,VMEntity*,const std::string&){++Exceptions;};
  Machine.OnDialog=[&](Model::VMDialogInfo&){++Dialogs;};
  // Observation only: all mutation below belongs to authored BHAV primitives.
  Machine.OnTraceInstruction=[&](VMStackFrame* frame,uint16_t opcode,int){
   if(!frame||!frame->Routine)return;
   if(opcode==46)++Snaps;
   if(opcode==16)++OutOfWorldCalls;
   if((frame->Routine->ID!=4096&&frame->Routine->ID!=4097)||frame->InstructionPointer!=0)return;
   Check(frame->Caller==frame->Callee&&(frame->Caller==Candidate||frame->Caller==Tail),"entry5 did not execute on candidate identity");
   Check(frame->StackObject==Caller,"entry5 StackObject is not original route caller");
   Check(frame->Args.size()>=5,"entry5 truncated authored five argument slots");
   // DVD422d79 passes NULL explicit args;4749b7 falls back to this candidate's
   // interpreter temporary bank. Distinct values exclude invented zero args.
   for(size_t k=0;k<5;++k)Check(frame->Args[k]==frame->Caller->Thread->TempRegisters[k],"entry5 did not inherit candidate temporary arguments");
   CallbackOrder.push_back(frame->Caller->ObjectID);
  };
 }
 ~Fixture(){for(auto& e:Owned){e->Container=nullptr;for(auto& child:e->Contained)child=nullptr;}Machine.Entities.clear();}
 void Position(VMEntity& e,int x,int y){e.PrePositionChange(&Context);e.Position(LotTilePos(int16_t(x),int16_t(y),1));e.PositionChange(&Context,true);}
 void Callback(VMGameObject& object,uint16_t id,Mutation mutation,bool result){
  auto* tree=new BHAV();tree->ChunkID=id;tree->ChunkProcessed=true;tree->Args=5;
  tree->Instructions.push_back(Expression(Scope::MyObjectAttributes,0,Scope::Literal,1,VMExpressionOperator::PlusEquals));
  tree->Instructions.push_back(Expression(Scope::MyObjectAttributes,1,Scope::StackObjectID,0));
  tree->Instructions.push_back(Expression(Scope::MyObjectAttributes,2,Scope::Parameters,4));
  if(mutation==Mutation::Contain){
   tree->Instructions.push_back(Expression(Scope::StackObjectID,0,Scope::Literal,Support->ObjectID));
   BHAVInstruction i;i.Opcode=46;VMSnapOperand p;p.Mode=VMSnapSlotScope::BeContained;p.Write(i.Operand.data());tree->Instructions.push_back(i);
  }else if(mutation==Mutation::OutOfWorld){
   tree->Instructions.push_back(Expression(Scope::StackObjectID,0,Scope::MyObject,int16_t(VMStackObjectVariable::ObjectId)));
   BHAVInstruction i;i.Opcode=16;VMFindLocationForOperand p;p.Mode=1;p.Write(i.Operand.data());tree->Instructions.push_back(i);
  }else if(mutation==Mutation::Penalty){
   tree->Instructions.push_back(Expression(Scope::MyObject,int16_t(VMStackObjectVariable::RoutePenalty),Scope::Literal,1));
  }
  // Mark completion only after successful placement. FALSE is an authored
  // ordinary return, not a failed primitive or an observer-induced mutation.
  tree->Instructions.push_back(Expression(Scope::MyObjectAttributes,3,Scope::Literal,77));
  for(size_t n=0;n<tree->Instructions.size();++n){auto& i=tree->Instructions[n];i.TruePointer=n+1==tree->Instructions.size()?(result?254:255):uint8_t(n+1);i.FalsePointer=255;}
  Iff.AddChunk(tree);object.EntryPoints[5].ActionFunction=id;
 }
 std::vector<TS1RouteRect> Collect(){Callback(*Tail,4097,Mutation::None,false);auto result=TS1CollectRouteObjectObstacles(*Caller);
  Check(!Exceptions&&!Dialogs,"callback raised/reset/dialog instead of ordinary return");
  Check(CallbackOrder==std::vector<int16_t>{Tail->ObjectID,Candidate->ObjectID},"entry5 duplicated, skipped or reordered manager traversal");
  for(auto* e:{Tail,Candidate}){
   Check(e->GetAttribute(0)==1&&e->GetAttribute(1)==Caller->ObjectID&&e->GetAttribute(2)==e->Thread->TempRegisters[4]&&e->GetAttribute(3)==77,
         "BHAV callback did not finish exactly once with caller identity and fifth temp argument");
   Check(e->Thread->Stack.empty()&&e->Thread->Queue.empty()&&!e->Dead,"callback leaked work or changed lifetime");
  }
  return result;
 }
 void Expected(const std::vector<TS1RouteRect>& got,const TS1RouteRect& candidate){
  Check(got.size()==2,"FALSE candidate lost after mutable callback or extra obstacle invented");
  Check(Equal(got[0],{85,37,107,59,INT32_MAX}),"first obstacle lost source manager order");
  Check(Equal(got[1],candidate),"candidate did not use literal post-callback numeric rectangle");
 }
};
void NumericCaller(){Fixture f;f.Position(*f.Candidate,48,40);f.Callback(*f.Candidate,4096,Mutation::None,false);
 f.Expected(f.Collect(),{37,29,59,51,INT32_MAX});Check(f.Snaps==0&&f.OutOfWorldCalls==0,"plain callback ran placement");}
void ContainedFalse(){Fixture f;f.Callback(*f.Candidate,4096,Mutation::Contain,false);auto got=f.Collect();
 Check(f.Candidate->Container==f.Support&&f.Support->GetSlot(0)==f.Candidate&&!f.Candidate->Footprint,"real Snap did not null physical occupancy through containment");
 Check(f.Candidate->TS1NumericFootprint==std::array<int32_t,4>{88,88,104,104},"contained position commit did not publish expected source bounds");
 f.Expected(got,{85,85,107,107,INT32_MAX});Check(f.Snaps==1&&f.OutOfWorldCalls==0,"contained mutation primitive did not run exactly once");}
void OutOfWorldFalse(){Fixture f;f.Callback(*f.Candidate,4096,Mutation::OutOfWorld,false);auto got=f.Collect();
 Check(f.Candidate->Position()==LotTilePos::OUT_OF_WORLD&&!f.Candidate->Footprint,"real find-location did not move candidate off world");
 Check(f.Candidate->TS1NumericFootprint==std::array<int32_t,4>{-32776,-32776,-32760,-32760},"off-world position commit did not publish literal source bounds");
 f.Expected(got,{-32779,-32779,-32757,-32757,INT32_MAX});Check(f.OutOfWorldCalls==1&&f.Snaps==0,"off-world mutation primitive did not run exactly once");}
void ZeroExtentPenalty(){Fixture f;f.Candidate->SetFlag(VMEntityFlags::HasZeroExtent,true);f.Candidate->SetValue(VMStackObjectVariable::RoutePenalty,1);
 Check(!f.Candidate->Footprint&&f.Candidate->TS1NumericFootprintKnown,"zero extent control retained physical occupancy or lost numeric cache");
 f.Callback(*f.Candidate,4096,Mutation::None,false);f.Expected(f.Collect(),{61,61,83,83,10});}
void PostCallbackPenalty(){Fixture f;f.Callback(*f.Candidate,4096,Mutation::Penalty,false);
 Check(f.Candidate->GetValue(VMStackObjectVariable::RoutePenalty)==0,"penalty control did not start at zero");
 f.Expected(f.Collect(),{61,61,83,83,10});Check(f.Candidate->GetValue(VMStackObjectVariable::RoutePenalty)==1,"authored penalty assignment did not commit");}
void TrueCallback(){Fixture f;f.Callback(*f.Candidate,4096,Mutation::None,true);auto got=f.Collect();
 Check(got.size()==1&&Equal(got[0],{85,37,107,59,INT32_MAX}),"TRUE allow-intersection callback still emitted an obstacle");}
}
int main(){try{VM::UseWorld(false);NumericCaller();ContainedFalse();OutOfWorldFalse();ZeroExtentPenalty();PostCallbackPenalty();TrueCallback();
 std::cout<<"PASS TS1 route entry5 callback numeric integration: "<<Checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
