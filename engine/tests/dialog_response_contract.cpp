/* SPDX-License-Identifier: MPL-2.0 */
// Resource-free causal service fixture, not gameplay. The actual creator name
// and checkpoint implementations are included to test their private boundary
// without adding a public testing protocol or creating commercial characters.
#include "simantics/primitives/VMTS1MakeNewCharacter.cpp"
#include "simantics/utils/VMTS1InteractionCheckpoint.cpp"
#include "simantics/primitives/VMDialogPrivateStrings.h"
#include "files/formats/iff/chunks/CTSS.h"
#include "content/TS1PersonalText.h"
#include <filesystem>
#include <chrono>
#include <iostream>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void Rejects(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Require(rejected,why);}
static void answer(VMStackFrame& frame,const std::string& input){auto wait=std::make_shared<VMDialogResult>();wait->Type=VMDialogType::TextEntry;wait->Responded=true;wait->ResponseText=input;frame.Thread->BlockingState=wait;VMDialogOperand operand;operand.Type=VMDialogType::TextEntry;Require(VMDialogPrivateStrings::ExecuteGeneric(&frame,&operand,nullptr)==VMPrimitiveExitCode::GOTO_TRUE,"text response continuation");Require(!frame.Thread->BlockingState,"text response remains async");}
static std::string committedInputName(VMStackFrame& frame,uint8_t expectedLanguage,bool expectedUTF8){
 FSO::Content::TS1::TS1CharacterFiles::NewCharacter nc;nc.GUID=0x22334455;
 ReadNewCharacterName(&frame,nc);
 const auto chunks=FSO::Content::TS1::TS1CharacterFiles::Clone({},nc,"user00001");
 const auto found=std::find_if(chunks.begin(),chunks.end(),[](const auto& c){return c.Type=="CTSS"&&c.ID==2000;});Require(found!=chunks.end(),"actual character writer omitted name");
 CTSS text;st::Reader bytes(found->Data);text.Read(nullptr,bytes);auto resolved=FSO::Content::TS1::ResolvePersonalText(&text);
 Require(resolved.SourceLanguage==expectedLanguage&&resolved.UTF8==expectedUTF8,"new name encoding provenance");return resolved.Value(0);
}
// Generate a real v7 snapshot through the v7 codec. Retagging today's v8
// capture would retain per-thread async-presence bytes absent from v7/v6.
static std::vector<uint8_t> captureVersion7(VM& vm,IffRawFile& house){
 st::Writer body(st::Endian::Little);Archive fields(body);InteractionCheckpointCodec codec(vm,fields,7);
 for(auto e:vm.Entities)if(e&&!e->Dead&&!e->GhostImage&&e->Object&&e->Object->OBJ){Require(bool(e->Thread),"legacy fixture thread missing");codec.include(e);}
 std::set<int16_t> done;
 while(done.size()<codec.participants.size())for(auto& entry:codec.participants){
  if(!done.insert(entry.first).second)continue;
  Record record;record.entity=entry.second;codec.record(record);
 }
 // Read the current header, then remove its v10 scheduler-authority byte
 // before combining it with the genuinely encoded v7 body.
 IffRawFile captured;captured.Set("ObjM",1,house.Find("ObjM",1)->Data);
 VMTS1InteractionCheckpoint::Capture(vm,captured,false,false,10);const auto& modern=captured.Find("ODHI",1)->Data;
 st::Reader input(modern);Require(input.u32()==10,"modern fixture version");input.u32();Archive header(input);
 bool departing=false;header.value(departing);Require(!departing,"legacy fixture unexpectedly departing");
 bool schedulerBound=false;header.value(schedulerBound);Require(!schedulerBound,"direct fixture unexpectedly bound");
 VMClock clock;uint64_t seed;ClockState(header,clock,seed);bool ready,alternate,buildBuy;
 header.value(ready);header.value(alternate);header.value(buildBuy);
 std::vector<int16_t> globals;header.vector(globals,4096);std::string text;header.string(text,VM::TS1TextInputMaxBytes);
 std::vector<uint8_t> prefix(modern.begin(),modern.begin()+input.Position());prefix.erase(prefix.begin()+9);prefix[0]=7;
 st::Writer bytes(st::Endian::Little);bytes.WriteBytes(prefix);
 bytes.WriteUInt32(done.size());bytes.WriteBytes(body.ToArray());return bytes.ToArray();
}
int main(){try{
 VMEntity::UseWorld=false;
 auto content=FSO::Content::Content::Get();const auto oldPath=content->SimsCompleteDir;
 IffFile definitions;FSO::Content::GameObjectResource resource(&definitions,nullptr,nullptr,"text-fixture",true);OBJD definition;definition.GUID=0x12345678;definition.NumAttributes=0;
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 Entities::VMMultitileGroup callerGroup,fatherGroup,motherGroup;callerGroup.Name="Creator";fatherGroup.Name="Father";motherGroup.Name="Mother";
 VMGameObject caller(&object,nullptr);VMAvatar father(&object),mother(&object);caller.MultitileGroup=&callerGroup;father.MultitileGroup=&fatherGroup;mother.MultitileGroup=&motherGroup;callerGroup.Objects.push_back(&caller);fatherGroup.Objects.push_back(&father);motherGroup.Objects.push_back(&mother);
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;vm.GlobalState.assign(38,0);vm.AddEntity(&caller);caller.Thread=std::make_unique<VMThread>(&context,&caller,0);auto& thread=*caller.Thread;
 VMStackFrame frame;frame.Thread=&thread;frame.Caller=frame.Callee=&caller;frame.StackObject=&father;frame.CodeOwner=&object;
 for(bool temp1:{false,true})for(int response:{0,1,2}){
  thread.TempRegisters.fill(1234);auto wait=std::make_shared<VMDialogResult>();wait->Type=VMDialogType::YesNoCancel;wait->Responded=true;wait->ResponseCode=response;thread.BlockingState=wait;VMDialogOperand operand;operand.Type=VMDialogType::YesNoCancel;operand.Flags=temp1?VMDialogFlags::UseTemp1:static_cast<VMDialogFlags>(0);
  const auto result=VMDialogPrivateStrings::ExecuteGeneric(&frame,&operand,nullptr);const bool yes=response==0;Require(result==(yes?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_FALSE),"No/Cancel continuation");Require(!thread.BlockingState,"answered wait not consumed");const int selected=temp1?1:0;Require(thread.TempRegisters[selected]==(yes?1234:response-1),"No/Cancel requested temp");Require(thread.TempRegisters[1-selected]==1234,"unrequested temp changed");
 }
 for(int response:{0,1}){auto wait=std::make_shared<VMDialogResult>();wait->Type=VMDialogType::YesNo;wait->Responded=true;wait->ResponseCode=response;thread.BlockingState=wait;VMDialogOperand operand;operand.Type=VMDialogType::YesNo;const auto result=VMDialogPrivateStrings::ExecuteGeneric(&frame,&operand,nullptr);Require(result==(response==0?VMPrimitiveExitCode::GOTO_TRUE:VMPrimitiveExitCode::GOTO_FALSE),"YesNo control regressed");}
 const auto previousLanguage=STR::DefaultLangCode;STR::DefaultLangCode=STRLangCode(14);
 const auto rng=context.RandomSeed;const auto objectData=caller.ObjectData;const auto attrs=caller.AttributeData();
 for(const std::string name:{std::string("Cedro"),std::string(u8"José"),std::string(u8"Lia 雪 Ø")}){
  frame.StackObject=&father;answer(frame,name);Require(vm.TS1TextInput==name,"TS1 did not publish common text channel");frame.StackObject=&mother;
  Require(committedInputName(frame,14,true)==name,"shared creation-name writer committed parent's name");
  Require(father.Name()=="Father"&&mother.Name()=="Mother"&&caller.Name()=="Creator","naming altered an existing identity");Require(context.RandomSeed==rng&&caller.ObjectData==objectData&&caller.AttributeData()==attrs,"name channel changed RNG/object state");
 }
 VMContext otherContext(nullptr);VM otherVM(&otherContext,nullptr);otherVM.TS1=true;Require(otherVM.TS1TextInput.empty(),"text input leaked to another VM");
 // Explicit direct names (CAS/import/generators) do not read the VM channel.
 FSO::Content::TS1::TS1CharacterFiles::NewCharacter direct;direct.FirstName="Explicit CAS";const auto directChunks=FSO::Content::TS1::TS1CharacterFiles::Clone({},direct,"user00002");const auto directText=std::find_if(directChunks.begin(),directChunks.end(),[](const auto& c){return c.Type=="CTSS";});Require(FSO::Content::TS1::RawIff::ReadSTRValues(directText->Data)[0]=="Explicit CAS","explicit creation acquired unrelated dialog name");
 vm.TS1=false;frame.StackObject=&father;answer(frame,"Intentional TSO rename");Require(father.Name()=="Intentional TSO rename"&&vm.TS1TextInput==u8"Lia 雪 Ø","nonTS1 intentional rename changed");father.Name("Father");vm.TS1=true;
 // Bad external responses must not clear the pending wait or replace a name.
 for(const std::string invalid:{std::string(4097,'x'),std::string("\xc3",1),std::string("a\0b",3)}){
  auto wait=std::make_shared<VMDialogResult>();wait->Type=VMDialogType::TextEntry;wait->Responded=true;wait->ResponseText=invalid;thread.BlockingState=wait;VMDialogOperand op;op.Type=VMDialogType::TextEntry;const auto old=vm.TS1TextInput;Rejects([&]{VMDialogPrivateStrings::ExecuteGeneric(&frame,&op,nullptr);},"invalid response accepted");Require(thread.BlockingState==wait&&vm.TS1TextInput==old&&father.Name()=="Father","invalid response partially consumed state");thread.BlockingState=nullptr;
 }
 // A synthetic localized UIText table authenticates empty-channel fallback.
 const auto dir=std::filesystem::temp_directory_path()/("odh-text-input-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Require(std::filesystem::create_directory(dir),"fresh fixture directory");std::filesystem::create_directories(dir/"GameData");IffRawFile ui;CTSS labels;for(int i=0;i<5;++i){STRItem item;item.LanguageCode=14;item.Value=i==4?u8"Criança padrão":"button";labels.LanguageSets[13].Strings.push_back(item);}st::Writer labelsBytes(st::Endian::Little);labels.Write(nullptr,labelsBytes);ui.Set("STR#",142,labelsBytes.ToArray(),"ObjDialogs");std::string error;Require(ui.Save((dir/"GameData/UIText.iff").string(),&error),error.c_str());content->SimsCompleteDir=dir.string();answer(frame,"");Require(vm.TS1TextInput.empty(),"empty answer retained stale name");Require(committedInputName(frame,14,true)==u8"Criança padrão","empty input did not use localized original default");content->SimsCompleteDir=(dir/"absent").string();Require(committedInputName(frame,14,true)=="No childname","missing UIText original fallback changed");content->SimsCompleteDir=oldPath;
 // Full native v8 capture/restore, including empty and UTF8 channel values.
 IffRawFile saved;saved.Set("ObjM",1,{0,0,0,0},"synthetic signature only");
 for(const std::string input:{std::string(),std::string(u8"José 雪")}){
  auto liveThread=std::move(caller.Thread);
  Rejects([&]{VMTS1InteractionCheckpoint::Capture(vm,saved,false);},"v8 capture silently omitted a persistent threadless instance");
  caller.Thread=std::move(liveThread);
  vm.TS1TextInput=input;VMTS1InteractionCheckpoint::Capture(vm,saved,false,false,10);const auto odhi=saved.Find("ODHI",1);Require(odhi&&odhi->Data[0]==10,"native name state did not version10");
  IffFile house;auto objm=new OBJM();objm->ChunkID=1;objm->ChunkProcessed=true;objm->Decoded=true;objm->SourceSignature=FSO::Files::Hash::CalculateHash(saved.Find("ObjM",1)->Data);objm->ObjectData[caller.ObjectID]=OBJM::MappedObject{};house.AddChunk(objm);auto chunk=new ODHI();chunk->ChunkID=1;chunk->ChunkProcessed=true;chunk->Data=odhi->Data;house.AddChunk(chunk);vm.TS1TextInput="changed after capture";VMTS1InteractionCheckpoint::Restore(vm,house);Require(vm.TS1TextInput==input,"native roundtrip lost last text including empty");
  // Manual decoded fixture must contain the same persistent identity domain.
  // Missing/additional IDs are rejected before any execution state publishes.
  const auto preservedThread=caller.Thread.get();const auto preservedTime=context.Clock->Ticks;
  objm->ObjectData[32767]=OBJM::MappedObject{};vm.TS1TextInput="unchanged on incomplete graph";
  Rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"v8 accepted a missing participant");
  Require(caller.Thread.get()==preservedThread&&context.Clock->Ticks==preservedTime&&vm.TS1TextInput=="unchanged on incomplete graph","v8 incomplete graph partially published");
  objm->ObjectData.erase(caller.ObjectID);
  Rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"v8 accepted different IDs with equal counts");
  Require(caller.Thread.get()==preservedThread&&vm.TS1TextInput=="unchanged on incomplete graph","v8 changed identity domain partially published");
  objm->ObjectData.erase(32767);objm->ObjectData[caller.ObjectID]=OBJM::MappedObject{};
  const auto good=chunk->Data;chunk->Data.push_back(0);vm.TS1TextInput="unchanged on rejection";const auto time=context.Clock->Ticks;Rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"trailing state accepted");Require(vm.TS1TextInput=="unchanged on rejection"&&context.Clock->Ticks==time,"rejected checkpoint partially published name/clock");chunk->Data=good;
  // Locate the modern text channel for malformed v8 payload gates.
  st::Reader reader(good);reader.u32();reader.u32();Archive header(reader);bool departing;header.value(departing);bool schedulerBound;header.value(schedulerBound);VMClock clock;uint64_t seed;ClockState(header,clock,seed);bool a,b,c;header.value(a);header.value(b);header.value(c);std::vector<int16_t> globals;header.vector(globals,4096);const auto start=reader.Position();std::string oldText;header.string(oldText,VM::TS1TextInputMaxBytes);const auto end=reader.Position();
  // Validate the new bounded channel before publishing any object/clock state.
  chunk->Data=good;for(int j=0;j<4;++j)chunk->Data[size_t(start)+j]=uint8_t(4097u>>(j*8));vm.TS1TextInput="unchanged on oversized channel";Rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"oversized channel accepted");Require(vm.TS1TextInput=="unchanged on oversized channel"&&context.Clock->Ticks==time,"oversized channel partially published");
  if(!input.empty()){chunk->Data=good;chunk->Data[size_t(start)+4]=0xff;vm.TS1TextInput="unchanged on malformed UTF8";Rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"malformed checkpoint UTF8 accepted");Require(vm.TS1TextInput=="unchanged on malformed UTF8"&&context.Clock->Ticks==time,"malformed UTF8 partially published");}
  // Actual v7 record layout, including no v8 async-presence tail bytes.
  vm.TS1TextInput=input;const auto legacy7=captureVersion7(vm,saved);chunk->Data=legacy7;
  vm.TS1TextInput="changed before real v7 restore";VMTS1InteractionCheckpoint::Restore(vm,house);
  Require(vm.TS1TextInput==input,"real v7 layout lost submitted name");
  const auto legacyThread=caller.Thread.get();objm->ObjectData[32767]=OBJM::MappedObject{};vm.TS1TextInput="unchanged on v7 graph rejection";
  Rejects([&]{VMTS1InteractionCheckpoint::Restore(vm,house);},"real v7 accepted incomplete graph");
  Require(caller.Thread.get()==legacyThread&&vm.TS1TextInput=="unchanged on v7 graph rejection","real v7 incomplete graph partially published");objm->ObjectData.erase(32767);
  // v6 differs from this genuine v7 only by the absent text-input header.
  st::Reader legacyReader(legacy7);legacyReader.u32();legacyReader.u32();Archive legacyHeader(legacyReader);
  bool legacyDeparting;legacyHeader.value(legacyDeparting);VMClock legacyClock;uint64_t legacySeed;ClockState(legacyHeader,legacyClock,legacySeed);
  bool legacyA,legacyB,legacyC;legacyHeader.value(legacyA);legacyHeader.value(legacyB);legacyHeader.value(legacyC);std::vector<int16_t> legacyGlobals;legacyHeader.vector(legacyGlobals,4096);
  const auto legacyStart=legacyReader.Position();std::string legacyText;legacyHeader.string(legacyText,VM::TS1TextInputMaxBytes);const auto legacyEnd=legacyReader.Position();
  chunk->Data=legacy7;chunk->Data.erase(chunk->Data.begin()+legacyStart,chunk->Data.begin()+legacyEnd);chunk->Data[0]=6;
  objm->ObjectData.clear();vm.TS1TextInput="not present in v6";VMTS1InteractionCheckpoint::Restore(vm,house);Require(vm.TS1TextInput.empty(),"v6 fabricated prior submitted name");
 }
 STR::DefaultLangCode=previousLanguage;std::filesystem::remove_all(dir);std::cout<<"PASS response branches; TS1 Type3->shared UTF8 name->shared creation-name boundary/CTSS (no Make19 Execute dispatch), existing identities intact; direct/TSO rename; localized empty fallback; invalid-response atomicity; nativev8 roundtrip/realv7 graph and name/v6 compatibility/rejected-state atomicity\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
