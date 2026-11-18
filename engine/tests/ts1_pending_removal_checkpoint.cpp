// SPDX-License-Identifier: MPL-2.0
// Native continuation contract, with synthetic objects and a decoded classic
// companion. Actual DVD and frontend save/reopen are separate integration gates.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMPrimitiveRegistration.h"
#include "simantics/primitives/VMTS1Budget.h"
#include "simantics/primitives/VMRandomNumber.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/chunks/OBJM.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "files/formats/iff/chunks/ODMP.h"
#include "files/formats/iff/chunks/SIMI.h"
#include "files/Hash.h"
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Checkpoint=Utils::VMTS1InteractionCheckpoint;
static int checks=0;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
template<class F>static void rejected(F f,const char* why){bool caught=false;try{f();}catch(const std::exception&){caught=true;}need(caught,why);}
static OBJTEntry type(uint32_t guid){st::Writer w(st::Endian::Little);w.WriteUInt32(guid);for(int i=0;i<4;++i)w.WriteUInt16(0);w.WriteUInt16(1);w.WriteUInt16(uint16_t(OBJDType::Normal));w.WriteByte('x');w.WriteByte(0);auto bytes=w.ToArray();st::Reader r(bytes);return OBJTEntry(r,2);}
struct Probe:VMGameObject{
 std::function<void()> Removing;
 explicit Probe(FSO::Content::GameObject* object):VMGameObject(object,nullptr){}
 void Tick()override{}
 void PrePositionChange(VMContext*)override{if(Removing)Removing();}
};
struct Fixture{
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"pending-checkpoint",true};
 OBJD Def;FSO::Content::GameObject Object;OBJTEntry Type;
 Entities::VMMultitileGroup Groups[3];std::unique_ptr<Probe> Objects[3];
 Fixture():Type(type(0x1234abcd)){
  Machine.GlobalState.assign(64,0);Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
  Context.Architecture()->Rooms[0].Map.assign(64,0);Context.RoomInfo.resize(1);
  Def.GUID=0x1234abcd;Object.OBJ=&Def;Object.GUID=Def.GUID;Object.Resource=&Resource;
  for(int i=0;i<3;++i){auto& e=Objects[i];e=std::make_unique<Probe>(&Object);e->MultitileGroup=&Groups[i];Groups[i].AddObject(e.get());Machine.AddEntity(e.get());e->Thread=std::make_unique<VMThread>(&Context,e.get(),16);e->SetRoom(0);}
  // Production placement normalizes the out-of-world script room sentinel.
  // Start inside that domain, rather than compare constructor-only room zero
  // against the restore publisher's correctly normalized room bank.
  Context.Clock->Ticks=232560;Context.RandomSeed=123;Context.TS1SimulationRandomSeed=456;
 }
 std::vector<int16_t> ids()const{return {Objects[2]->ObjectID,Objects[0]->ObjectID};}
 void pending(){for(auto id:ids())need(Machine.QueueTS1Removal(Machine.GetObjectById(id),true),"completed Init request rejected");}
 IffRawFile raw(){
  IffRawFile result;result.Set("ObjM",1,{1,2,3});
  SIMI simi;simi.Version=0x49;simi.GlobalData.resize(38);simi.GlobalTail.resize(26);simi.BudgetDays.resize(6);
  st::Writer bytes(st::Endian::Little);simi.Write(nullptr,bytes);result.Set("SIMI",1,bytes.ToArray());return result;
 }
 std::unique_ptr<IffFile> house(IffRawFile& raw){
  auto result=std::make_unique<IffFile>();auto objm=new OBJM();objm->ChunkID=1;objm->ChunkProcessed=true;objm->Decoded=true;objm->Version=0x49;
  objm->SourceSignature=FSO::Files::Hash::CalculateHash(raw.Find("ObjM",1)->Data);
  for(auto& e:Objects){auto inst=std::make_shared<OBJMInstance>();inst->OBJT=&Type;inst->HasSchedulerStateTail=true;inst->UnknownInt=0x10002;objm->ObjectData[e->ObjectID].Instance=inst;}
  result->AddChunk(objm);
  auto native=new ODHI();native->ChunkID=1;native->ChunkProcessed=true;native->Data=raw.Find("ODHI",1)->Data;result->AddChunk(native);
  if(auto source=raw.Find("ODMP",1)){auto marker=new ODMP();marker->ChunkID=1;marker->ChunkProcessed=true;marker->Data=source->Data;result->AddChunk(marker);}
  auto simi=new SIMI();simi->ChunkID=1;simi->ChunkProcessed=true;st::Reader bytes(raw.Find("SIMI",1)->Data);simi->Read(nullptr,bytes);result->AddChunk(simi);
  return result;
 }
};
static void pausedRoundtrip(){
 Fixture source;source.pending();VMTS1Budget::LoadState(&source.Machine,731,{});
 source.Objects[2]->Thread->TempRegisters[4]=119;source.Objects[2]->MainParam=17;source.Objects[2]->MyList.push_back(93);
 auto raw=source.raw();const auto classic=raw.Find("ObjM",1)->Data;
 auto oldThread=source.Objects[0]->Thread.get();Checkpoint::Capture(source.Machine,raw,false);
 need(source.Machine.GetTS1PendingRemovalIDs()==source.ids()&&!source.Objects[0]->Dead&&source.Objects[0]->Thread.get()==oldThread,"capture changed live removal state");
 need(source.Context.Clock->Ticks==232560&&source.Context.RandomSeed==123&&source.Context.TS1SimulationRandomSeed==456&&source.Machine.TS1Budget.Purse==731,"capture advanced time, RNG or purse");
 need(raw.Find("ObjM",1)->Data==classic,"native capture rewrote its classic identity companion");
 Fixture restored;need(restored.Objects[0].get()!=source.Objects[0].get(),"restore reused original pointers");auto house=restored.house(raw);
 restored.Context.RandomSeed=999;restored.Context.TS1SimulationRandomSeed=888;restored.Context.Clock->Ticks=100;
 auto thread=restored.Objects[0]->Thread.get();Checkpoint::Restore(restored.Machine,*house,false,true);
 need(restored.Objects[0]->Thread.get()==thread&&!restored.Machine.HasTS1PendingRemovals()&&restored.Context.Clock->Ticks==100&&restored.Context.RandomSeed==999,"validation published continuation");
 Checkpoint::Restore(restored.Machine,*house);
 need(restored.Machine.GetTS1PendingRemovalIDs()==source.ids()&&restored.Context.Clock->Ticks==232560&&restored.Context.RandomSeed==123&&restored.Context.TS1SimulationRandomSeed==456&&restored.Machine.TS1Budget.Purse==731,"new identity graph did not resume exact pending continuation");
 need(restored.Objects[2]->Thread->TempRegisters[4]==119&&restored.Objects[2]->MainParam==17&&restored.Objects[2]->MyList==source.Objects[2]->MyList,"pending object's completed body state was lost on reopen");
 auto repeated=restored.raw();Checkpoint::Capture(restored.Machine,repeated,false);need(repeated.Find("ODHI",1)->Data==raw.Find("ODHI",1)->Data,"paused save after reopen changed continuation bytes");
 std::vector<int16_t> order;for(auto& e:restored.Objects){auto p=e.get();p->Removing=[&,p]{if(order.empty()||order.back()!=p->ObjectID)order.push_back(p->ObjectID);};}
 restored.Machine.InternalTick();need(order==source.ids()&&restored.Objects[0]->Dead&&restored.Objects[2]->Dead&&!restored.Objects[1]->Dead&&!restored.Machine.HasTS1PendingRemovals(),"first ordinary sweep lost FIFO or removed another identity");
 restored.Machine.InternalTick();need(order==source.ids(),"second sweep removed a target twice");
 need(!source.Objects[0]->Dead&&source.Machine.GetTS1PendingRemovalIDs()==source.ids(),"restored queue retained source pointers");
 for(uint32_t version:{10u,11u,12u,13u,14u,15u,16u}){auto destination=source.raw();auto before=destination.Write();rejected([&]{Checkpoint::Capture(source.Machine,destination,false,false,version);},"old compatibility capture silently dropped pending requests");need(destination.Write()==before,"old compatibility failure changed destination");}
}
static void malformedTail(){
 Fixture source;source.pending();auto raw=source.raw();Checkpoint::Capture(source.Machine,raw,false);
 const auto bytes=raw.Find("ODHI",1)->Data;
 // New continuation sections may follow this FIFO. Locate its independently
 // encoded unique fixture instead of corrupting whichever field is last.
 st::Writer fifo(st::Endian::Little);fifo.WriteUInt32(source.ids().size());
 for(const auto id:source.ids()){fifo.WriteUInt16(uint16_t(id));fifo.WriteUInt32(source.Def.GUID);}
 const auto pattern=fifo.ToArray();const auto found=std::search(bytes.begin(),bytes.end(),pattern.begin(),pattern.end());
 need(found!=bytes.end()&&std::search(found+1,bytes.end(),pattern.begin(),pattern.end())==bytes.end(),"pending FIFO fixture is absent or ambiguous");
 const size_t countAt=size_t(found-bytes.begin()),first=countAt+4;
 for(int scenario=0;scenario<9;++scenario){
  Fixture target;target.pending();auto broken=raw;broken.Remove("ODMP",1);auto& data=broken.Find("ODHI",1)->Data;
  if(scenario==0){data[first+6]=data[first];data[first+7]=data[first+1];} // duplicate
  if(scenario==1){data[first]=0;data[first+1]=0;} // null ID
  if(scenario==2){data[first]=0xff;data[first+1]=0xff;} // negative ID
  if(scenario==3){data[first]=0xff;data[first+1]=0x7f;} // unknown ID
  if(scenario==4)data[first+2]^=1; // wrong GUID
  if(scenario==5)data.resize(first+11); // truncated second reference, before later sections
  if(scenario==6){data[countAt]=0;data[countAt+1]=0x80;} // count overflow
  if(scenario==7)target.Objects[2]->Dead=true;
  if(scenario==8)target.Objects[2]->GhostImage=true;
  auto house=target.house(broken);auto thread=target.Objects[0]->Thread.get();auto purse=target.Machine.TS1Budget.Purse;
  target.Context.RandomSeed=789;target.Context.Clock->Ticks=12;target.Machine.TS1TextInput="unchanged";
  rejected([&]{Checkpoint::Restore(target.Machine,*house);},"malformed removal continuation accepted");
  need(target.Objects[0]->Thread.get()==thread&&target.Context.RandomSeed==789&&target.Context.Clock->Ticks==12&&target.Machine.TS1Budget.Purse==purse&&target.Machine.TS1TextInput=="unchanged","malformed removal restore published partial state");
  target.Objects[2]->Dead=false;target.Objects[2]->GhostImage=false;
  need(target.Machine.GetTS1PendingRemovalIDs()==target.ids(),"malformed removal restore changed existing queue");
 }
 // An otherwise live pending identity must belong to the complete saved graph.
 Fixture missing;auto house=missing.house(raw);house->Get<OBJM>(1)->ObjectData.erase(missing.Objects[2]->ObjectID);
 rejected([&]{Checkpoint::Restore(missing.Machine,*house);},"omitted classic pending identity accepted");
 need(!missing.Machine.HasTS1PendingRemovals(),"omitted identity published a queue");
}
static void oldDefaultAndPreparedCommit(){
 Fixture source;auto raw=source.raw();
 for(uint32_t version:{10u,12u,13u,14u,15u,16u}){
  Checkpoint::Capture(source.Machine,raw,false,false,version);Fixture target;target.pending();auto house=target.house(raw);
  Checkpoint::Restore(target.Machine,*house);need(!target.Machine.HasTS1PendingRemovals(),"old native snapshot inferred pending requests");
 }
 Fixture target;target.pending();auto prepared=target.Machine.PrepareTS1PendingRemovalRestore({target.Objects[1]->ObjectID});
 need(target.Machine.GetTS1PendingRemovalIDs()==target.ids(),"prepare changed live queue");
 target.Machine.CommitTS1PendingRemovalRestore(prepared);need(target.Machine.GetTS1PendingRemovalIDs()==std::vector<int16_t>({target.Objects[1]->ObjectID}),"prepared queue commit did not replace state");
 for(auto invalid:std::vector<std::vector<int16_t>>{{0},{-1},{32767},{1,1}}){rejected([&]{target.Machine.PrepareTS1PendingRemovalRestore(invalid);},"invalid prepared request accepted");need(target.Machine.GetTS1PendingRemovalIDs()==std::vector<int16_t>({target.Objects[1]->ObjectID}),"invalid prepare changed queue");}
}
static void temporaryCheckBlocksPublication(){
 Fixture f;f.pending();auto raw=f.raw();Checkpoint::Capture(f.Machine,raw,false);auto house=f.house(raw);
 VMRoutine routine;routine.Arguments=4;VMInstruction instruction;instruction.Opcode=8;instruction.TruePointer=254;instruction.FalsePointer=255;
 auto operand=std::make_unique<VMRandomNumberOperand>();operand->DestinationData=1;operand->DestinationScope=Scopes::VMVariableScope::Temps;operand->RangeData=97;operand->RangeScope=Scopes::VMVariableScope::Literal;
 instruction.Operand=std::move(operand);routine.Instructions.push_back(std::move(instruction));
 auto frame=std::make_shared<VMStackFrame>();auto object=f.Objects[1].get();frame->Caller=object;frame->Callee=object;frame->StackObject=object;frame->CodeOwner=&f.Object;frame->Routine=&routine;frame->Args.assign(4,0);
 bool observed=false,isolated=false,guarded=false,captureRefused=false,restoreRefused=false,unchanged=false;
 f.Machine.OnTraceInstruction=[&](VMStackFrame* active,uint16_t opcode,int){
  if(opcode!=8)return;observed=true;isolated=active->Thread!=object->Thread.get()&&active->Thread->IsCheck&&!f.Machine.EntityTickRunning();guarded=!f.Machine.CanCaptureTS1RemovalState();
  IffRawFile destination;destination.Set("SIMI",1,{1,2,3});const auto seed=f.Context.RandomSeed;const auto tick=f.Context.Clock->Ticks;auto thread=object->Thread.get();
  try{Checkpoint::Capture(f.Machine,destination,false);}catch(const std::runtime_error& e){captureRefused=std::string(e.what()).find("dispatch")!=std::string::npos;}
  try{Checkpoint::Restore(f.Machine,*house);}catch(const std::runtime_error& e){restoreRefused=std::string(e.what()).find("dispatch")!=std::string::npos;}
  unchanged=destination.Find("SIMI",1)->Data==std::vector<uint8_t>({1,2,3})&&f.Context.RandomSeed==seed&&f.Context.Clock->Ticks==tick&&object->Thread.get()==thread;
 };
 need(VMThread::EvaluateCheck(&f.Context,object,frame)==VMPrimitiveExitCode::RETURN_TRUE,"temporary check failed its authored return");
 need(observed&&isolated&&guarded&&captureRefused&&restoreRefused&&unchanged,"temporary check permitted a reentrant or partially published continuation");
 need(f.Machine.CanCaptureTS1RemovalState()&&f.Machine.GetTS1PendingRemovalIDs()==f.ids(),"temporary check leaked execution guard or changed pending queue");
}
static void malformedLiveQueueBlocksCapture(){
 for(int scenario=0;scenario<3;++scenario){
  Fixture f;f.pending();auto destination=f.raw();const auto before=destination.Write();
  std::unique_ptr<VMThread> threads[3];
  if(scenario==0)for(auto& e:f.Objects)e->Object=nullptr;
  if(scenario==1)f.Object.OBJ=nullptr;
  if(scenario==2)for(int i=0;i<3;++i)threads[i]=std::move(f.Objects[i]->Thread);
  rejected([&]{f.Machine.GetTS1PendingRemovalIDs();},"malformed live pending identity was exposed as saveable");
  rejected([&]{f.Machine.PrepareTS1PendingRemovalRestore(f.ids());},"malformed live pending identity was prepared for restore");
  rejected([&]{Checkpoint::Capture(f.Machine,destination,false);},"all-ineligible pending graph was silently dropped");
  need(destination.Write()==before&&f.Machine.HasTS1PendingRemovals()&&f.Context.Clock->Ticks==232560&&f.Context.RandomSeed==123&&f.Context.TS1SimulationRandomSeed==456,"malformed live queue capture changed destination or simulation");
  if(scenario==0)for(auto& e:f.Objects)e->Object=&f.Object;
  if(scenario==1)f.Object.OBJ=&f.Def;
  if(scenario==2)for(int i=0;i<3;++i)f.Objects[i]->Thread=std::move(threads[i]);
  need(f.Machine.GetTS1PendingRemovalIDs()==f.ids(),"malformed live queue capture lost pending requests");
 }
}
struct ThrowingCheck:VMPrimitiveHandler{
 VMPrimitiveExitCode Execute(VMStackFrame*,VMPrimitiveOperand*)override{throw std::runtime_error("injected temporary check failure");}
};
static void temporaryCheckRecoveryBlocksPublication(){
 Fixture f;f.pending();auto raw=f.raw();Checkpoint::Capture(f.Machine,raw,false);auto house=f.house(raw);
 auto registration=new VMPrimitiveRegistration(new ThrowingCheck());registration->Opcode=200;registration->Name="Checkpoint recovery test";f.Context.AddPrimitive(registration);
 VMRoutine routine;routine.Arguments=4;VMInstruction instruction;instruction.Opcode=200;instruction.TruePointer=254;instruction.FalsePointer=255;routine.Instructions.push_back(std::move(instruction));
 auto frame=std::make_shared<VMStackFrame>();auto object=f.Objects[1].get();frame->Caller=object;frame->Callee=object;frame->StackObject=object;frame->CodeOwner=&f.Object;frame->Routine=&routine;frame->Args.assign(4,0);
 int exceptions=0,dialogs=0;std::vector<bool> guarded;
 auto probe=[&]{
  bool captureRefused=false,restoreRefused=false;IffRawFile destination;destination.Set("SIMI",1,{4,5,6});
  const auto seed=f.Context.RandomSeed;const auto tick=f.Context.Clock->Ticks;auto thread=object->Thread.get();
  try{Checkpoint::Capture(f.Machine,destination,false);}catch(const std::runtime_error& e){captureRefused=std::string(e.what()).find("dispatch")!=std::string::npos;}
  try{Checkpoint::Restore(f.Machine,*house);}catch(const std::runtime_error& e){restoreRefused=std::string(e.what()).find("dispatch")!=std::string::npos;}
  guarded.push_back(!f.Machine.EntityTickRunning()&&!f.Machine.CanCaptureTS1RemovalState()&&captureRefused&&restoreRefused&&destination.Find("SIMI",1)->Data==std::vector<uint8_t>({4,5,6})&&f.Context.RandomSeed==seed&&f.Context.Clock->Ticks==tick&&object->Thread.get()==thread);
 };
 f.Machine.OnSimanticsException=[&](VMEntity* actor,VMEntity* callee,const std::string& message){if(actor==object&&callee==object&&message=="injected temporary check failure"){++exceptions;probe();}};
 f.Machine.OnDialog=[&](Model::VMDialogInfo& info){if(info.Title=="SimAntics Exception!"){++dialogs;probe();}};
 VMThread::EvaluateCheck(&f.Context,object,frame);
 need(exceptions==1&&dialogs==1&&guarded==std::vector<bool>({true,true}),"temporary check recovery callbacks published an unfinished continuation");
 need(f.Machine.CanCaptureTS1RemovalState()&&f.Machine.GetTS1PendingRemovalIDs()==f.ids()&&!object->Dead&&object->Thread->Stack.empty(),"temporary check recovery leaked guard or changed real execution state");
 auto after=f.raw();Checkpoint::Capture(f.Machine,after,false);need(after.Find("ODHI",1)->Data==raw.Find("ODHI",1)->Data,"completed temporary check recovery changed native continuation");
}
int main(){try{VM::UseWorld(false);pausedRoundtrip();malformedTail();oldDefaultAndPreparedCommit();temporaryCheckBlocksPublication();malformedLiveQueueBlocksCapture();temporaryCheckRecoveryBlocksPublication();std::cout<<"PASS TS1 pending-removal checkpoint: "<<checks<<" checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
