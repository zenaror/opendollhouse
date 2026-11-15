// SPDX-License-Identifier: MPL-2.0
// Asset-backed diagnostic. Normal App load/frame/save consumers; no state/result
// preparation. The forward study flow acknowledges only the cooking-skill
// notice also observed when reopening the original DVD save, through its UI.
// Run fresh as a SEPARATE PROCESS after forward-save. Supplied
// SAVE/CACHE roots must be disposable copies prepared by the coordinator.
#include "app/App.h"
#include "app/front/FrontEnd.h"
#include "app/front/LotSave.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMClock.h"
#include "simantics/model/VMStackObjectVariable.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/utils/VMTS1InteractionCheckpoint.h"
#include "files/formats/iff/IffRawFile.h"
#include "files/formats/iff/chunks/OBJM.h"
#include "files/formats/iff/chunks/OBJT.h"
#include "files/formats/iff/chunks/ODHI.h"
#include "tools/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace simapp;
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace fs=std::filesystem;
static void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static std::string boolean(bool value){return value?"true":"false";}
template<class C>static std::string numbers(const C& values){std::ostringstream out;out<<'[';bool comma=false;for(auto value:values){if(comma)out<<',';comma=true;out<<+value;}return out.str()+']';}
struct Report{
 std::vector<std::pair<std::string,std::string>> fields;
 void raw(const std::string& key,const std::string& value){fields.push_back({key,value});}
 void text(const std::string& key,const std::string& value){raw(key,json_utf8(value));}
 void write(const fs::path& path){std::ofstream out(path);require(bool(out),"cannot write report");out<<"{\n";for(size_t i=0;i<fields.size();++i)out<<json_utf8(fields[i].first)<<':'<<fields[i].second<<(i+1<fields.size()?",\n":"\n");out<<"}\n";require(bool(out),"report write failed");}
};
static std::string persons(LotSession& session){
 std::ostringstream out;out<<'[';bool comma=false;
 for(auto e:session.VM()->Entities){auto a=dynamic_cast<VMAvatar*>(e);if(!a||a->Dead)continue;if(comma)out<<',';comma=true;
  const auto p=a->Position();auto t=a->Thread.get();auto active=t?t->ActiveAction():nullptr;
  out<<"{\"oid\":"<<a->ObjectID<<",\"guid\":"<<a->Object->OBJ->GUID<<",\"position\":["<<p.x<<','<<p.y<<','<<int(p.Level)<<"],\"container\":"<<(a->Container?a->Container->ObjectID:0)
   <<",\"posture\":"<<a->GetPersonData(VMPersonDataVariable::Posture)<<",\"cooking\":"<<a->GetPersonData(VMPersonDataVariable::CookingSkill)
   <<",\"use_count\":"<<a->GetValue(VMStackObjectVariable::UseCount)<<",\"active_uid\":"<<(active?int(active->UID):-1)<<",\"queue\":[";
  if(t)for(size_t i=0;i<t->Queue.size();++i){auto q=t->Queue[i];if(i)out<<',';out<<"["<<q->UID<<','<<(q->Callee?q->Callee->ObjectID:0)<<','<<(q->ActionRoutine?q->ActionRoutine->ID:0)<<']';}
  out<<"],\"frames\":[";
  if(t)for(size_t i=0;i<t->Stack.size();++i){auto f=t->Stack[i];if(i)out<<',';out<<'['<<(f->Routine?f->Routine->ID:0)<<','<<f->InstructionPointer<<','<<(f->CodeOwner?f->CodeOwner->OBJ->GUID:0)<<','<<(f->Callee?f->Callee->ObjectID:0)<<','<<int(f->SpecialResult)<<','<<boolean(f->ActionTree)<<']';}
  std::vector<int> motives;for(int i=0;i<16;++i)motives.push_back(a->GetMotiveData(VMMotive(i)));
  out<<"],\"motives\":"<<numbers(motives)<<",\"animation\":";
  auto anim=a->CurrentAnimationState();if(anim)out<<"{\"name\":"<<json_utf8(anim->Anim->Name)<<",\"frame\":"<<anim->CurrentFrame<<",\"events_run\":"<<int(anim->EventsRun)<<",\"queued_events\":"<<numbers(anim->EventQueue)<<'}';else out<<"null";out<<'}';
 }
 return out.str()+']';
}
struct First{uint16_t tree=0,pc=0;uint32_t owner=0;bool observed=false;size_t instructions=0,actionInstructions=0;};
// Compare the decoded original state with the actual live VM BEFORE any tick.
// Names/family metadata and derived spatial words have other owners; this gate
// compares execution, object uses, positions, attributes, supported skill/motive
// banks and queues, without pretending all original runtime fields are known.
static std::map<int,First> verifyClassic(LotSession& session,Report& report){
 auto house=session.LoadedHouseResource();auto objm=house->Get<OBJM>(1);auto objt=house->Get<OBJT>(0);
 require(objm&&objm->Decoded&&objt,"classic records were not fully decoded");
 require(!house->HasChunkType<ODHI>(),"classic inspection received an ODHI save");
 require(session.Updates==0,"classic App load advanced before pre-tick inspection");
 std::map<int,uint32_t> types;for(const auto& type:objt->Entries)types[type.TypeID]=type.GUID;
 std::map<int,First> watched;size_t frames=0,queues=0,uses=0;
 for(const auto& item:objm->ObjectData){const auto& inst=*item.second.Instance;auto e=session.VM()->GetObjectById(int16_t(item.first));
  const auto why="original oid"+std::to_string(item.first)+": ";
  require(e&&!e->Dead&&e->Object->OBJ->GUID==inst.OBJT->GUID,why+"identity");auto t=e->Thread.get();require(t,why+"missing thread");
  require(t->Stack.size()==inst.Stack.size(),why+"stack count");
  for(size_t i=0;i<inst.Stack.size();++i){const auto& source=inst.Stack[i];const auto& f=t->Stack[i];
   require(f&&!f->IsRoutingFrame()&&f->Routine&&f->Routine->ID==uint16_t(source.TreeID)&&f->InstructionPointer==uint16_t(source.NodeID)&&f->CodeOwner->OBJ->GUID==types.at(source.CodeOwnerObjType),why+"frame "+std::to_string(i)+" tree/PC/owner");
   require(f->StackObjectID()==source.StackObjectID,why+"stack object scalar");
   require(f->Args.size()>=source.Parameters.size()&&std::equal(source.Parameters.begin(),source.Parameters.end(),f->Args.begin()),why+"arguments");
   require(f->Locals.size()>=source.Locals.size()&&std::equal(source.Locals.begin(),source.Locals.end(),f->Locals.begin()),why+"locals");++frames;
  }
  require(std::equal(inst.TempRegisters.begin(),inst.TempRegisters.end(),t->TempRegisters.begin()),why+"temp registers");
  require(e->AttributeData().size()>=inst.Attributes.size()&&std::equal(inst.Attributes.begin(),inst.Attributes.end(),e->AttributeData().begin()),why+"attributes");
  const auto p=e->Position();
  const auto expected=(inst.X==-16&&inst.Y==-16)?FSO::LotView::Model::LotTilePos::OUT_OF_WORLD:
   FSO::LotView::Model::LotTilePos(int16_t(inst.X),int16_t(inst.Y),int8_t(inst.Level));
  require(p==expected,why+"saved position (classic OOW sentinel normalized)");
  require((e->Container?e->Container->ObjectID:0)==item.second.ContainerID,why+"container");
  if(!inst.HasPersonData)continue;
  auto a=dynamic_cast<VMAvatar*>(e);require(a,why+"person identity");const auto& person=inst.PersonData;
  for(int index:{0,10,11,12,15,17,18})if(size_t(index)<person.PersonData.size())require(a->GetPersonData(VMPersonDataVariable(index))==person.PersonData[index],why+"posture/skill "+std::to_string(index));
  for(int i=0;i<16;++i)require(a->GetMotiveData(VMMotive(i))==std::lround(person.MotiveData[i]),why+"motive "+std::to_string(i));
  const bool active=person.ActiveInteraction.IsValid();require(t->ActiveQueueBlock==(active?0:-1)&&t->Queue.size()==person.InteractionQueue.size()+size_t(active),why+"active/queued count");
  auto checkAction=[&](size_t index,const OBJMInteraction& source){auto actual=t->Queue.at(index);require(actual&&actual->UID==source.UID&&actual->Callee&&actual->Callee->ObjectID==source.TargetID&&actual->ActionRoutine->ID==uint16_t(source.ActionTreeID)&&actual->InteractionNumber==source.TTAIndex,why+"queued action identity");++queues;};
  if(active)checkAction(0,person.ActiveInteraction);for(size_t i=0;i<person.InteractionQueue.size();++i)checkAction(i+size_t(active),person.InteractionQueue[i]);
  std::vector<int> callees(t->Stack.size(),e->ObjectID);
  for(const auto& use:person.ObjectUses){require(use.StackLength>0&&size_t(use.StackLength)<=callees.size(),why+"use depth");for(size_t i=size_t(use.StackLength-1);i<callees.size();++i)callees[i]=use.TargetID;++uses;}
  for(size_t i=0;i<callees.size();++i)require(t->Stack[i]->Callee&&t->Stack[i]->Callee->ObjectID==callees[i],why+"object use callee");
  if(active&&!t->Stack.empty()){const auto& top=t->Stack.back();watched[e->ObjectID]={top->Routine->ID,top->InstructionPointer,top->CodeOwner->OBJ->GUID,false,0,0};}
 }
 report.raw("classic_instances",std::to_string(objm->ObjectData.size()));report.raw("classic_frames_compared",std::to_string(frames));report.raw("classic_actions_compared",std::to_string(queues));report.raw("classic_uses_compared",std::to_string(uses));report.raw("classic_pre_tick_match","true");return watched;
}
static void click(App& app,int x,int y){AppInput i;i.DesktopPointer=true;i.TouchX=x;i.TouchY=y;i.TouchDown=i.Touching=true;app.Frame(i,0);i.TouchDown=i.Touching=false;i.TouchUp=true;app.Frame(i,0);}
static void load(App& app,int house){app.Front()->FinishBootNow();app.Speed=0;app.Frame(AppInput{},0);int x=0,y=0;require(app.ScriptHousePos(house,x,y),"house absent");click(app,x,y);for(int i=0;i<240&&!app.Session()&&app.CurrentMode()!=App::Mode::Error;++i)app.Frame(AppInput{},0);require(app.Session()&&app.CurrentMode()==App::Mode::Live,"App lot load failed: "+app.Log);require(app.Session()->Updates==0,"load advanced simulation before inspection");}
// Compare the production codec's capture of the LIVE VM, not merely the bytes
// loaded from disk. Ignore ONLY the ObjM file signature at bytes4..7; saving
// rewrites that classic envelope. Version and every execution field remain exact.
static std::vector<uint8_t> liveExecution(LotSession& session){IffRawFile raw;std::string error;require(raw.Load(session.LoadedHousePath,&error),error);Engine::Utils::VMTS1InteractionCheckpoint::Capture(*session.VM(),raw,false);auto chunk=raw.Find("ODHI",1);require(chunk&&chunk->Data.size()>8,"live capture missing execution state");auto bytes=chunk->Data;std::fill(bytes.begin()+4,bytes.begin()+8,0);return bytes;}
static std::vector<uint8_t> read(const fs::path& path){std::ifstream in(path,std::ios::binary);require(bool(in),"cannot read expected state");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
static void write(const fs::path& path,const std::vector<uint8_t>& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(bool(out),"cannot write expected state");}
static void advance(App& app,int wanted,std::map<int,First>& watched,Report& report){
 auto session=app.Session();auto vm=session->VM();const auto before=session->Updates;const int resets=session->Resets;std::string traceError;
 std::vector<std::string> skillNotices;
 vm->OnTraceInstruction=[&](VMStackFrame* f,uint16_t,int){if(!f||!f->Caller||!f->Routine)return;auto found=watched.find(f->Caller->ObjectID);if(found==watched.end())return;auto& state=found->second;
  if(!state.observed){
   if(f->Routine->ID!=state.tree||f->InstructionPointer!=state.pc||f->CodeOwner->OBJ->GUID!=state.owner)
    traceError="first instruction differs from saved continuation oid"+std::to_string(f->Caller->ObjectID)+" expected tree/PC "+std::to_string(state.tree)+"/"+std::to_string(state.pc)+" actual "+std::to_string(f->Routine->ID)+"/"+std::to_string(f->InstructionPointer);
   state.observed=true;
  }
  ++state.instructions;if(f->ActionTree)++state.actionInstructions;
 };
 int x=0,y=0;require(app.ScriptButtonPos("speed1",x,y),"normal speed control missing");click(app,x,y);
 for(int frames=0;session->Updates-before<wanted&&frames<wanted*4+120;++frames){
  require(app.Session()==session&&app.CurrentMode()==App::Mode::Live,"normal advance changed lot");
  require(!vm->Departure.caller,"normal advance reached travel boundary");
  if(std::any_of(session->Dialogs.begin(),session->Dialogs.end(),[](const auto& d){return d.Block;})){
   const auto& notice=session->Dialogs.front();
   const bool knownCookingNotice=notice.Block&&notice.Title=="Congratulations!"&&
    notice.Message.find(" has received 1 Cooking Skill Point.")!=std::string::npos;
   if(knownCookingNotice){
    require(skillNotices.size()<4,"repeated cooking notices exceed bounded study flow");
    const auto message=notice.Message;app.Frame(AppInput{},0);
    int x=0,y=0;require(app.ScriptButtonPos("simdlg0",x,y),"known cooking notice has no normal OK control");
    click(app,x,y);skillNotices.push_back(message);
    require(traceError.empty()&&session->Resets==resets,"continuation changed unexpectedly while acknowledging notice");
    continue;
   }
   std::ostringstream dialogs;dialogs<<'[';bool comma=false;
   for(const auto& d:session->Dialogs)if(d.Block){if(comma)dialogs<<',';comma=true;dialogs<<"{\"title\":"<<json_utf8(d.Title)<<",\"message\":"<<json_utf8(d.Message)<<'}';}
   report.raw("blocking_dialogs",dialogs.str()+']');report.raw("updates_before_dialog",std::to_string(session->Updates-before));report.raw("at_dialog",persons(*session));
   require(false,"normal advance reached blocking dialog; no response submitted");
  }
  app.Frame(AppInput{},1000./60.);
  require(traceError.empty(),traceError);
  require(session->Resets==resets,"SimAntics reset during normal advancement");
 }
 vm->OnTraceInstruction=nullptr;require(session->Updates-before>=wanted,"normal advance stalled before limit");
 std::ostringstream notices;notices<<'[';for(size_t i=0;i<skillNotices.size();++i){if(i)notices<<',';notices<<json_utf8(skillNotices[i]);}report.raw("acknowledged_cooking_notices",notices.str()+']');
 std::ostringstream observations;observations<<'[';bool comma=false;
 for(const auto& entry:watched){if(comma)observations<<',';comma=true;const auto& state=entry.second;observations<<"{\"oid\":"<<entry.first<<",\"first_saved_PC_executed\":"<<boolean(state.observed)<<",\"instructions\":"<<state.instructions<<",\"action_instructions\":"<<state.actionInstructions<<'}';}
 report.raw("execution_observations",observations.str()+']');report.raw("advanced_updates",std::to_string(session->Updates-before));
 for(const auto& entry:watched)require(entry.second.observed&&entry.second.actionInstructions>0,"saved active action did not execute during bounded advancement");
 report.raw("after_advance",persons(*session));report.raw("resets",std::to_string(session->Resets));
}
int main(int argc,char** argv){Report report;fs::path output;try{
 require(argc==8||argc==9,"usage: probe inspect|forward-save|fresh DATA SAVE CACHE HOUSE REPORT STATE [updates=600]");
 const std::string mode=argv[1];const fs::path data=fs::weakly_canonical(argv[2]),save=fs::weakly_canonical(argv[3]),cache=fs::weakly_canonical(argv[4]);output=argv[6];
 require(mode=="inspect"||mode=="forward-save"||mode=="fresh","unknown mode");
 auto inside=[](const fs::path& a,const fs::path& b){auto relative=a.lexically_relative(b);return !relative.empty()&&*relative.begin()!="..";};
 require(data!=save&&!inside(save,data)&&!inside(data,save)&&!inside(cache,data)&&!inside(cache,save),"DATA/SAVE/CACHE roots must be separate");
 require(!inside(fs::weakly_canonical(output),data)&&!inside(fs::weakly_canonical(output),save),"report belongs outside DATA/SAVE");
 const fs::path state=argv[7];if(mode!="inspect")require(!inside(fs::weakly_canonical(state),data)&&!inside(fs::weakly_canonical(state),save),"state artifact belongs outside DATA/SAVE");
 const int house=std::stoi(argv[5]),updates=argc==9?std::stoi(argv[8]):600;require(updates>0&&updates<=36000,"updates must be1..36000");
 report.text("mode",mode);report.text("scope","asset-backed App consumer; no preparation setters; only known cooking notices receive normal UI OK; fresh requires separate invocation");
 App app;App::Config cfg;cfg.Desktop=true;cfg.SkipIntro=true;cfg.VMBudgetMs=0;cfg.LanguageCode=1;cfg.DataRoot=data.string();cfg.SaveRoot=save.string();cfg.CacheDir=cache.string();require(app.Init(cfg),"App init failed");load(app,house);
 auto session=app.Session();report.text("loaded_house",session->LoadedHousePath);report.raw("pre_tick_updates",std::to_string(session->Updates));report.raw("before",persons(*session));report.raw("before_budget",std::to_string(session->Budget()));
 auto watched=mode=="fresh"?std::map<int,First>{}:verifyClassic(*session,report);
 if(mode=="inspect"){report.text("status","PASS_INSPECT_ONLY");report.text("limit","No forward execution, Save or fresh acceptance from this mode.");}
 else if(mode=="forward-save"){
  require(!watched.empty(),"forward-save requires original active actions");advance(app,updates,watched,report);
  const int budget=session->Budget();const auto before=liveExecution(*session);std::string error,log;
  require(front::SaveLot(*session,error,log),"SaveLot failed: "+error);require(app.Front()->SaveNeighbourhood(),"SaveNeighbourhood failed: "+app.Front()->Error);
  const auto after=liveExecution(*session);report.raw("save_live_execution_unchanged",boolean(before==after));require(before==after&&session->Budget()==budget,"Save changed live execution or budget");
  write(state,after);report.raw("expected_execution_bytes",std::to_string(after.size()));report.text("expected_execution_file",state.string());report.text("status","PASS_FORWARD_SAVE_PENDING_FRESH");
 }
 else{
  require(session->LoadedHouseResource()->HasChunkType<ODHI>(),"fresh input has no ODHI");auto expected=read(state),actual=liveExecution(*session);const bool equal=actual==expected;
  if(!equal){
   write(state.string()+".fresh-actual",actual);
   std::ostringstream entities;entities<<'[';bool comma=false;
   for(auto e:session->VM()->Entities)if(e&&!e->Dead&&!e->GhostImage&&e->Object&&e->Object->OBJ&&e->Thread){
    if(comma)entities<<',';comma=true;entities<<"{\"oid\":"<<e->ObjectID<<",\"guid\":"<<e->Object->OBJ->GUID<<",\"name\":"<<json_utf8(e->ToString())<<'}';
   }
   report.raw("live_participants",entities.str()+']');report.text("actual_execution_file",state.string()+".fresh-actual");
  }
  report.raw("fresh_live_execution_equal",boolean(equal));report.raw("expected_execution_bytes",std::to_string(expected.size()));report.raw("actual_execution_bytes",std::to_string(actual.size()));
  if(!equal){size_t i=0;while(i<std::min(expected.size(),actual.size())&&expected[i]==actual[i])++i;report.raw("first_different_byte",std::to_string(i));}
  require(equal,"fresh live threads/queues/routes/attributes/clock/motives/animation differ from pre-close capture");
  report.text("status","PASS_FRESH_EXACT_EXECUTION");report.text("limit","Exact state at fresh boundary; no claim of universal action or visual parity.");
 }
 report.write(output);std::cout<<"PROBE_REPORT "<<output<<'\n';return 0;
 }catch(const std::exception& error){report.text("status","FAIL_OR_BLOCKED");report.text("error",error.what());if(!output.empty())try{report.write(output);}catch(...){}std::cerr<<error.what()<<'\n';return 1;}}
