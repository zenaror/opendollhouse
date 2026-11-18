// SPDX-License-Identifier: MPL-2.0
// Native ODHI18 lossless codec/ownership contract. This synthetic fixture does
// not establish the original game's C2 save/continue behavior.
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
// Isolated fixture owner lookup; keep this seam out of the product API.
#define private public
#include "simantics/VM.h"
#include "content/ContentManager.h"
#undef private
#include "../sims3ds/src/simantics/utils/VMTS1InteractionCheckpoint.cpp"
#include <iostream>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Routing;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
int checks=0;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);++checks;}
template<class F>void Reject(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,why);}
TS1MotionRequest Request(){
 TS1MotionRequest m;m.Kind=TS1MotionKind::Body;m.Token=UINT64_C(0x1122334455667788);m.NextToken=m.Token+1;
 m.OwnerFrame=0;m.OwnerCaller=11;m.OwnerCallee=12;m.OwnerCodeGUID=0x12345678;m.OwnerRoutine=4096;m.OwnerPC=0;
 m.Status=-1;m.ModelState=4;m.Outcome=1;m.PathCursor=1;m.Stopped=0;m.StopDeadline=123456;
 m.StopRequested=true;m.LastModelTick=232561;m.Path={{101,202},{303,404},{505,606}};
 m.TargetX=1.25f;m.TargetY=-2.5f;m.TargetHeading=.75f;
 m.StartX=3.125f;m.StartY=-4.25f;m.StartHeading=-.875f;
 m.EndX=-5.5f;m.EndY=6.75f;m.EndHeading=1.125f;m.SegmentProgress=.375f;
 m.StopPhase1=.625f;m.StopPhase2=.125f;m.StopDistance1=7.5f;m.StopDistance2=8.25f;
 m.TurnStart=1.5f;m.TurnResidual=-.25f;m.StopProfileKnown=true;
 m.StopProfileAnimation="fixture-walk";m.StopProfileSignature=UINT64_C(0xfedcba9876543210);
 m.ForegroundInterruptible=1;m.ForegroundAnimation="fixture-turn";return m;
}
std::vector<uint8_t> Write(VM& vm,TS1MotionRequest& m,uint32_t version=18){
 st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(vm,fields,version);codec.modelMotion(m);return bytes.ToArray();
}
void Read(VM& vm,const std::vector<uint8_t>& bytes,TS1MotionRequest& m,uint32_t version=18){
 st::Reader input(bytes);Archive fields(input);InteractionCheckpointCodec codec(vm,fields,version);codec.modelMotion(m);Check(!input.has_more(),"model record has trailing bytes");
}
void AllFields(const TS1MotionRequest& a,const TS1MotionRequest& b){
#define FIELD(name) Check(a.name==b.name,"model field lost: " #name)
 FIELD(Kind);FIELD(Token);FIELD(NextToken);FIELD(OwnerFrame);FIELD(OwnerCaller);FIELD(OwnerCallee);
 FIELD(OwnerCodeGUID);FIELD(OwnerRoutine);FIELD(OwnerPC);FIELD(Status);FIELD(ModelState);FIELD(Outcome);
 FIELD(PathCursor);FIELD(Stopped);FIELD(StopDeadline);FIELD(StopRequested);FIELD(LastModelTick);
 FIELD(TargetX);FIELD(TargetY);FIELD(TargetHeading);FIELD(StartX);FIELD(StartY);FIELD(StartHeading);
 FIELD(EndX);FIELD(EndY);FIELD(EndHeading);FIELD(SegmentProgress);FIELD(StopPhase1);FIELD(StopPhase2);
 FIELD(StopDistance1);FIELD(StopDistance2);FIELD(TurnStart);FIELD(TurnResidual);FIELD(StopProfileKnown);
 FIELD(StopProfileAnimation);FIELD(StopProfileSignature);FIELD(ForegroundInterruptible);FIELD(ForegroundAnimation);
 FIELD(PendingFailureCode);FIELD(PendingFailureBlocker);
 FIELD(PendingCollisionStatus);FIELD(PendingCollisionObject);FIELD(BlendActive);
 FIELD(BlendStart);FIELD(BlendDuration);FIELD(BlendFrom);FIELD(BlendTo);
#undef FIELD
 Check(a.Path.size()==b.Path.size(),"motion path count lost");
 for(size_t i=0;i<a.Path.size();++i)Check(a.Path[i].X==b.Path[i].X&&a.Path[i].Y==b.Path[i].Y,"motion path point lost");
 Check(!b.ModelUpdating,"transient model update flag restored");
}
void CodecContracts(VM& vm){
 vm.Context->Clock->Ticks=232561;
 auto original=Request();auto bytes=Write(vm,original);TS1MotionRequest restored;Read(vm,bytes,restored);AllFields(original,restored);
 Check(Write(vm,restored)==bytes,"model record is not byte-stable after reopen");
 for(auto kind:{TS1MotionKind::Body,TS1MotionKind::Route}){original.Kind=kind;auto payload=Write(vm,original);TS1MotionRequest candidate;Read(vm,payload,candidate);AllFields(original,candidate);}
 original=Request();original.Kind=TS1MotionKind::Route;original.Status=3;original.Outcome=3;original.PendingFailureCode=5;original.PendingFailureBlocker=42;
 {auto payload=Write(vm,original);TS1MotionRequest candidate;Read(vm,payload,candidate);AllFields(original,candidate);}
 original.Status=1;Reject([&]{Write(vm,original);},"deferred routing failure admitted without failed model status");
 original.Kind=TS1MotionKind::Body;original.Status=3;Reject([&]{Write(vm,original);},"body request admitted a deferred routing failure");
 original=Request();original.PendingFailureCode=14;Reject([&]{Write(vm,original);},"unknown deferred failure code admitted");
 original=Request();original.Kind=TS1MotionKind::Route;original.Status=1;original.Outcome=0;original.PendingCollisionStatus=7;original.PendingCollisionObject=43;
 original.BlendActive=true;original.BlendStart=1000;original.BlendDuration=250;original.BlendFrom=.9f;original.BlendTo=.2f;
 {auto payload=Write(vm,original);TS1MotionRequest candidate;Read(vm,payload,candidate);AllFields(original,candidate);}
 original.PendingCollisionStatus=6;Reject([&]{Write(vm,original);},"accepted through-wall status stored as pending collision");
 original.PendingCollisionStatus=51;Reject([&]{Write(vm,original);},"unknown collision status admitted");
 original.PendingCollisionStatus=7;original.Kind=TS1MotionKind::Body;Reject([&]{Write(vm,original);},"body request admitted a route collision");
 original=Request();original.BlendActive=true;original.BlendDuration=99;Reject([&]{Write(vm,original);},"active blend admitted a short duration");
 original.BlendDuration=100;original.BlendFrom=-.1f;Reject([&]{Write(vm,original);},"active blend admitted negative weight");
 original.BlendFrom=1;original.BlendTo=1.1f;Reject([&]{Write(vm,original);},"active blend admitted excessive target weight");
 original.BlendTo=1;original.ForegroundAnimation.clear();Reject([&]{Write(vm,original);},"active blend admitted missing foreground binding");
 for(int state:{0,1,2,5,8,10,11}){original=Request();original.ModelState=state;Reject([&]{Write(vm,original);},"body model admitted ungenerated state");}
 original=Request();original.Status=4;Reject([&]{Write(vm,original);},"unknown model status admitted");
 original=Request();original.Outcome=4;Reject([&]{Write(vm,original);},"unknown model outcome admitted");
 original=Request();original.Stopped=2;Reject([&]{Write(vm,original);},"unknown model stopped flag admitted");
 original=Request();original.Status=2;Reject([&]{Write(vm,original);},"ready model admitted nonterminal outcome/state");
 original=Request();original.Status=3;Reject([&]{Write(vm,original);},"failed model admitted nonfailure outcome");
 original=Request();original.PathCursor=2;Reject([&]{Write(vm,original);},"model cursor advanced into tangent-only node");
 original=Request();original.LastModelTick=232562;Reject([&]{Write(vm,original);},"model update timestamp beyond saved clock admitted");
 original=Request();original.LastModelTick=-2;Reject([&]{Write(vm,original);},"model update timestamp below sentinel admitted");
 original=Request();original.StopProfileKnown=false;Reject([&]{Write(vm,original);},"unbound stop profile retained sampled window values");
 original=Request();original.StopProfileSignature=0;Reject([&]{Write(vm,original);},"known stop profile lost resource signature");
 // Signed foot displacements and custom anchor ordering are source-valid;
 // do not replace them with guessed nonnegative/ordered profile constraints.
 original=Request();original.StopDistance1=-3.5f;original.StopPhase1=.125f;original.StopPhase2=.875f;
 {auto payload=Write(vm,original);TS1MotionRequest candidate;Read(vm,payload,candidate);AllFields(original,candidate);}
 original=Request();original.ModelUpdating=true;Reject([&]{Write(vm,original);},"capture admitted an in-progress model update");
 original=Request();original.TargetX=std::numeric_limits<float>::quiet_NaN();Reject([&]{Write(vm,original);},"capture admitted non-finite motion");
 original=Request();original.Path.resize(65537);Reject([&]{Write(vm,original);},"capture admitted an oversized motion path");
 original=Request();original.PathCursor=3;Reject([&]{Write(vm,original);},"capture admitted out-of-range path cursor");
 original=Request();original.Kind=static_cast<TS1MotionKind>(255);Reject([&]{Write(vm,original);},"capture admitted unknown model kind");
 auto malformed=bytes;malformed[0]=255;TS1MotionRequest candidate;
 Reject([&]{Read(vm,malformed,candidate);},"reader admitted unknown model kind");
 // ODHI18 fixed prefix: kind1 + tokens16 + owner16 + state24 + stop1 + tick8;
 // the bounded path count starts66, before any path allocation.
 malformed=bytes;malformed[66]=1;malformed[67]=0;malformed[68]=1;malformed[69]=0;
 Reject([&]{Read(vm,malformed,candidate);},"reader allocated oversized motion path");
 malformed=bytes;malformed.resize(65);
 Reject([&]{Read(vm,malformed,candidate);},"reader admitted truncated motion prefix");
 for(uint32_t version:{10u,15u,16u,17u}){
  original=Request();Reject([&]{Write(vm,original,version);},"legacy output silently dropped owned motion");
  original=TS1MotionRequest{};Check(Write(vm,original,version).empty(),"legacy layout acquired model bytes");
  candidate=Request();Read(vm,{},candidate,version);
  Check(candidate.Kind==TS1MotionKind::None&&!candidate.Token&&candidate.OwnerFrame==-1&&candidate.NextToken==1,"legacy input inferred live motion");
 }
}
struct OwnershipFixture {
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"motion fixture",true};
 OBJD Def;FSO::Content::GameObject Object;std::unique_ptr<VMAvatar> Actor;std::unique_ptr<VMGameObject> Target;
 VMRoutine Routine;
 OwnershipFixture(){
  Def.GUID=0x12345678;Object.OBJ=&Def;Object.GUID=Def.GUID;Object.Resource=&Resource;
  Actor=std::make_unique<VMAvatar>(&Object);Target=std::make_unique<VMGameObject>(&Object,nullptr);
  Actor->ObjectID=11;Target->ObjectID=12;Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
  Routine.ID=4096;Routine.Instructions.resize(1);Routine.Instructions[0].Opcode=22;
 }
 std::shared_ptr<VMStackFrame> Frame(bool route=false){
  std::shared_ptr<VMStackFrame> f=route?std::static_pointer_cast<VMStackFrame>(std::make_shared<VMRoutingFrame>()):std::make_shared<VMStackFrame>();
  f->Thread=Actor->Thread.get();f->Caller=Actor.get();f->Callee=Target.get();f->Routine=&Routine;
  f->CodeOwner=&Object;f->PrimitiveState=1;f->TS1MotionToken=Request().Token;return f;
 }
};
void OwnershipContracts(){
 OwnershipFixture f;st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(f.Machine,fields,18);
 auto m=Request();auto frame=f.Frame();std::vector<std::shared_ptr<VMStackFrame>> staged{frame};
 codec.validateModelMotion(m,f.Actor.get(),staged);Check(true,"valid staged body model rejected");
 Check(f.Actor->Thread->Stack.empty()&&!f.Actor->TS1ModelMotion.Token,"ownership validation published staged motion");
 staged.push_back(frame);Reject([&]{codec.validateModelMotion(m,f.Actor.get(),staged);},"duplicate frame token admitted");staged.pop_back();
 auto expectOwnerReject=[&](TS1MotionRequest changed){Reject([&]{codec.validateModelMotion(changed,f.Actor.get(),staged);},"changed model owner identity admitted");};
 auto bad=m;bad.OwnerFrame=1;expectOwnerReject(bad);bad=m;bad.OwnerCaller=12;expectOwnerReject(bad);
 bad=m;bad.OwnerCallee=11;expectOwnerReject(bad);bad=m;bad.OwnerCodeGUID++;expectOwnerReject(bad);
 bad=m;bad.OwnerRoutine++;expectOwnerReject(bad);bad=m;bad.OwnerPC=1;expectOwnerReject(bad);bad=m;bad.Token++;expectOwnerReject(bad);
 frame->PrimitiveState=0;expectOwnerReject(m);frame->PrimitiveState=1;
 f.Routine.Instructions[0].Opcode=27;expectOwnerReject(m);f.Routine.Instructions[0].Opcode=22;
 Reject([&]{codec.validateModelMotion(TS1MotionRequest{},f.Actor.get(),staged);},"inactive avatar admitted dangling frame token");
 Reject([&]{codec.validateModelMotion(m,nullptr,staged);},"non-avatar thread admitted motion token");
 frame=f.Frame(true);staged={frame};m.Kind=TS1MotionKind::Route;codec.validateModelMotion(m,f.Actor.get(),staged);
 auto route=static_cast<VMRoutingFrame*>(frame.get());route->TS1SharedModelRouting=false;
 expectOwnerReject(m);route->TS1SharedModelRouting=true;
 // A failed candidate must be rejected before publish touches any live field.
 Record record;record.entity=f.Actor.get();record.preparedThread=std::make_unique<VMThread>(&f.Context,f.Actor.get(),16);
 record.preparedThread->Stack=staged;record.avatar=std::make_unique<AvatarRecord>();record.avatar->modelMotion=m;
 record.avatar->modelMotion.OwnerCallee=99;std::map<int16_t,Record> records;records.emplace(11,std::move(record));
 auto liveThread=f.Actor->Thread.get();f.Actor->TS1ModelMotion.NextToken=77;f.Context.Clock->Ticks=232560;f.Context.RandomSeed=123;
 OBJM classic;Reject([&]{codec.publish(records,classic);},"publish admitted malformed model ownership");
 Check(f.Actor->Thread.get()==liveThread&&f.Actor->Thread->Stack.empty()&&f.Actor->TS1ModelMotion.NextToken==77&&f.Context.Clock->Ticks==232560&&f.Context.RandomSeed==123,"failed motion preflight published partial live state");
}
void StopProfileContracts(){
 OwnershipFixture f;st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(f.Machine,fields,18);
 auto animation=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-motion-stop.anim");
 animation->Translations={FSO::Vitaboy::Vector3(1.f,2.f,3.f)};animation->TranslationCount=1;
 auto& skeleton=*f.Actor->Avatar->BaseSkeleton;
 if(skeleton.Bones.empty())skeleton.Bones.push_back(std::make_unique<FSO::Vitaboy::Bone>());
 auto m=Request();m.StopProfileAnimation=animation->Name;m.StopProfileSignature=TS1StopProfileSignature(*animation,skeleton);
 codec.validateModelProfile(m,*f.Actor);Check(true,"bound stop profile rejected");
 animation->Translations[0].X+=1.f;
 Reject([&]{codec.validateModelProfile(m,*f.Actor);},"changed animation displacement keys admitted");
 animation->Translations[0].X-=1.f;
 skeleton.Bones[0]->Translation.Y+=1.f;
 Reject([&]{codec.validateModelProfile(m,*f.Actor);},"changed skeleton bind translation admitted");
 skeleton.Bones[0]->Translation.Y-=1.f;
 skeleton.Bones[0]->ParentName+="changed";
 Reject([&]{codec.validateModelProfile(m,*f.Actor);},"changed skeleton bone hierarchy admitted");
 m.StopProfileKnown=false;codec.validateModelProfile(m,*f.Actor);
 Check(true,"unknown stop profile incorrectly inferred a resource binding");
}
void FrameTokenContracts(){
 OwnershipFixture f;
 auto content=FSO::Content::Content::Get();
 struct RestoreProvider {
  FSO::Content::Content* Content;std::unique_ptr<FSO::Content::WorldObjectProvider> Original;
  ~RestoreProvider(){Content->WorldObjects=std::move(Original);}
 } provider{content,std::move(content->WorldObjects)};
 content->WorldObjects=std::make_unique<FSO::Content::WorldObjectProvider>(content);
 content->WorldObjects->Cache[f.Def.GUID]=std::make_unique<FSO::Content::GameObject>(f.Object);
 auto bhav=new BHAV();bhav->ChunkProcessed=true;bhav->ChunkID=4096;
 BHAVInstruction instruction;instruction.Opcode=22;bhav->Instructions.push_back(instruction);f.Iff.AddChunk(bhav);
 auto frame=f.Frame();frame->Routine=f.Machine.Assemble(bhav);frame->Args.resize(4);frame->Locals.resize(std::max(frame->Routine->Locals,frame->Routine->Arguments));
 f.Actor->Thread->Stack={frame};
 // The reader resolves the fresh IDs through the ordinary VM identity map.
 f.Machine.AddEntity(f.Actor.get());f.Machine.AddEntity(f.Target.get());
 auto write=[&](uint32_t version){st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(f.Machine,fields,version);codec.thread(*f.Actor->Thread);return bytes.ToArray();};
 auto read=[&](const std::vector<uint8_t>& bytes,uint32_t version){auto result=std::make_unique<VMThread>(&f.Context,f.Actor.get(),16);st::Reader input(bytes);Archive fields(input);InteractionCheckpointCodec codec(f.Machine,fields,version);codec.thread(*result);Check(!input.has_more(),"frame token record has trailing bytes");return result;};
 auto payload=write(18);auto restored=read(payload,18);
 Check(restored->Stack.size()==1&&restored->Stack[0]->TS1MotionToken==frame->TS1MotionToken,"frame motion token was dropped");
 Reject([&]{write(17);},"old output dropped a live frame token before projection");
 frame->TS1MotionToken=0;auto legacy=read(write(17),17);
 Check(legacy->Stack.size()==1&&!legacy->Stack[0]->TS1MotionToken,"legacy frame inferred a motion token");
 f.Machine.Entities.clear();
}
}
int main(){try{VM::UseWorld(false);VMContext context(nullptr);VM vm(&context,nullptr);CodecContracts(vm);OwnershipContracts();StopProfileContracts();FrameTokenContracts();std::cout<<"PASS native TS1 motion checkpoint: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
