// SPDX-License-Identifier: MPL-2.0
// Synthetic codec contract; gameplay persistence requires a separate real-lot run.
// Include the implementation so this focused fixture exercises the actual writer
// and reader without exposing diagnostic hooks in the engine's public ABI.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
// Test-only access to route fields, VM identity mapping and the single-instruction
// consumer. Public VMThread::Tick also runs queue scheduling and subsequent BHAV
// instructions, so it cannot isolate this return edge. No production API changes.
#define private public
#include "simantics/VM.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMThread.h"
#undef private
#include "../sims3ds/src/simantics/utils/VMTS1InteractionCheckpoint.cpp"
#include "simantics/primitives/VMExpression.h"
#include "simantics/primitives/VMDialogPrivateStrings.h"
#include "simantics/model/VMDialogInfo.h"
#include "simantics/utils/VMTS1FamilyLoader.h"
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
static int cases=0;
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++cases;printf("PASS %s\n",name);}
template<class F> static void rejects(F f,const char* name){bool rejected=false;try{f();}catch(const std::runtime_error&){rejected=true;}check(rejected,name);}

static OBJTEntry classicType(uint32_t guid,uint16_t type) {
 st::Writer bytes(st::Endian::Little);bytes.WriteUInt32(guid);
 for(int i=0;i<4;++i)bytes.WriteUInt16(0);
 bytes.WriteUInt16(type);bytes.WriteUInt16(uint16_t(OBJDType::Normal));bytes.WriteByte('x');bytes.WriteByte(0);
 auto raw=bytes.TakeArray();st::Reader input(raw);return OBJTEntry(input,2);
}
static void classicContract() {
 VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"classic fixture",true);
 auto bhav=new BHAV();bhav->ChunkProcessed=true;bhav->ChunkID=4096;bhav->Args=1;bhav->Locals=2;
 // Instruction0 is a previously completed side effect; the saved PC is1.
 for(int index=0;index<3;++index){
  BHAVInstruction op;op.Opcode=2;op.TruePointer=index==2?254:uint8_t(index+1);op.FalsePointer=255;
  FSO::SimAntics::Primitives::VMExpressionOperand args;
  args.LhsOwner=FSO::SimAntics::Engine::Scopes::VMVariableScope::MyObjectAttributes;args.LhsData=index==0?0:1;
  args.RhsOwner=FSO::SimAntics::Engine::Scopes::VMVariableScope::Literal;args.RhsData=1;
  args.Operator=FSO::SimAntics::Primitives::VMExpressionOperator::PlusEquals;args.Write(op.Operand.data());
  bhav->Instructions.push_back(op);
 }
 iff.AddChunk(bhav);
 auto idle=new BHAV();idle->ChunkProcessed=true;idle->ChunkID=4097;idle->Args=0;
 BHAVInstruction allow;allow.Opcode=17;idle->Instructions.push_back(allow);iff.AddChunk(idle);
 auto movement=new BHAV();movement->ChunkProcessed=true;movement->ChunkID=4098;movement->Locals=2;
 BHAVInstruction route;route.Opcode=27;movement->Instructions.push_back(route);iff.AddChunk(movement);
 OBJD definition;definition.GUID=0x12345678;definition.NumAttributes=2;
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 VMGameObject furniture(&object,nullptr);VMAvatar actor(&object);
 Entities::VMMultitileGroup furnitureGroup,actorGroup;
 furniture.MultitileGroup=&furnitureGroup;actor.MultitileGroup=&actorGroup;
 furnitureGroup.AddObject(&furniture);actorGroup.AddObject(&actor);
 furniture.ObjectID=10;actor.ObjectID=20;vm.Entities={&furniture,&actor};vm.ObjectsById[10]=&furniture;vm.ObjectsById[20]=&actor;
 context.SetToNextCache->NewObject(&furniture);context.SetToNextCache->NewObject(&actor);
 furniture.Thread=std::make_unique<VMThread>(&context,&furniture,16);actor.Thread=std::make_unique<VMThread>(&context,&actor,16);
 furniture.SetAttribute(0,9);actor.SetAttribute(0,7);
 actor.SetPersonData(Model::VMPersonDataVariable::Posture,1);
 // TS1 use count is derived from group membership and restored users, not
 // the raw object-data word. The fixture must have real group members.
 OBJT types;types.Entries.push_back(classicType(definition.GUID,23));
 OBJM snapshot;snapshot.Decoded=true;
 auto frame=[](int tree,int node){OBJMStackFrame f;f.TreeID=tree;f.NodeID=node;f.StackObjectID=32000;f.CodeOwnerObjType=23;f.Parameters={44};f.Locals={77,88};return f;};
 for(int id:{10,20}){
  auto& mapped=snapshot.ObjectData[id];mapped.GUID=definition.GUID;mapped.Instance=std::make_shared<OBJMInstance>();
  auto& inst=*mapped.Instance;inst.OBJT=&types.Entries[0];inst.TempRegisters.assign(8,0);inst.TempRegisters[3]=66;
  inst.Stack.push_back(frame(4096,1));
 }
 auto& personInst=*snapshot.ObjectData[20].Instance;personInst.HasPersonData=true;
 personInst.Stack.insert(personInst.Stack.begin(),frame(4097,0));personInst.Stack[0].Locals.clear();personInst.Stack[0].PrimitiveState=1;
 auto& person=personInst.PersonData;
 person.ActiveInteraction.TTAIndex=0;person.ActiveInteraction.UID=65535;person.ActiveInteraction.TargetID=10;person.ActiveInteraction.ActionTreeID=4096;
 person.ActiveInteraction.Flags=int(OBJMInteractionFlags::UserInitiated);person.ActiveInteraction.Priority=50;
 person.InteractionQueue.push_back(person.ActiveInteraction);person.InteractionQueue.back().UID=0;
 OBJMObjectUse use;use.TargetID=10;use.StackLength=2;person.ObjectUses.push_back(use);
 OBJMMotiveDelta delta;delta.Motive=int(Model::VMMotive::Fun);delta.TickDelta=1/1800.f;delta.StopAt=80;person.MotiveDeltas.push_back(delta);
 auto restore=[&]{st::Writer output;Archive archive(output);InteractionCheckpointCodec codec(vm,archive,7);codec.classic(snapshot,types);};
 auto oldFurniture=furniture.Thread.get(),oldActor=actor.Thread.get();
 auto priorClock=context.Clock->Ticks;auto priorRng=context.RandomSeed;
 restore();
 check(furniture.Thread.get()!=oldFurniture&&actor.Thread.get()!=oldActor,"classic candidate replaces threads only after preparation");
 check(furniture.Thread->Stack[0]->InstructionPointer==1&&furniture.Thread->Stack[0]->Locals==std::vector<int16_t>({77,88})&&furniture.Thread->Stack[0]->Args==std::vector<int16_t>({44,0,0,0})&&furniture.Thread->TempRegisters[3]==66,"classic PC locals arguments and temps survive by TypeID rather than table index");
 check(furniture.Thread->Stack[0]->StackObject==nullptr&&furniture.Thread->Stack[0]->StackObjectID()==32000,"classic scalar stack object survives without inventing a lot instance");
 check(actor.Thread->ActiveQueueBlock==0&&actor.Thread->Queue.size()==2&&actor.Thread->Queue[0]->UID==65535&&actor.Thread->Queue[1]->UID==0&&actor.Thread->ActionUID==1,"classic active and pending queues retain IDs with safe wraparound allocation");
 check(actor.Thread->Stack[1]->SpecialResult==VMSpecialResult::Interaction&&actor.Thread->Stack[1]->ActionTree&&actor.Thread->Stack[1]->Callee==&furniture&&actor.Thread->Queue[0]->UserQueued,"classic object use rebuilds actual interaction boundary and callee");
 check(actor.Thread->Stack[0]->PrimitiveState==1,"classic original frame primitive state survives independently of ActionTree");
 printf("classic restored posture=%d users=%d motive_rate=%d\n",actor.GetPersonData(Model::VMPersonDataVariable::Posture),furniture.GetValue(Model::VMStackObjectVariable::UseCount),actor.GetMotiveChange(int(Model::VMMotive::Fun)).PerHourChange);
 check(actor.GetPersonData(Model::VMPersonDataVariable::Posture)==1,"classic posture survives without reset cleanup");
 check(furniture.GetValue(Model::VMStackObjectVariable::UseCount)==1,"classic object use is retained by restored action");
 check(actor.GetMotiveChange(int(Model::VMMotive::Fun)).PerHourChange==1,"classic motive delta survives without reset cleanup");
 check(context.Clock->Ticks==priorClock&&context.RandomSeed==priorRng&&furniture.GetAttribute(0)==9&&actor.GetAttribute(0)==7,"classic conversion runs no simulation or repeated side effect");
 // Both PCs resolve, but an expression cannot own a child frame.
 auto& furnitureInst=*snapshot.ObjectData[10].Instance;
 furnitureInst.Stack.push_back(frame(4096,2));
 restore();
 check(furniture.Thread->Stack.empty()&&actor.Thread->Stack.size()==2&&actor.Thread->ActiveQueueBlock==0,
       "incompatible classic parent-child resets only its instance and preserves valid active avatar");
 check(actor.GetPersonData(Model::VMPersonDataVariable::Posture)==1&&furniture.GetAttribute(0)==9,
       "selective compatibility reset preserves valid posture and durable effects");
 furnitureInst.Stack.pop_back();
 for(uint16_t opcode:{uint16_t(17),uint16_t(20),uint16_t(22),uint16_t(27),uint16_t(28),uint16_t(30),uint16_t(45),uint16_t(4096)}){
  furnitureInst.Stack={frame(4098,0),frame(4096,1)};
  vm.Assemble(movement)->Instructions[0].Opcode=opcode;restore();
  check(furniture.Thread->Stack.size()==2,"exact Alex tree-running primitive/subroutine parent retains continuation");
 }
 vm.Assemble(movement)->Instructions[0].Opcode=27;
 furnitureInst.Stack={frame(4096,1)};
 vm.Assemble(idle)->Instructions[0].Opcode=2;restore();
 check(actor.Thread->Stack.empty()&&actor.Thread->Queue.empty()&&actor.GetMotiveChange(int(Model::VMMotive::Fun)).PerHourChange==1,
       "incompatible avatar reset still imports saved motive changes before exact reference reset");
 vm.Assemble(idle)->Instructions[0].Opcode=17;restore();
 furnitureInst.Stack[0].NodeID=300;restore();
 check(furniture.Thread->Stack.empty()&&actor.Thread->Stack.size()==2,"out-of-range historic PC resets only incompatible instance");
 furnitureInst.Stack[0].NodeID=1;furnitureInst.Stack[0].TreeID=4999;restore();
 check(furniture.Thread->Stack.empty()&&actor.Thread->Stack.size()==2,"missing historic routine resets only incompatible instance");
 furnitureInst.Stack[0].TreeID=4096;restore();
 furniture.Thread->NextInstruction();
 check(furniture.GetAttribute(0)==9&&furniture.GetAttribute(1)==1&&furniture.Thread->Stack[0]->InstructionPointer==2,"real thread consumer continues at saved PC without replaying prior effect");
 oldFurniture=furniture.Thread.get();oldActor=actor.Thread.get();personInst.Stack.back().CodeOwnerObjType=99;
 rejects(restore,"classic malformed owner rejects before thread publication");
 check(furniture.Thread.get()==oldFurniture&&actor.Thread.get()==oldActor&&furniture.GetAttribute(1)==1,"classic failed preparation leaves earlier thread and effects intact");
 personInst.Stack.back().CodeOwnerObjType=23;
 person.ObjectUses[0].StackLength=99;rejects(restore,"classic invalid object use is explicit and never reset silently");person.ObjectUses[0].StackLength=2;
 personInst.Stack[0].TreeID=4098; // portal subroutine under a route primitive
 personInst.Stack[0].NodeID=0;
 personInst.Stack[0].Locals={0,0};person.ActiveInteraction.TTAIndex=-1;person.InteractionQueue.clear();
 restore();
 check(actor.Thread->Stack[1]->SpecialResult==VMSpecialResult::Retry&&!actor.Thread->Stack[1]->ActionTree&&actor.Thread->Stack[1]->Callee==&furniture,"classic portal returns retry current movement primitive without replaying enclosing action");
 actor.Thread->Pop(VMPrimitiveExitCode::RETURN_TRUE);
 check(actor.Thread->Stack.size()==1&&actor.Thread->Stack[0]->InstructionPointer==0&&actor.Thread->TempRegisters[3]==66,"real Retry consumer returns to saved movement primitive and preserves registers");
 snapshot.ObjectData[10].Instance->Stack.clear();personInst.Stack.clear();person.ObjectUses.clear();restore();
 check(furniture.Thread->Stack.empty()&&actor.Thread->Stack.empty()&&actor.Thread->ActiveQueueBlock==-1,"genuinely empty classic stacks remain valid without authored Reset or fabricated actions");
 FSO::Vitaboy::Animation anim;anim.Name="fixture";anim.Motions.resize(1);anim.Motions[0].FrameCount=30;
 FSO::Vitaboy::TimePropertyList properties;
 for(int time:{0,250,750}){FSO::Vitaboy::TimePropertyListItem item;item.ID=time;properties.Items.push_back(item);}anim.Motions[0].TimeProperties.push_back(properties);
 auto state=ClassicAnimation(&anim,"fixture;1;1000;500;1000;0;1;1",2);
 check(state->CurrentFrame==15&&state->EventsRun==2&&state->EventQueue.empty()&&state->TimePropertyLists.size()==1&&state->TimePropertyLists[0].ID==750,"classic animation resumes frame and consumed-event count without replaying elapsed properties");
 FSO::Vitaboy::TimePropertyListItem quantized;quantized.ID=500;
 FSO::Vitaboy::PropertyListItem xevt;xevt.KeyPairs.push_back({"xevt","0"});quantized.Properties.Items.push_back(xevt);
 anim.Motions[0].TimeProperties[0].Items={quantized};
 auto consumed=ClassicAnimation(&anim,"fixture;1;1000;500;1000;0;1;1",1);
 check(consumed->TimePropertyLists.empty()&&consumed->EventsRun==1,"classic consumed xevt beyond rounded frame boundary cannot replay its side effect");
 anim.Motions[0].TimeProperties[0]=properties;
 auto backwards=ClassicAnimation(&anim,"fixture;1;-1000;500;1000;0;1;1",1);
 check(backwards->PlayingBackwards&&backwards->TimePropertyLists.size()==2&&backwards->TimePropertyLists.back().ID==250,"classic backwards animation retains only not-yet-crossed properties");
 rejects([&]{ClassicAnimation(&anim,"fixture;1;1000;bad;1000;0;1;1",0);},"malformed classic animation numeric state is rejected");
 // Exercise the family activation consumer too: a resident's saved OOW pose
 // is authored state, while only newly created arrivals need placement.
 auto content=FSO::Content::Content::Get();auto previous=std::move(content->Neighborhood);
 content->Neighborhood=std::make_unique<FSO::Content::TS1::TS1NeighborhoodProvider>(content);
 auto& provider=*content->Neighborhood;provider.MainResource=std::make_unique<IffFile>();
 auto neighbors=new NBRS();neighbors->ChunkProcessed=true;provider.MainResource->AddChunk(neighbors);provider.Neighbors=neighbors;
 auto family=new FAMI();family->ChunkProcessed=true;family->ChunkID=1;family->HouseNumber=9;
 family->FamilyGUIDs={definition.GUID};family->Budget=1234;provider.MainResource->AddChunk(family);
 vm.GlobalState.assign(38,0);actor.Position(FSO::LotView::Model::LotTilePos::OUT_OF_WORLD);
 auto retained=actor.Thread.get();VMTS1FamilyLoader loader(&vm);auto household=loader.LoadFamily(9,family,true);
 check(household.size()==1&&household[0]==&actor&&actor.Position()==FSO::LotView::Model::LotTilePos::OUT_OF_WORLD&&actor.Thread.get()==retained,"real family activation preserves existing resident OOW position and thread");
 check(family->Budget==1234&&vm.GetGlobalValue(VMTS1Neighbourhood::GLOBAL_BUDGET_MOD)==1234,"family activation keeps one budget without replaying action charges");
 // A decoded off-world visitor is also a saved participant. Family selection
 // must leave its identity and dormant continuation available to ODHI restore.
 OBJD visitorDefinition;visitorDefinition.GUID=0x12345679;
 FSO::Content::GameObject visitorObject;visitorObject.OBJ=&visitorDefinition;
 visitorObject.GUID=visitorDefinition.GUID;visitorObject.Resource=&resource;
 VMAvatar visitor(&visitorObject);Entities::VMMultitileGroup visitorGroup;
 visitor.MultitileGroup=&visitorGroup;visitorGroup.AddObject(&visitor);
 visitor.ObjectID=30;visitor.Position(FSO::LotView::Model::LotTilePos::OUT_OF_WORLD);
 visitor.SetPersonData(Model::VMPersonDataVariable::PersonType,2);
 visitor.SetPersonData((Model::VMPersonDataVariable)VMTS1Neighbourhood::PD_NEIGHBOR_ID,17);
 visitor.TS1SchedulerState=0x80000;
 visitor.Thread=std::make_unique<VMThread>(&context,&visitor,16);
 auto dormant=std::make_shared<VMStackFrame>();dormant->Caller=&visitor;dormant->Callee=&visitor;
 dormant->CodeOwner=&visitorObject;dormant->Routine=vm.Assemble(bhav);dormant->InstructionPointer=0;
 dormant->Args={19};dormant->Locals={23,29};visitor.Thread->Stack.push_back(dormant);
 vm.Entities.push_back(&visitor);vm.ObjectsById[30]=&visitor;context.SetToNextCache->NewObject(&visitor);
 auto visitorThread=visitor.Thread.get();const auto activationTick=context.Clock->Ticks;const auto activationSeed=context.RandomSeed;
 loader.LoadFamily(9,family,true);
 check(vm.GetObjectById(30)==&visitor&&!visitor.Dead&&visitor.Position()==FSO::LotView::Model::LotTilePos::OUT_OF_WORLD&&
       visitor.Thread.get()==visitorThread&&visitor.Thread->Stack.size()==1&&visitor.Thread->Stack.front()==dormant&&
       dormant->InstructionPointer==0&&dormant->Args==std::vector<int16_t>{19}&&dormant->Locals==std::vector<int16_t>({23,29})&&
       visitor.TS1SchedulerState==0x80000&&context.Clock->Ticks==activationTick&&context.RandomSeed==activationSeed,
       "decoded non-family offworld visitor keeps identity, dormant frame and manager state through family activation");
 // This isolated codec VM has no architecture for household arrival placement.
 // Keep the resident placed while exercising only the undecoded visitor cleanup.
 actor.Position(FSO::LotView::Model::LotTilePos(16,16,1));
 loader.LoadFamily(9,family,false);
 check(visitor.Dead&&vm.GetObjectById(30)==nullptr&&visitorGroup.Objects.empty(),
       "undecoded offworld visitor still follows legacy cleanup fallback");
 content->Neighborhood=std::move(previous);
 vm.Entities.clear();vm.ObjectsById.clear();
}

