// SPDX-License-Identifier: MPL-2.0
// Real OP47 consumer with authored banks/independent source slots and callbacks.
#include "simantics/primitives/VMReach.h"
#include "simantics/primitives/VMExpression.h"
#include "simantics/engine/VMStackFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/IffFile.h"
#include "files/formats/iff/chunks/STR.h"
#include "content/ContentManager.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using Pos=FSO::LotView::Model::LotTilePos;
namespace {
int checks=0;
void Need(bool b,const char* message){if(!b)throw std::runtime_error(message);++checks;}
struct Fixture {
 VMContext C{nullptr};VM V{&C,nullptr};IffFile Iff,GlobalIff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"reach-contract",true};
 FSO::Content::GameGlobalResource GlobalResource{&GlobalIff,nullptr};FSO::Content::GameGlobal Global;
 OBJD Def,PersonDef;FSO::Content::GameObject Object,Person;
 Entities::VMMultitileGroup AG,OG,SG,PG;
 std::unique_ptr<VMAvatar> Actor;std::unique_ptr<VMGameObject> Item,Support,Parent;
 std::shared_ptr<VMStackFrame> Frame;VMReach Reach;
 Fixture(){
  V.TS1=true;V.GlobalState.resize(38,0);C.Clock->Ticks=900;Global.Resource=&GlobalResource;C.Globals=&Global;
  C.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&C));C.Architecture()->Rooms[0].Map.assign(64,0);C.RoomInfo.resize(1);
  C.Architecture()->TS1TileFlags.assign(2,std::vector<uint8_t>(64,255));C.Architecture()->TS1WaterTiles.assign(64,-1);C.RebuildEnvironmentGeometry();C.Architecture()->TS1PoolTiles.assign(64,0);C.Architecture()->TS1WaterTiles.assign(64,-1);C.Architecture()->TS1TileFlags.assign(2,std::vector<uint8_t>(64,255));C.Architecture()->TS1FloorPresent.assign(2,std::vector<uint8_t>(64,1));
  Def.GUID=0xf0270001;Def.NumAttributes=3;PersonDef.GUID=0xf0270002;PersonDef.NumAttributes=3;PersonDef.ObjectType=OBJDType::Person;
  Object.OBJ=&Def;Object.GUID=Def.GUID;Object.Resource=&Resource;Person.OBJ=&PersonDef;Person.GUID=PersonDef.GUID;Person.Resource=&Resource;
  Actor=std::make_unique<VMAvatar>(&Person);Item=std::make_unique<VMGameObject>(&Object,nullptr);Support=std::make_unique<VMGameObject>(&Object,nullptr);Parent=std::make_unique<VMGameObject>(&Object,nullptr);
  VMEntity* es[]={Actor.get(),Item.get(),Support.get(),Parent.get()};Entities::VMMultitileGroup* gs[]={&AG,&OG,&SG,&PG};
  for(int i=0;i<4;++i){es[i]->MultitileGroup=gs[i];gs[i]->AddObject(es[i]);if(i>0)es[i]->PublishTS1PhysicalSlots(std::vector<VMEntity*>(1,nullptr));V.AddEntity(es[i]);es[i]->Thread=std::make_unique<VMThread>(&C,es[i],16);es[i]->Position(Pos(24,24,1));es[i]->SetValue(VMStackObjectVariable::PlacementFlags,3);es[i]->SetValue(VMStackObjectVariable::Flags,0x2000);es[i]->SetValue(VMStackObjectVariable::AllowedHeightFlags,0x7fff);es[i]->SetValue(VMStackObjectVariable::SupportStrength,100);es[i]->SetValue(VMStackObjectVariable::Weight,1);C.RegisterObjectPos(es[i]);}
  Actor->SetPersonData(VMPersonDataVariable::PersonsAge,30);
  for(int id:{154,155}){auto* t=new STR();t->ChunkID=id;t->ChunkProcessed=true;t->LanguageSets[0].Strings.resize(11);for(int n=0;n<11;++n)t->LanguageSets[0].Strings[n].Value=std::string(id==154?"fixture-reach-a-":"fixture-reach-c-")+std::to_string(n);GlobalIff.AddChunk(t);for(int n=1;n<11;++n){auto*a=FSO::Content::Content::Get()->AvatarAnimations->Get(t->GetString(n)+".anim");a->Duration=1000;if(a->Motions.empty())a->Motions.resize(1);a->Motions[0].FrameCount=30;}}
  auto* bhav=new BHAV();bhav->ChunkID=4096;bhav->ChunkProcessed=true;bhav->Args=1;BHAVInstruction op;op.Opcode=47;op.TruePointer=254;op.FalsePointer=255;bhav->Instructions.push_back(op);Iff.AddChunk(bhav);
  Frame=std::make_shared<VMStackFrame>();Frame->Thread=Actor->Thread.get();Frame->Caller=Actor.get();Frame->Callee=Item.get();Frame->CodeOwner=&Object;Frame->Routine=V.Assemble(bhav);Frame->Args.assign(4,0);Frame->StackObject=Item.get();Actor->Thread->Stack={Frame};
  for(int i=0;i<5;++i)Actor->Thread->TempRegisters[i]=int16_t(70+i);
  for(auto* name:{"CT - Grab","CT - Put"}){
   auto* cb=new BHAV();cb->ChunkProcessed=true;cb->ChunkID=name[5]=='G'?4100:4101;cb->Args=5;cb->ChunkLabel=name;
   BHAVInstruction step;step.Opcode=2;step.TruePointer=1;step.FalsePointer=255;VMExpressionOperand e;e.LhsOwner=Scopes::VMVariableScope::MyObjectAttributes;e.LhsData=0;e.RhsOwner=Scopes::VMVariableScope::Literal;e.RhsData=name[5]=='G'?1:2;e.Operator=VMExpressionOperator::PlusEquals;e.Write(step.Operand.data());cb->Instructions.push_back(step);
   step.TruePointer=2;e.LhsOwner=Scopes::VMVariableScope::StackObjectAttributes;e.LhsData=1;e.RhsOwner=Scopes::VMVariableScope::Parameters;e.RhsData=4;e.Operator=VMExpressionOperator::Assign;e.Write(step.Operand.data());cb->Instructions.push_back(step);
   step=BHAVInstruction();step.Opcode=44;step.TruePointer=254;step.FalsePointer=255;cb->Instructions.push_back(step); // callback retires FG: detect stale-pointer consumers.
   Iff.AddChunk(cb);Resource.TreeByName.emplace(name,FSO::Content::VMTreeByNameTableEntry(cb));
  }
 }
 VMPrimitiveExitCode Run(int mode=0,int transfer=1){VMReachOperand o;o.Mode=uint16_t(mode);o.GrabOrDrop=uint16_t(transfer);return Reach.Execute(Frame.get(),&o);}
 void Target(VMEntity* e){Frame->StackObject=e;Frame->StackObjectRawID=e?e->ObjectID:0;}
 void Meta(VMEntity* e,float z,int height=3){auto& n=C.TS1HierarchyNodes.at(e->ObjectID);n.Metadata[1].Offset.Z=z;n.Metadata[1].Height=height;}
};
void PickupAndCallback(){
 Fixture f;auto& c=f.Actor->TS1AnimationController;c.ScriptActive=1;c.EventsRun=7;c.Events={42};auto queue=std::make_shared<VMQueuedAction>();queue->Callee=f.Support.get();f.Actor->Thread->Queue={queue};const auto seed=f.C.TS1SimulationRandomSeed;
 Need(f.Run()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Frame->PrimitiveState==1,"Reach starts source frame1");
 auto* clip=f.Actor->CurrentAnimationState();Need(clip&&clip->TS1Clock&&clip->TS1Timestamp==45000&&clip->Anim->Name=="fixture-reach-a-1","floor Reach uses authored bank and actual model clock");
 Need(c.ModelState==0&&c.CarrySuppressed==1&&c.ScriptActive==1&&c.EventsRun==7&&c.Events.empty(),"Reach owns suppression/FIFO without stealing AnimateSim latch/counter");
 c.Events={77,0,0,100};Need(f.Run()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Frame->PrimitiveState==2,"all FIFO zeros produce only one committed pickup");
 Need(f.C.TS1SourceSlot(f.Actor.get(),1)==f.Item.get()&&f.Actor->GetAttribute(0)==1&&f.Item->GetAttribute(1)==74,"moved-resource CT callback runs once with moved StackOID and declared fifth argument");
 Need(!f.Actor->CurrentAnimationState()&&f.Actor->Thread->Stack.size()==1&&f.Actor->Thread->Stack[0]==f.Frame,"synchronous callback may clear foreground without stale clip/stack access");
 Need(f.Run()==VMPrimitiveExitCode::GOTO_TRUE&&c.CarrySuppressed==0&&c.ModelState==3&&c.ScriptActive==1,"completed pickup uses source cleanup and preserves unrelated script latch");
 Need(f.Actor->Thread->Queue.size()==1&&f.Actor->Thread->Queue[0]==queue&&f.C.TS1SimulationRandomSeed==seed,"Reach preserves pending user queue and RNG");
}
void QueryFailure(){
 Fixture f;Need(f.Run()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK,"failure control starts");f.Item->SetValue(VMStackObjectVariable::AllowedHeightFlags,0);f.Actor->TS1AnimationController.Events={0};Need(f.Run()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Frame->PrimitiveState==3,"typed query rejection becomes frame3, not a committed transfer");
 Need(!f.Item->Container&&f.Actor->GetAttribute(0)==0&&f.C.TS1SourceSlot(f.Actor.get(),1)==nullptr,"rejected pickup retains source edges and never calls CT");f.Actor->CurrentAnimationState()->EndReached=true;
 Need(f.Run()==VMPrimitiveExitCode::GOTO_FALSE&&f.Actor->TS1AnimationController.CarrySuppressed==0,"failed Reach waits for clip completion before false cleanup");
}
void HeightAndMode(){
 Fixture f;f.Meta(f.Parent.get(),4);f.Meta(f.Support.get(),.25f);f.C.TS1CommitPhysicalPlacement(f.Support.get(),f.C.TS1PhysicalRequest(f.Support.get(),f.Parent.get(),0));f.Target(f.Support.get());
 Need(f.Run(1,0)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Actor->CurrentAnimationState()->Anim->Name=="fixture-reach-a-5","mode1 uses cumulative ancestor Z, not relative SLOT or height code");
 f.Actor->CurrentAnimationState()->EndReached=true;Need(f.Run(1,0)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Frame->PrimitiveState==2,"playback-only completion advances to terminal state without transfer");Need(f.Run(1,0)==VMPrimitiveExitCode::GOTO_TRUE&&f.Support->Container==f.Parent.get(),"playback-only Reach completes");
 Fixture e;e.Meta(e.Support.get(),4);e.Target(e.Support.get());e.Actor->SetPersonData(VMPersonDataVariable::PersonsAge,17);e.Actor->SetPersonData(VMPersonDataVariable::Posture,1);
 Need(e.Run(1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&e.Actor->CurrentAnimationState()->Anim->Name=="fixture-reach-c-6","signed child age and seated resource ordinal are source-selected");e.Actor->CurrentAnimationState()->EndReached=true;
 Need(e.Run(1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&e.Frame->PrimitiveState==3&&e.C.TS1SourceSlot(e.Actor.get(),1)==nullptr,"mode1 empty hand fails rather than grabbing from target support");Need(e.Run(1)==VMPrimitiveExitCode::GOTO_FALSE&&e.Actor->TS1AnimationController.ModelState==1,"failed seated Reach restores source posture state");
 Fixture m;m.Target(nullptr);Need(m.Run(2,0)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&m.Actor->CurrentAnimationState()->Anim->Name=="fixture-reach-a-9","mode2 playback resolves mouth-height resource without object endpoints");
}
void FirstEntryFailures(){
 Fixture missing;missing.Target(nullptr);
 Need(missing.Run(0,0)==VMPrimitiveExitCode::GOTO_FALSE&&missing.Frame->PrimitiveState==0&&!missing.Actor->CurrentAnimationState(),"state0 missing endpoint rejects even for playback-only Reach");
 Fixture noClip;noClip.Meta(noClip.Support.get(),8.f);noClip.Target(noClip.Support.get());
 Need(noClip.Run(1,0)==VMPrimitiveExitCode::GOTO_TRUE&&noClip.Frame->PrimitiveState==0&&!noClip.Actor->CurrentAnimationState(),"state0 animation selection failure succeeds for playback-only without an instance");
}
void PutAndCallback(){
 Fixture f;f.Meta(f.Support.get(),4.f);f.C.TS1CommitPhysicalPlacement(f.Item.get(),f.C.TS1PhysicalRequest(f.Item.get(),f.Actor.get(),0));f.Target(f.Support.get());
 Need(f.Run(1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Frame->PrimitiveState==1&&f.Actor->CurrentAnimationState()->Anim->Name=="fixture-reach-a-5","mode1 starts table-height put using carried source1 item");
 f.Actor->TS1AnimationController.Events={0,0};Need(f.Run(1)==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Frame->PrimitiveState==2,"mode1 drains duplicate transfer events but commits put once");
 Need(f.C.TS1SourceSlot(f.Actor.get(),1)==nullptr&&f.C.TS1SourceSlot(f.Support.get(),1)==f.Item.get()&&f.Item->Container==f.Support.get(),"mode1 put moves carried item into requested support physical0");
 Need(f.Actor->GetAttribute(0)==2&&f.Item->GetAttribute(1)==74&&!f.Actor->CurrentAnimationState(),"mode1 selects CT - Put once, with moved StackOID and fifth temp argument");
 Need(f.Run(1)==VMPrimitiveExitCode::GOTO_TRUE&&f.Actor->TS1AnimationController.CarrySuppressed==0,"successful put completes source cleanup after callback retires foreground");
}
void LegacyMarker(){
 Fixture f;f.Frame->TS1LegacyReach=true;f.Actor->TS1AnimationController.Events={88};auto* a=FSO::Content::Content::Get()->AvatarAnimations->Get("a2o-reach-floorht.anim");auto old=std::make_shared<VMAnimationState>(a,false);f.Actor->Animations={old};
 Need(f.Run()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&f.Actor->CurrentAnimationState()==old.get()&&!old->TS1Clock&&f.Frame->PrimitiveState==0,"saved legacy Reach keeps old instance/clock instead of replaying source entry");
 Need(f.Actor->TS1AnimationController.Events.empty(),"legacy original-lifecycle polling retains its prior FIFO behavior");
 Fixture e;e.Actor->TS1AnimationController.OriginalLifecycle=false;Need(e.Run()==VMPrimitiveExitCode::CONTINUE_NEXT_TICK&&e.Frame->TS1LegacyReach,"new legacy-policy operation records durable per-frame adapter ownership");
}
}
int main(){try{VM::UseWorld(false);PickupAndCallback();QueryFailure();HeightAndMode();FirstEntryFailures();PutAndCallback();LegacyMarker();std::printf("PASS %d source Reach controls\n",checks);return 0;}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
