// SPDX-License-Identifier: MPL-2.0
// Direct Capture must synchronize the only financial authority, classic SIMI.
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/engine/VMThread.h"
#include "simantics/primitives/VMTS1Budget.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "simantics/utils/VMTS1BudgetPersistence.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/SIMI.h"
#include "files/formats/iff/chunks/OBJM.h"
#include "files/formats/iff/chunks/OBJMWriter.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "files/formats/iff/chunks/ODMP.h"
#include "files/Hash.h"
#include "content/ContentManager.h"
#include "content/TS1NeighbourProvider.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static int checks=0;static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
template<class F>static void rejected(F f,const char* why){bool caught=false;try{f();}catch(const std::exception&){caught=true;}need(caught,why);}
static OBJTEntry type(uint32_t guid){st::Writer w(st::Endian::Little);w.WriteUInt32(guid);for(int i=0;i<4;++i)w.WriteUInt16(0);w.WriteUInt16(1);w.WriteUInt16(uint16_t(OBJDType::Normal));w.WriteByte('x');w.WriteByte(0);auto b=w.TakeArray();st::Reader r(b);return OBJTEntry(r,2);}
static void legacyCandidate() {
 VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.GlobalState.resize(64,0);
 IffFile resources;FSO::Content::GameObjectResource resource(&resources,nullptr,nullptr,"legacy-finance",true);
 OBJD def;def.GUID=0x87654321;FSO::Content::GameObject object;object.OBJ=&def;object.GUID=def.GUID;object.Resource=&resource;
 VMGameObject entity(&object,nullptr);Entities::VMMultitileGroup group;entity.MultitileGroup=&group;group.AddObject(&entity);vm.AddEntity(&entity);entity.Thread=std::make_unique<VMThread>(&ctx,&entity,16);
 auto typ=type(def.GUID);OBJMRecordOut record;record.ObjectID=entity.ObjectID;record.TypeID=1;record.Instance.OBJT=&typ;
 record.Instance.Level=1;
 record.Instance.ObjectData.resize(OBJMInstance::ObjectDataCount);record.Instance.ObjectData[11]=entity.ObjectID;
 IffRawFile raw;raw.Set("ObjM",1,WriteOBJMChunk(0,0x49,{record},{}));
 SIMI original;original.Version=0x49;original.Unknown3=17;original.Unknown4=9;original.GlobalData.resize(38);original.GlobalTail.resize(26);original.BudgetDays.resize(6);
 st::Writer w(st::Endian::Little);original.Write(nullptr,w);auto legacy=w.ToArray();legacy.resize(174);legacy.resize(198,0);raw.Set("SIMI",1,legacy);
 vm.SetGlobalValue(13,590);VMTS1InteractionCheckpoint::Capture(vm,raw,false,false,10);need(raw.Find("ODHI",1)->Data[0]==10,"legacy fixture checkpoint version");
 SIMI strict;st::Reader strictReader(legacy);rejected([&]{strict.Read(nullptr,strictReader);},"standalone marker-only data accepted as authentic");
 auto originalOnly=raw;originalOnly.Remove("ODHI",1);originalOnly.Remove("ODMP",1);IffFile noNative;noNative.Read(originalOnly.Write());rejected([&]{noNative.Get<SIMI>(1);},"original without ODHI accepted marker-only candidate");
 IffFile candidate;candidate.Read(raw.Write());auto simi=candidate.Get<SIMI>(1);need(simi&&simi->LegacyBudgetCandidate,"explicit legacy envelope selected only as candidate");
 auto broken=raw;broken.Remove("ODMP",1);broken.Find("ODHI",1)->Data.pop_back();
 IffFile brokenHouse;brokenHouse.Read(broken.Write());need(brokenHouse.Get<SIMI>(1)->LegacyBudgetCandidate,"truncated graph remains provisional after envelope preflight");
 auto brokenObjm=brokenHouse.Get<OBJM>(1);if(!brokenObjm->Prepare([&](uint16_t id){OBJMResource result;if(id==1){result.OBJT=&typ;result.OBJD=&def;}return result;})) throw std::runtime_error("decode rejected graph fixture: "+brokenObjm->DecodeError);
 auto content=FSO::Content::Content::Get();auto priorProvider=std::move(content->Neighborhood);
 content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content);
 auto& provider=*content->Neighborhood;FAMI family;family.Budget=912;provider.GameState.ActiveFamily=&family;
 VMTS1Budget::LoadState(&vm,731,{});auto beforeRejected=vm.TS1Budget;auto threadBeforeRejected=entity.Thread.get();
 rejected([&]{VMTS1InteractionCheckpoint::Restore(vm,brokenHouse,false,true);},"truncated candidate graph accepted");
 need(vm.TS1Budget.Purse==beforeRejected.Purse && vm.TS1Budget.Days==beforeRejected.Days && !vm.TS1Budget.LegacyCandidateValidated && entity.Thread.get()==threadBeforeRejected && provider.GameState.ActiveFamily==&family && family.Budget==912,"rejected graph did not publish budget, family, provider or execution state");
 provider.GameState.ActiveFamily=nullptr;content->Neighborhood=std::move(priorProvider);
 auto objm=candidate.Get<OBJM>(1);need(objm->Prepare([&](uint16_t id){OBJMResource result;if(id==1){result.OBJT=&typ;result.OBJD=&def;}return result;}),"decode legacy object graph");
 auto originalThread=entity.Thread.get();int32_t promoted=0;
 VMTS1InteractionCheckpoint::Restore(vm,candidate,false,true,&promoted);
 need(promoted==590,"legacy graph promotes captured mirrors rather than stale SIMI purse or delta");
 VMTS1Budget::LoadState(&vm,promoted,{});
 need(vm.TS1Budget.LegacyCandidateValidated&&entity.Thread.get()==originalThread,"full validation admits candidate without publishing thread");
 vm.TS1Budget.LegacyPromoted=true;need(VMTS1Budget::Commit(&vm,3,10),"simulated pre-restore EP8 transaction");
 VMTS1InteractionCheckpoint::Restore(vm,candidate);
 need(vm.TS1Budget.Purse==580 && !vm.TS1Budget.LegacyPromoted,"same restore preserves pre-publication transaction and consumes promotion");
 auto rawB=raw;vm.TS1Budget.Initialized=false;vm.SetGlobalValue(13,100);
 VMTS1InteractionCheckpoint::Capture(vm,rawB,false,false,10);VMTS1Budget::LoadState(&vm,580,vm.TS1Budget.Days);
 IffFile candidateB;candidateB.Read(rawB.Write());auto objmB=candidateB.Get<OBJM>(1);
 need(objmB->Prepare([&](uint16_t id){OBJMResource result;if(id==1){result.OBJT=&typ;result.OBJD=&def;}return result;}),"decode second legacy snapshot");
 VMTS1InteractionCheckpoint::Restore(vm,candidateB);
 need(vm.TS1Budget.Purse==100,"later legacy snapshot uses its own saved purse");
 VMTS1InteractionCheckpoint::Capture(vm,raw,false);
 st::Reader upgraded(raw.Find("SIMI",1)->Data);SIMI canonical;canonical.Read(nullptr,upgraded);
 need(raw.Find("ODHI",1)->Data[0]==12&&canonical.BudgetDays.size()==6&&!canonical.LegacyBudgetCandidate,"accepted legacy candidate rewrites authentic SIMI and bound checkpoint");
 auto malformed=legacy;malformed.back()=1;IffRawFile invalid=raw;invalid.Set("SIMI",1,malformed);IffFile invalidHouse;invalidHouse.Read(invalid.Write());rejected([&]{invalidHouse.Get<SIMI>(1);},"nonzero marker-only tail accepted");
}
static void earlyUpgrade() {
 VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.GlobalState.resize(64);vm.TS1=true;
 for (uint32_t version : {0x12u,0x26u,0x33u}) {
  SIMI input;input.Version=version;input.GlobalData.resize(38);input.Unknown3=100;input.Unknown4=-7;input.UnknownTail={0xde,0xad};input.BudgetDays.resize(6);
  st::Writer wire(st::Endian::Little);input.Write(nullptr,wire);auto sourceBytes=wire.ToArray();st::Reader read(sourceBytes);SIMI decoded;decoded.Read(nullptr,read);
  need(decoded.LoadedPurse()==93,"source delta becomes logical balance");
  VMTS1Budget::LoadState(&vm,decoded.LoadedPurse(),{});need(VMTS1Budget::Commit(&vm,3,10),"upgrade fixture transaction");
  IffRawFile raw;raw.Set("SIMI",1,sourceBytes);FSO::SimAntics::Utils::SyncTS1Budget(vm,raw);
  st::Reader check(raw.Find("SIMI",1)->Data);SIMI saved;saved.Read(nullptr,check);
  need(saved.Version==0x34 && saved.Unknown3==83 && saved.Unknown4==0 && saved.LoadedPurse()==83 && saved.BudgetDays[0].Values()[3]==10 && saved.UnknownTail==input.UnknownTail,"old SIMI upgrades to retaining ledger without replaying delta");
  if(version<0x25) need(saved.LotValue==4500 && saved.ObjectsValue==0 && saved.ArchitectureValue==0,"early metadata defaults survive upgrade");
 }
}
int main(){try{
 VM::UseWorld(false);VMContext ctx(nullptr);VM vm(&ctx,nullptr);vm.GlobalState.resize(64,0);
 IffFile resources;FSO::Content::GameObjectResource resource(&resources,nullptr,nullptr,"finance-capture",true);
 OBJD def;def.GUID=0x12345678;FSO::Content::GameObject object;object.OBJ=&def;object.GUID=def.GUID;object.Resource=&resource;
 VMGameObject e(&object,nullptr);Entities::VMMultitileGroup group;e.MultitileGroup=&group;group.AddObject(&e);vm.AddEntity(&e);e.Thread=std::make_unique<VMThread>(&ctx,&e,16);
 IffRawFile raw;raw.Set("ObjM",1,{1,2,3});
 // Historic fixtures with no initialized owner and no SIMI still capture.
 VMTS1InteractionCheckpoint::Capture(vm,raw,false);need(raw.Find("ODHI",1)&&raw.Find("ODHI",1)->Data[0]==12,"uninitialized direct overlay uses v12 without finance");
 VMTS1Budget::DaysType days{};days[0][3]=55;days[1][4]=-123;days[5][7]=777;VMTS1Budget::LoadState(&vm,0x12345678,days);
 auto untouched=raw.Write();rejected([&]{VMTS1InteractionCheckpoint::Capture(vm,raw,false);},"initialized capture without SIMI accepted");need(raw.Write()==untouched,"missing SIMI failure mutated IFF");
 SIMI original;original.Version=0x49;original.GlobalData.resize(38,0);original.GlobalTail.resize(26,0);original.Unknown3=17;original.SchedulerCursor=-8;original.GUID1=0x1234;original.UnknownTail={0xde,0xad,0xbe,0xef};original.BudgetDays.resize(6);
 st::Writer source(st::Endian::Little);original.Write(nullptr,source);raw.Set("SIMI",1,source.ToArray());
 auto invalid=raw;invalid.Set("FSOV",1,{1});untouched=invalid.Write();rejected([&]{VMTS1InteractionCheckpoint::Capture(vm,invalid,false);},"conflicting graph accepted");need(invalid.Write()==untouched,"later failure published partial synchronized SIMI");
 auto bad=raw;bad.Find("SIMI",1)->Data.resize(5);untouched=bad.Write();rejected([&]{VMTS1InteractionCheckpoint::Capture(vm,bad,false);},"truncated SIMI accepted");need(bad.Write()==untouched,"bad SIMI changed capture input");
 VMTS1InteractionCheckpoint::Capture(vm,raw,false);auto native=raw.Find("ODHI",1);need(native&&native->Data[0]==12,"financially bound direct capture is v12");
 auto rawSimi=raw.Find("SIMI",1);st::Reader sr(rawSimi->Data);SIMI saved;saved.Read(nullptr,sr);
 need(saved.Unknown3==0x12345678&&saved.BudgetDays[0].Values()==days[0]&&saved.BudgetDays[1].Values()==days[1]&&saved.BudgetDays[5].Values()==days[5]&&saved.SchedulerCursor==-8&&saved.GUID1==0x1234&&saved.UnknownTail==original.UnknownTail,"direct capture synchronized purse and six records without altering metadata");
 auto entry=type(def.GUID);
 auto makeHouse=[&](const std::vector<uint8_t>& simiBytes){auto house=std::make_unique<IffFile>();auto simi=new SIMI();simi->ChunkID=1;simi->ChunkProcessed=true;st::Reader input(simiBytes);simi->Read(nullptr,input);house->AddChunk(simi);auto objm=new OBJM();objm->ChunkID=1;objm->ChunkProcessed=true;objm->Decoded=true;objm->Version=0x49;objm->SourceSignature=FSO::Files::Hash::CalculateHash(raw.Find("ObjM",1)->Data);auto instance=std::make_shared<OBJMInstance>();instance->OBJT=&entry;objm->ObjectData[e.ObjectID].Instance=instance;house->AddChunk(objm);auto odhi=new ODHI();odhi->ChunkID=1;odhi->ChunkProcessed=true;odhi->Data=native->Data;house->AddChunk(odhi);auto odmp=new ODMP();odmp->ChunkID=1;odmp->ChunkProcessed=true;odmp->Data=raw.Find("ODMP",1)->Data;house->AddChunk(odmp);return house;};
 auto house=makeHouse(rawSimi->Data);VMTS1Budget::LoadState(&vm,9,{});
 auto preflightThread=e.Thread.get();auto preflightBudget=vm.TS1Budget;int32_t unused=0;
 VMTS1InteractionCheckpoint::Restore(vm,*house,false,true,&unused);
 need(e.Thread.get()==preflightThread&&vm.TS1Budget.Purse==preflightBudget.Purse&&vm.TS1Budget.Days==preflightBudget.Days,"v11 full graph validation leaves execution and finance unpublished");
 auto malformedGraph=makeHouse(rawSimi->Data);malformedGraph->Get<ODMP>(1)->Data.back()^=1;
 rejected([&]{VMTS1InteractionCheckpoint::Restore(vm,*malformedGraph,false,true,&unused);},"v11 malformed graph supplement accepted");
 need(e.Thread.get()==preflightThread&&vm.TS1Budget.Purse==preflightBudget.Purse&&vm.TS1Budget.Days==preflightBudget.Days,"v11 graph rejection leaves execution and finance untouched");
 VMTS1InteractionCheckpoint::Restore(vm,*house);
 need(VMTS1Budget::GetRawBudget(&vm)==0x12345678&&vm.TS1Budget.Days==days,"direct capture native restore uses synchronized SIMI finance");
 auto tampered=rawSimi->Data;tampered.back()^=1;auto badHouse=makeHouse(tampered);auto before=vm.TS1Budget;auto oldThread=e.Thread.get();
 rejected([&]{VMTS1InteractionCheckpoint::Restore(vm,*badHouse);},"stale SIMI binding accepted");need(vm.TS1Budget.Purse==before.Purse&&vm.TS1Budget.Days==before.Days&&e.Thread.get()==oldThread,"bad SIMI binding published partial state");
 legacyCandidate();
 earlyUpgrade();
 std::printf("PASS %d TS1 budget persistence checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