static void dialogContract() {
 VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);
 // The real codec reader resolves owners through the content provider. Keep
 // the synthetic definition in an isolated provider, never an ambient cache.
 auto content=FSO::Content::Content::Get();
 struct RestoreProvider {
  FSO::Content::Content* content;
  std::unique_ptr<FSO::Content::WorldObjectProvider> original;
  ~RestoreProvider(){content->WorldObjects=std::move(original);}
 } provider{content,std::move(content->WorldObjects)};
 content->WorldObjects=std::make_unique<FSO::Content::WorldObjectProvider>(content);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"dialog fixture",true);
 auto dialog=new BHAV();dialog->ChunkProcessed=true;dialog->ChunkID=4096;
 BHAVInstruction prompt;prompt.Opcode=36;prompt.TruePointer=1;prompt.FalsePointer=2;
 Primitives::VMDialogOperand operand;operand.Type=Primitives::VMDialogType::YesNo;operand.Write(prompt.Operand.data());
 dialog->Instructions.push_back(prompt);
 for(int i=0;i<2;++i){BHAVInstruction side;side.Opcode=2;side.TruePointer=254;side.FalsePointer=255;
  Primitives::VMExpressionOperand a;a.LhsOwner=Scopes::VMVariableScope::MyObjectAttributes;a.LhsData=i;
  a.RhsOwner=Scopes::VMVariableScope::Literal;a.RhsData=1;a.Operator=Primitives::VMExpressionOperator::PlusEquals;a.Write(side.Operand.data());dialog->Instructions.push_back(side);}
 iff.AddChunk(dialog);
 OBJD definition;definition.GUID=0x12345678;definition.NumAttributes=2;
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 content->WorldObjects->Cache[definition.GUID]=std::make_unique<FSO::Content::GameObject>(object);
 VMGameObject actor(&object,nullptr);actor.ObjectID=10;
 Entities::VMMultitileGroup group;group.AddObject(&actor);actor.MultitileGroup=&group;
 vm.Entities={&actor};vm.ObjectsById[10]=&actor;
 auto original=std::make_unique<VMThread>(&context,&actor,16);
 auto frame=std::make_shared<VMStackFrame>();frame->Thread=original.get();frame->Caller=frame->Callee=frame->StackObject=&actor;
 frame->CodeOwner=&object;frame->Routine=vm.Assemble(dialog);frame->Args.resize(4);original->Stack.push_back(frame);
 actor.Thread=std::move(original);
 auto write=[&](VMThread& thread,uint32_t version){st::Writer bytes(st::Endian::Little);Archive archive(bytes);InteractionCheckpointCodec codec(vm,archive,version);codec.thread(thread);return bytes.ToArray();};
 auto read=[&](const std::vector<uint8_t>& bytes,uint32_t version){auto candidate=std::make_unique<VMThread>(&context,&actor,16);st::Reader in(bytes);Archive archive(in);InteractionCheckpointCodec codec(vm,archive,version);codec.thread(*candidate);check(!in.has_more(),"thread payload consumed exactly");return candidate;};
 const auto legacy=write(*actor.Thread,7);auto old=read(legacy,7);
 check(!old->BlockingState&&old->Stack.back()->InstructionPointer==0&&write(*old,7)==legacy,"version7 thread byte layout remains readable and exact");
 auto pending=std::make_shared<Primitives::VMDialogResult>();pending->Type=operand.Type;pending->WaitTime=123;pending->Version=42;pending->Timeout=456;pending->ResponseText="accepted text";pending->HasDisplayed=true;
 actor.Thread->BlockingState=pending;
 auto bytes=write(*actor.Thread,8);
 check(pending->HasDisplayed&&!pending->Responded&&pending->WaitTime==123,"capture never mutates dialog presentation response or wait");
 auto restored=read(bytes,8);auto state=std::dynamic_pointer_cast<Primitives::VMDialogResult>(restored->BlockingState);
 check(state&&!state->Responded&&state->WaitTime==123&&state->Version==42&&state->Timeout==456&&state->ResponseText=="accepted text"&&!state->HasDisplayed,
       "version8 restores complete unanswered dialog state with fresh presentation");
 actor.Thread=std::move(restored);int shown=0;vm.OnDialog=[&](Model::VMDialogInfo& info){++shown;check(info.Block&&info.Operand.Type==operand.Type,"restored UI uses actual blocking dialog producer");};
 auto clock=context.Clock->Ticks;auto rng=context.RandomSeed;VMTS1InteractionCheckpoint::PresentDialogs(vm);VMTS1InteractionCheckpoint::PresentDialogs(vm);
 check(shown==1&&state->HasDisplayed&&actor.Thread->Stack.back()->InstructionPointer==0&&context.Clock->Ticks==clock&&context.RandomSeed==rng,
       "unanswered UI appears once before first tick without PC clock or RNG advance");
 check(write(*actor.Thread,8)==bytes,"fresh presentation does not change persisted dialog bytes");
 // Accepted response survives save/load, is never re-prompted and executes one
 // authored branch only after the ordinary VM instruction consumer resumes.
 for(uint8_t code:{uint8_t(0),uint8_t(1)}){
  state->Responded=true;state->ResponseCode=code;
  auto answered=write(*actor.Thread,8);auto next=read(answered,8);auto response=std::dynamic_pointer_cast<Primitives::VMDialogResult>(next->BlockingState);
  actor.Thread=std::move(next);auto before=shown;VMTS1InteractionCheckpoint::PresentDialogs(vm);
  check(shown==before&&response->Responded&&response->ResponseCode==code,"accepted response fresh restore never re-prompts or consumes during load");
  actor.Thread->NextInstruction();
  check(!actor.Thread->BlockingState&&actor.Thread->Stack.back()->InstructionPointer==(code==0?1:2),"real dialog consumer consumes accepted response and takes correct authored branch");
  auto effects=actor.GetAttribute(code);actor.Thread->NextInstruction();
  check(actor.GetAttribute(code)==effects+1,"continued dialog branch side effect runs exactly once");
  actor.Thread=read(bytes,8);state=std::dynamic_pointer_cast<Primitives::VMDialogResult>(actor.Thread->BlockingState);
 }
 // Failures prepare only a detached candidate: live actor/state remain intact.
 auto retained=actor.Thread.get();auto prior=actor.Thread->BlockingState;
 auto truncated=bytes;truncated.pop_back();rejects([&]{read(truncated,8);},"truncated dialog payload rejects before publication");
 check(actor.Thread.get()==retained&&actor.Thread->BlockingState==prior,"malformed read leaves live thread and response untouched");
 state->Type=Primitives::VMDialogType::TextEntry;rejects([&]{write(*actor.Thread,8);},"mismatched dialog type rejected");state->Type=operand.Type;
 auto& instruction=actor.Thread->Stack.back()->Routine->Instructions[0];
 instruction.Opcode=39;rejects([&]{write(*actor.Thread,8);},"dialog semiglobal missing source rejected before publication");instruction.Opcode=36;
 state->ResponseText=std::string(1,'\0');rejects([&]{write(*actor.Thread,8);},"malformed response text rejected");state->ResponseText="";
 state->WaitTime=-1;rejects([&]{write(*actor.Thread,8);},"negative dialog wait rejected");state->WaitTime=0;
 auto eod=std::make_shared<VMEODPluginThreadState>();actor.Thread->BlockingState=eod;
 rejects([&]{write(*actor.Thread,8);},"unsupported TSO async state remains rejected");
 actor.Thread->BlockingState.reset();actor.Thread->EODConnection=eod;
 rejects([&]{write(*actor.Thread,8);},"unsupported EOD connection remains rejected");
 // The iconless standard cancellation action uses an existing saved flag,
 // not an idle scheduling mode or a new ODHI field. Both accepted layouts
 // must preserve the explicit null icon rather than invent a target portrait.
 actor.Thread->EODConnection.reset();
 auto hidden=std::make_shared<VMQueuedAction>();
 hidden->Callee=hidden->StackObject=&actor;hidden->CodeOwner=&object;
 hidden->ActionRoutine=vm.Assemble(dialog);hidden->Args.assign(4,0);
 hidden->InteractionNumber=33;hidden->Priority=50;hidden->UID=123;
 hidden->Flags=TTABFlags::TS1HideQueueIcon;hidden->Mode=VMQueueMode::Normal;
 actor.Thread->Queue={hidden};actor.Thread->ActiveQueueBlock=-1;
 for(uint32_t version:{uint32_t(7),uint32_t(8)}){
  const auto encoded=write(*actor.Thread,version);auto loaded=read(encoded,version);
  const auto action=loaded->Queue.at(0);
  check(action->HasHiddenIcon()&&!action->IconOwner()&&!PersistedActionIcon(vm,*action)&&
        action->Mode==VMQueueMode::Normal&&action->InteractionNumber==33&&action->Priority==50,
        "iconless cleanup preserves ordinary scheduling and null icon through existing saved layout");
  check(write(*loaded,version)==encoded,"iconless queue state writes back without layout drift");
 }
 hidden->Flags=TTABFlags(0);
 check(hidden->IconOwner()==&actor,"ordinary missing custom icon retains target fallback");
 actor.Thread->Queue.clear();
 OBJT stateTypes;stateTypes.Entries.push_back(classicType(definition.GUID,23));
 OBJM companion;companion.Decoded=true;auto& source=companion.ObjectData[10];source.Instance=std::make_shared<OBJMInstance>();source.Instance->OBJT=&stateTypes.Entries[0];
 OBJMStackFrame sourceFrame;sourceFrame.TreeID=4096;sourceFrame.NodeID=0;sourceFrame.StackObjectID=10;sourceFrame.CodeOwnerObjType=23;
 source.Instance->Stack={sourceFrame};
 const auto unchangedBody=write(*actor.Thread,8);
 for(int state:{0,1}){
  actor.Thread->Stack[0]->PrimitiveState=state;
  check(write(*actor.Thread,8)==unchangedBody,"primitive state does not change ODHI8 byte layout");
  std::map<int16_t,Record> candidates;auto& candidate=candidates[10];candidate.entity=&actor;candidate.preparedThread=read(unchangedBody,8);
  source.Instance->Stack[0].PrimitiveState=state;
  st::Writer unused;Archive archive(unused);InteractionCheckpointCodec codec(vm,archive,8);
  codec.preparePrimitiveStates(candidates,companion,&stateTypes);
  check(candidate.preparedThread->Stack[0]->PrimitiveState==state,"matched classic companion restores exact zero/one primitive state");
 }
 source.Instance->Stack[0].TreeID=4999;
 std::map<int16_t,Record> mismatched;auto& oldProjection=mismatched[10];oldProjection.entity=&actor;oldProjection.preparedThread=read(unchangedBody,8);
 st::Writer ignored;Archive ignoredArchive(ignored);InteractionCheckpointCodec overlay(vm,ignoredArchive,8);
 overlay.preparePrimitiveStates(mismatched,companion,&stateTypes);
 check(oldProjection.preparedThread->Stack[0]->PrimitiveState==0&&oldProjection.preparedThread->Stack[0]->Routine->ID==4096,
       "mismatched legacy classic projection never overlays a valid native execution graph");
 vm.Entities.clear();vm.ObjectsById.clear();
}

int main(){try{
 VMContext context(nullptr);VM vm(&context,nullptr);
 IffFile iff;FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"route-fixture",true);
 OBJD definition;definition.GUID=0x12345678;definition.ChunkLabel="route fixture";
 FSO::Content::GameObject object;object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
 VMGameObject dead(&object,nullptr),live(&object,nullptr),replacement(&object,nullptr);
 dead.ObjectID=7;dead.Dead=true;live.ObjectID=8;replacement.ObjectID=7;
 vm.ObjectsById[8]=&live;
 auto write=[&](VMRoutingFrame& r,VMRoutingFrame* parent=nullptr){st::Writer bytes(st::Endian::Little);Archive archive(bytes);InteractionCheckpointCodec codec(vm,archive,6);codec.route(r,parent);return bytes.ToArray();};
 auto read=[&](const std::vector<uint8_t>& bytes,VMRoutingFrame& r,VMRoutingFrame* parent=nullptr){st::Reader input(bytes);Archive archive(input);InteractionCheckpointCodec codec(vm,archive,6);codec.route(r,parent);};
 VMRoutingFrame route;route.Target=&dead;route.State=VMRoutingFrameState::WALKING;route.PushTick=22;route.WaitTime=91;route.CallFailureTrees=true;route.Velocity=3;
 route.WalkTo=std::make_shared<std::list<st::Point>>();route.WalkTo->push_back({12,13});route.CurRoute=std::make_shared<VMFindLocationResult>();route.CurRoute->Chair=&dead;
 route.Rooms.push_back({99,5});route.CurrentPortal={99,5};route.IgnoredRooms.insert({99,5});
 const auto bytes=write(route);VMRoutingFrame restored;read(bytes,restored);
 check(restored.State==VMRoutingFrameState::FAILED&&restored.HeldResult==int(VMPrimitiveExitCode::RETURN_FALSE)&&restored.PushTick==22,"dead target becomes an existing-format failed return");
 check(!restored.Target&&!restored.Slot&&!restored.CurRoute&&!restored.WalkTo&&!restored.Choices&&restored.Rooms.empty()&&!restored.CurrentPortal.ObjectID&&restored.IgnoredRooms.empty()&&restored.AvatarsToConsider.empty()&&restored.WaitTime==0&&!restored.CallFailureTrees,"abandoned route carries no target/path/portal/goal refs or feedback");
 check(route.Target==&dead&&route.State==VMRoutingFrameState::WALKING&&route.WaitTime==91&&route.CurRoute->Chair==&dead&&route.Rooms.size()==1&&route.HeldResult==-1,"capture does not mutate live route or its lifecycle");
 vm.ObjectsById[7]=&replacement;check(write(route)==bytes,"dead target with reused ID never binds replacement");
 check(write(restored)==bytes,"canonical failed route is byte-stable on recapture");
 VMRoutingFrame parent;route.ParentRoute=&parent;auto nested=write(route,&parent);VMRoutingFrame restoredNested;read(nested,restoredNested,&parent);check(restoredNested.ParentRoute==&parent&&restoredNested.HeldResult==int(VMPrimitiveExitCode::RETURN_FALSE),"nested route preserves enclosing parent chain");route.ParentRoute=nullptr;
 VMRoutingFrame normal;normal.Target=&live;normal.State=VMRoutingFrameState::INITIAL;auto liveBytes=write(normal);VMRoutingFrame liveRestored;read(liveBytes,liveRestored);check(liveRestored.Target==&live&&write(liveRestored)==liveBytes,"live route retains exact identity and bytes");
 normal.Target=&dead;dead.Dead=false;rejects([&]{write(normal);},"nondead replaced target remains rejected");dead.Dead=true;
 normal.Target=&live;live.GhostImage=true;rejects([&]{write(normal);},"ghost route target remains rejected");live.GhostImage=false;
 dead.GhostImage=true;normal.Target=&dead;rejects([&]{write(normal);},"dead ghost target is not normalized");dead.GhostImage=false;
 route.Velocity=9;rejects([&]{write(route);},"dead route cannot mask invalid routing scalar");route.Velocity=3;
 route.ParentRoute=&parent;rejects([&]{write(route);},"dead route cannot mask invalid parent chain");route.ParentRoute=nullptr;
 auto essential=[&](VMEntity* e,const char* use){st::Writer output(st::Endian::Little);Archive archive(output);InteractionCheckpointCodec codec(vm,archive,6);codec.reference(e,use);};
 for(auto use:{"frame caller","frame callee","action target","callback target","route chair","route blocker"})rejects([&]{essential(&dead,use);},(std::string("essential reference rejects deleted: ")+use).c_str());
 // A route's collision cache may outlive a departing NPC. Expired cache
 // entries are distinct from the essential targets validated above.
 Entities::VMMultitileGroup avatarGroup;avatarGroup.Name="blocker fixture";
 VMAvatar departed(&object),standing(&object),other(&object);
 departed.MultitileGroup=standing.MultitileGroup=other.MultitileGroup=&avatarGroup;
 departed.ObjectID=31;departed.Dead=true;standing.ObjectID=32;other.ObjectID=31;
 vm.ObjectsById[32]=&standing;vm.ObjectsById[31]=&other;
 VMRoutingFrame cached;cached.Target=&live;cached.State=VMRoutingFrameState::INITIAL;
 cached.AvatarsToConsider.insert(&departed);cached.AvatarsToConsider.insert(&standing);
 auto cachedBytes=write(cached);VMRoutingFrame cachedRestored;read(cachedBytes,cachedRestored);
 check(cachedRestored.Target==&live&&cachedRestored.State==cached.State&&cachedRestored.AvatarsToConsider.size()==1&&cachedRestored.AvatarsToConsider.count(&standing),"departed blocker omitted while live route and blocker survive");
 check(cached.AvatarsToConsider.size()==2&&cached.AvatarsToConsider.count(&departed),"capture leaves running collision cache untouched");
 check(!cachedRestored.AvatarsToConsider.count(&other)&&write(cachedRestored)==cachedBytes,"departed blocker never rebinds reused object ID and recapture is stable");
 departed.GhostImage=true;rejects([&]{write(cached);},"dead ghost blocker remains rejected");departed.GhostImage=false;
 departed.Dead=false;rejects([&]{write(cached);},"nondead replaced blocker remains rejected");departed.Dead=true;
 auto departedId=departed.ObjectID;departed.ObjectID=0;rejects([&]{write(cached);},"invalid deleted blocker identity remains rejected");departed.ObjectID=departedId;
 cached.AvatarsToConsider.insert(nullptr);rejects([&]{write(cached);},"null blocker rejected before sorting collision cache");cached.AvatarsToConsider.erase(nullptr);
 vm.ObjectsById.erase(31);vm.ObjectsById.erase(32);
 vm.ObjectsById.erase(8);rejects([&]{VMRoutingFrame r;read(liveBytes,r);},"reader rejects missing route identity");vm.ObjectsById[8]=&live;
 auto originalGuid=definition.GUID;definition.GUID++;rejects([&]{VMRoutingFrame r;read(liveBytes,r);},"reader rejects changed route GUID");definition.GUID=originalGuid;
 live.Dead=true;rejects([&]{VMRoutingFrame r;read(liveBytes,r);},"reader rejects dead route identity");live.Dead=false;
 live.GhostImage=true;rejects([&]{VMRoutingFrame r;read(liveBytes,r);},"reader rejects ghost route identity");live.GhostImage=false;
 // VMThread's held-result consumer returns the route false edge without calling
 // Tick, touching an avatar, changing stats, or executing route-failure feedback.
 VMThread thread(&context,&live,16);auto frame=std::make_shared<VMRoutingFrame>();read(bytes,*frame);frame->Thread=&thread;thread.Stack.push_back(frame);thread.NextInstruction();
 check(thread.Stack.empty()&&thread.LastStackExitCode==VMPrimitiveExitCode::RETURN_FALSE,"real VMThread consumer unwinds canonical failed route");
 VMRoutine mainRoutine;mainRoutine.Instructions.resize(3);mainRoutine.Instructions[0].TruePointer=1;mainRoutine.Instructions[0].FalsePointer=2;
 auto mainFrame=std::make_shared<VMStackFrame>();mainFrame->Thread=&thread;mainFrame->Routine=&mainRoutine;mainFrame->Caller=mainFrame->Callee=&live;mainFrame->Args={42};mainFrame->Locals={123};
 auto pending=std::make_shared<VMQueuedAction>();pending->Callee=&live;pending->UID=44;thread.Queue.push_back(pending);thread.TempRegisters[2]=73;thread.Stack.push_back(mainFrame);read(bytes,*frame);thread.Stack.push_back(frame);thread.NextInstruction();
 check(thread.Stack.size()==1&&thread.Stack[0]==mainFrame&&mainFrame->InstructionPointer==2&&mainFrame->Args==std::vector<int16_t>{42}&&mainFrame->Locals==std::vector<int16_t>{123},"real VMThread preserves Main and takes authored false edge");
 check(thread.Queue.size()==1&&thread.Queue[0]==pending&&thread.TempRegisters[2]==73&&thread.ActiveQueueBlock==-1,"failed route leaves pending work and thread registers intact");
 classicContract();
 dialogContract();
 printf("route checkpoint contract: %d cases passed\n",cases);return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
