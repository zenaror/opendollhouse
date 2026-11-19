// SPDX-License-Identifier: MPL-2.0
// Native21/22/23 persistence of source model-owned script and locomotion state. This does not claim
// that classic saves contain the missing model admission/event metadata.
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "../sims3ds/src/simantics/utils/VMTS1InteractionCheckpoint.cpp"
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Utils;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
namespace {
int checks=0;
void Check(bool good,const char* why){if(!good)throw std::runtime_error(why);++checks;}
template<class F>void Reject(F f,const char* why){bool failed=false;try{f();}catch(const std::exception&){failed=true;}Check(failed,why);}
struct Fixture {
 VMContext Context{nullptr};VM Machine{&Context,nullptr};IffFile Iff;
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"controller checkpoint",true};
 OBJD Definition;FSO::Content::GameObject Object;std::unique_ptr<VMAvatar> Actor;
 Fixture(){Definition.GUID=0xf0459021;Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;Actor=std::make_unique<VMAvatar>(&Object);}
 std::vector<uint8_t> Write(AvatarRecord record,uint32_t version=23){st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(Machine,fields,version);codec.animationController(record);return bytes.ToArray();}
 AvatarRecord Read(const std::vector<uint8_t>& bytes,uint32_t version=23){st::Reader input(bytes);Archive fields(input);InteractionCheckpointCodec codec(Machine,fields,version);AvatarRecord record;record.animationController=Actor->TS1AnimationController;codec.animationController(record);Check(!input.has_more(),"controller left trailing bytes");return record;}
 std::vector<uint8_t> Timing(Model::VMAnimationState& state,uint32_t version){st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(Machine,fields,version);codec.animationTiming(state,0);return bytes.ToArray();}
 void ReadTiming(const std::vector<uint8_t>& bytes,Model::VMAnimationState& state,uint32_t version){st::Reader input(bytes);Archive fields(input);InteractionCheckpointCodec codec(Machine,fields,version);codec.animationTiming(state,0);Check(!input.has_more(),"animation timing left trailing bytes");}
};
void Persistence(){
 Fixture f;AvatarRecord original;auto& s=original.animationController;
 s.ScriptActive=1;s.EventsRun=300;s.ModelState=0;s.Events={INT32_MIN,-100,-1,0,99,100,INT32_MAX};s.CarrySuppressed=1;s.CarryKind=6;s.CarryCachedIndex=-1;
 // Admission and consumed event count are independent of clip ownership.
 Check(original.animations.empty()&&!original.carry,"fixture unexpectedly owns a clip");
 const auto bytes=f.Write(original);auto decoded=f.Read(bytes);const auto& d=decoded.animationController;
 Check(d.OriginalLifecycle&&d.ScriptActive==1&&d.EventsRun==300&&d.ModelState==0,"script controller scalars lost");
 Check(d.Events==s.Events&&d.CarrySuppressed==1&&d.CarryKind==6&&d.CarryCachedIndex==-1,"signed FIFO or custom/suppressed carry lost");
 Check(f.Write(decoded)==bytes,"controller byte roundtrip differs");
 InteractionCheckpointCodec::publishAnimationController(*f.Actor,decoded);
 Check(f.Actor->TS1AnimationController.Events==s.Events,"staged controller did not publish");
 auto load=[&](const std::vector<uint8_t>& raw){auto staged=f.Read(raw);InteractionCheckpointCodec::publishAnimationController(*f.Actor,staged);};
 auto truncated=bytes;truncated.pop_back();Reject([&]{load(truncated);},"truncated late field published partial state");
 Check(f.Actor->TS1AnimationController.Events==s.Events&&f.Actor->TS1AnimationController.EventsRun==300,"failed decode changed live controller");
 auto oversized=bytes; // bool + active + consumed + modelstate then bounded FIFO count
 for(int i=0;i<4;++i)oversized.at(13+i)=0xff;
 Reject([&]{load(oversized);},"unbounded controller FIFO accepted");
 for(int which=0;which<6;++which){AvatarRecord bad;auto& b=bad.animationController;
  if(which==0)b.ScriptActive=2;if(which==1)b.EventsRun=-1;if(which==2)b.ModelState=13;
  if(which==3)b.CarrySuppressed=-1;if(which==4)b.CarryKind=7;if(which==5)b.CarryCachedIndex=1;
  Reject([&]{f.Write(bad);},"invalid controller domain accepted");}
 for(int state=0;state<=3;++state)for(int kind=0;kind<=6;++kind){AvatarRecord valid;valid.animationController.ModelState=state;valid.animationController.CarryKind=kind;Check(f.Read(f.Write(valid)).animationController.CarryKind==kind,"valid independent controller state rejected");}
}
void LocomotionPersistence(){
 Fixture f;AvatarRecord original;auto& s=original.animationController;
 s.MotionStepKnown=false;s.ModelState=6;s.MotionCode=19;s.Moving=0;s.StopLatched=7;s.StepLatched=-2;
 s.MotionForegroundTransfer=1;s.MotionTransitionKnown=true;
 s.MotionStartX=414.25f;s.MotionStartY=437.5f;s.MotionStartHeading=-.75f;
 s.MotionEndX=411.125f;s.MotionEndY=427.75f;s.MotionEndHeading=1.25f;
 s.MotionSavedX=298.5f;s.MotionSavedY=344.25f;s.MotionSavedHeading=-2.25f;
 auto bytes=f.Write(original,22);auto decoded=f.Read(bytes,22);const auto& d=decoded.animationController;
#define LOCOMOTION_FIELD(name) Check(s.name==d.name,"locomotion controller lost " #name)
 LOCOMOTION_FIELD(OriginalLocomotion);LOCOMOTION_FIELD(MotionCode);LOCOMOTION_FIELD(Moving);
 LOCOMOTION_FIELD(StopLatched);LOCOMOTION_FIELD(StepLatched);LOCOMOTION_FIELD(MotionForegroundTransfer);LOCOMOTION_FIELD(MotionTransitionKnown);
 LOCOMOTION_FIELD(MotionStartX);LOCOMOTION_FIELD(MotionStartY);LOCOMOTION_FIELD(MotionStartHeading);
 LOCOMOTION_FIELD(MotionEndX);LOCOMOTION_FIELD(MotionEndY);LOCOMOTION_FIELD(MotionEndHeading);
 LOCOMOTION_FIELD(MotionSavedX);LOCOMOTION_FIELD(MotionSavedY);LOCOMOTION_FIELD(MotionSavedHeading);
#undef LOCOMOTION_FIELD
 Check(f.Write(decoded,22)==bytes,"locomotion controller byte roundtrip differs");
 // Persistent model markers may survive an ownership-token detach. No active
 // request is needed for these bytes to be meaningful.
 Check(decoded.modelMotion.Kind==Routing::TS1MotionKind::None&&d.StopLatched==7&&d.StepLatched==-2,
       "inactive ownership discarded literal model history");
 for(uint32_t version:{18u,20u,21u})Reject([&]{f.Write(original,version);},"old output discarded new model history");
 for(int state=0;state<=12;++state){AvatarRecord valid;valid.animationController.MotionStepKnown=false;valid.animationController.ModelState=state;
  Check(f.Read(f.Write(valid,22),22).animationController.ModelState==state,"new source FSM state lost");}
 for(int field=0;field<9;++field){auto invalid=original;auto& b=invalid.animationController;
  float* values[]={&b.MotionStartX,&b.MotionStartY,&b.MotionStartHeading,&b.MotionEndX,&b.MotionEndY,
                  &b.MotionEndHeading,&b.MotionSavedX,&b.MotionSavedY,&b.MotionSavedHeading};
  *values[field]=std::numeric_limits<float>::infinity();
  Reject([&]{f.Write(invalid,22);},"nonfinite transition anchor accepted");}
 auto invalid=original;invalid.animationController.MotionCode=-2;
 Reject([&]{f.Write(invalid,22);},"invalid negative foreground code accepted");
 invalid=original;invalid.animationController.OriginalLifecycle=false;
 Reject([&]{f.Write(invalid,22);},"source locomotion without lifecycle accepted");
 InteractionCheckpointCodec::publishAnimationController(*f.Actor,decoded);
 auto truncated=bytes;truncated.pop_back();
 Reject([&]{auto staged=f.Read(truncated,22);InteractionCheckpointCodec::publishAnimationController(*f.Actor,staged);},
        "truncated transition published partial state");
 Check(f.Actor->TS1AnimationController.MotionSavedHeading==s.MotionSavedHeading&&
       f.Actor->TS1AnimationController.StopLatched==s.StopLatched,"failed transition restore mutated actor");
 AvatarRecord legacy;legacy.animationController.OriginalLocomotion=false;
 legacy.animationController.ScriptActive=1;legacy.animationController.ModelState=0;
 auto old=f.Write(legacy,21);auto oldDecoded=f.Read(old,21);
 Check(!oldDecoded.animationController.OriginalLocomotion&&oldDecoded.animationController.MotionCode==-1&&
       !oldDecoded.animationController.MotionTransitionKnown&&oldDecoded.animationController.MotionSavedHeading==0,
       "old input invented locomotion history or inherited current actor markers");
}

void AnalyticMotionPersistence(){
 Fixture f;AvatarRecord source;auto& s=source.animationController;
 s.ModelState=6;s.MotionCode=19;s.MotionTransitionKnown=true;
 s.MotionStartX=414.25f;s.MotionStartY=437.5f;s.MotionEndX=411.125f;s.MotionEndY=427.75f;
 s.MotionStepKnown=true;s.MotionStepX=-.3333333432674408f;s.MotionStepY=1.00811767578125f;
 auto bytes=f.Write(source,23);auto restored=f.Read(bytes,23);
 Check(restored.animationController.MotionStepKnown&&
       restored.animationController.MotionStepX==s.MotionStepX&&restored.animationController.MotionStepY==s.MotionStepY,
       "Native23 lost unrounded analytical movement history");
 Check(f.Write(restored,23)==bytes,"Native23 analytic state is not byte stable");
 for(uint32_t version:{18u,21u,22u})Reject([&]{f.Write(source,version);},"old output silently dropped analytical history");
 auto native22=source;auto& old=native22.animationController;
 old.MotionStepKnown=false;old.MotionStepX=old.MotionStepY=0;
 auto oldBytes=f.Write(native22,22);auto oldDecoded=f.Read(oldBytes,22);
 Check(oldDecoded.animationController.OriginalLocomotion&&oldDecoded.animationController.ModelState==6&&
       oldDecoded.animationController.MotionTransitionKnown&&oldDecoded.animationController.MotionEndX==s.MotionEndX,
       "Native22 migration disabled or lost an active source transition");
 Check(!oldDecoded.animationController.MotionStepKnown&&oldDecoded.animationController.MotionStepX==0&&
       oldDecoded.animationController.MotionStepY==0,"Native22 invented missing analytical movement");
 Check(f.Write(oldDecoded,22)==oldBytes,"Native22 wire layout changed");
 auto unknown=f.Read(f.Write(oldDecoded,23),23);
 Check(!unknown.animationController.MotionStepKnown&&unknown.animationController.ModelState==6,
       "saving an old transition in23 fabricated known motion");
 auto invalid=native22;invalid.animationController.MotionStepX=.25f;
 Reject([&]{f.Write(invalid,23);},"unknown movement retained a fabricated displacement");
 for(int field=0;field<2;++field){auto bad=source;
  (field?bad.animationController.MotionStepY:bad.animationController.MotionStepX)=std::numeric_limits<float>::infinity();
  Reject([&]{f.Write(bad,23);},"nonfinite analytical vector accepted");}
 auto zeroActive=source;zeroActive.animationController.MotionStepX=zeroActive.animationController.MotionStepY=0;
 Reject([&]{f.Write(zeroActive,22);},"active known-zero source history silently lost its policy");
 AvatarRecord pristine;auto projected=f.Read(f.Write(pristine,22),22);
 Check(!projected.animationController.MotionStepKnown&&projected.animationController.ModelState==3,
       "pristine old-output adapter projection failed");
 AvatarRecord legacyZero;legacyZero.animationController.OriginalLocomotion=false;
 legacyZero.animationController.ModelState=0;legacyZero.animationController.ScriptActive=1;
 auto legacyProjected=f.Read(f.Write(legacyZero,22),22);
 Check(!legacyProjected.animationController.OriginalLocomotion&&!legacyProjected.animationController.MotionStepKnown&&
       legacyProjected.animationController.ScriptActive==1,"legacy zero policy projection lost existing fields");
 InteractionCheckpointCodec::publishAnimationController(*f.Actor,restored);
 auto cut=bytes;cut.pop_back();Reject([&]{auto staged=f.Read(cut,23);InteractionCheckpointCodec::publishAnimationController(*f.Actor,staged);},
       "truncated final displacement published partial controller");
 Check(f.Actor->TS1AnimationController.MotionStepKnown&&f.Actor->TS1AnimationController.MotionStepY==s.MotionStepY,
       "failed analytical-vector decode changed live history");
 auto badKnown=bytes;badKnown.at(badKnown.size()-9)=2;
 Reject([&]{auto staged=f.Read(badKnown,23);InteractionCheckpointCodec::publishAnimationController(*f.Actor,staged);},
       "invalid analytic-known boolean was accepted");
 Check(f.Actor->TS1AnimationController.MotionStepKnown&&f.Actor->TS1AnimationController.MotionStepY==s.MotionStepY,
       "invalid known boolean published partial state");

}

void ResourceBinding(){
 Fixture f;auto* resource=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-binding-checkpoint.anim");Check(resource!=nullptr,"isolated resource provider missing");
 resource->Duration=1000;resource->Motions.resize(2);
 for(size_t i=0;i<2;++i){auto& motion=resource->Motions[i];motion.BoneName=i?"b":"a";motion.FrameCount=30;motion.Duration=1000;motion.TimeProperties.resize(1);motion.TimeProperties[0].Items.resize(1);
  auto& item=motion.TimeProperties[0].Items[0];item.ID=100+100*i;FSO::Vitaboy::PropertyListItem property;property.KeyPairs={{"xevt",std::to_string(i+1)}};item.Properties.Items={property};}
 auto original=std::make_shared<Model::VMAnimationState>(resource,false);original->TS1BindingEventCursors={1,0};f.Actor->Animations={original};
 st::Writer output(st::Endian::Little);Archive writing(output);InteractionCheckpointCodec encoder(f.Machine,writing,21);encoder.animation(original);const auto bytes=output.ToArray();
 const auto oldSignature=AnimationSignature(*resource,*original,20);const auto signature=AnimationSignature(*resource,*original,21);
 auto rejectResource=[&]{Reject([&]{st::Reader input(bytes);Archive fields(input);InteractionCheckpointCodec decoder(f.Machine,fields,21);auto staged=f.Actor->Animations.front();decoder.animation(staged);f.Actor->Animations.front()=std::move(staged);},"changed per-motion resource identity accepted");Check(f.Actor->Animations.front()==original&&original->TS1BindingEventCursors==std::vector<int32_t>({1,0}),"resource binding failure published partial animation");};
 resource->Motions[0].BoneName="different-bone";Model::VMAnimationState renamed(resource,false);
 Check(AnimationSignature(*resource,renamed,20)==oldSignature&&AnimationSignature(*resource,renamed,21)!=signature,"bone identity version gate changed historical signature");rejectResource();resource->Motions[0].BoneName="a";
 std::swap(resource->Motions[0].TimeProperties,resource->Motions[1].TimeProperties);Model::VMAnimationState reassigned(resource,false);
 Check(AnimationSignature(*resource,reassigned,20)==oldSignature&&AnimationSignature(*resource,reassigned,21)!=signature,"equal-count reassigned binding tables not distinguished");rejectResource();std::swap(resource->Motions[0].TimeProperties,resource->Motions[1].TimeProperties);
 Model::VMAnimationState restored(resource,false);Check(AnimationSignature(*resource,restored,21)==signature,"restoring authored resource did not restore signature");
}

AvatarRecord Paired(int state){
 AvatarRecord record;auto& m=record.modelMotion;
 m.Kind=Routing::TS1MotionKind::Body;m.Token=1;m.NextToken=2;m.OwnerFrame=0;m.OwnerCaller=103;m.OwnerCallee=105;
 m.OwnerCodeGUID=0x1234;m.OwnerRoutine=4096;m.OwnerPC=0;m.Status=1;m.ModelState=state;m.Outcome=1;
 m.Path={{1,2},{3,4},{5,6}};m.PathCursor=0;m.Stopped=0;
 record.animationController.OriginalLocomotion=false;record.animationController.ModelState=state;record.animationController.ScriptActive=state==0?1:0;record.person[0]=state;
 return record;
}
void CoupledOwnership(){
 Fixture f;
 auto write=[&](AvatarRecord record,uint32_t version=21,bool allow=true){st::Writer out(st::Endian::Little);Archive fields(out);InteractionCheckpointCodec codec(f.Machine,fields,version);codec.modelMotion(record.modelMotion,allow);codec.animationController(record);codec.validateAnimationModelCoupling(record);return out.ToArray();};
 auto read=[&](const std::vector<uint8_t>& raw,int16_t posture){st::Reader input(raw);Archive fields(input);InteractionCheckpointCodec codec(f.Machine,fields,21);AvatarRecord staged;staged.person[0]=posture;codec.modelMotion(staged.modelMotion,true);codec.animationController(staged);codec.validateAnimationModelCoupling(staged);Check(!input.has_more(),"paired motion/controller trailing bytes");return staged;};
 for(int state=0;state<=2;++state){auto source=Paired(state);auto raw=write(source);auto restored=read(raw,state);Check(restored.modelMotion.ModelState==state&&restored.animationController.ModelState==state,"paired authored model state lost");
  Reject([&]{write(source,20,true);},"old layout accepted authored model state");Reject([&]{write(source,21,false);},"generic motion codec accepted unpaired script state");
  source.modelMotion.Status=2;source.modelMotion.Outcome=2;Check(!write(source).empty(),"ready model with coupled authored state rejected");
  source.animationController.ModelState=3;Reject([&]{write(source);},"mismatched paired model controller accepted");
 }
 // Native22 preserves a complete active transition together with its request.
 for(int state:{4,5,6,7,11,12}){
  auto source=Paired(state);auto& c=source.animationController;c.OriginalLocomotion=true;c.MotionStepKnown=false;
  c.MotionTransitionKnown=true;c.MotionCode=state==11?22:19;
  c.MotionStartX=14.5f;c.MotionStartY=17.25f;c.MotionStartHeading=-.25f;
  c.MotionEndX=20.125f;c.MotionEndY=22.75f;c.MotionEndHeading=.875f;
  c.MotionSavedX=30.5f;c.MotionSavedY=40.25f;c.MotionSavedHeading=1.75f;
  for(int outcome:state==7?std::vector<int>{1,2,3}:std::vector<int>{1}){
   source.modelMotion.Outcome=outcome;
   auto payload=write(source,22,true);st::Reader input(payload);Archive fields(input);
   InteractionCheckpointCodec decoder(f.Machine,fields,22);AvatarRecord restored;
   decoder.modelMotion(restored.modelMotion,true);decoder.animationController(restored);
   decoder.validateAnimationModelCoupling(restored);
   Check(!input.has_more()&&restored.modelMotion.ModelState==state&&
         restored.modelMotion.Outcome==outcome&&restored.animationController.MotionSavedHeading==1.75f&&
         restored.animationController.MotionEndX==20.125f,"paired transition or stop outcome lost");
  }
  auto invalid=source;invalid.animationController.ModelState=3;
  Reject([&]{write(invalid,22,true);},"mismatched source model/controller state accepted");
  invalid=source;invalid.animationController.MotionTransitionKnown=false;
  if(state==12)Check(!write(invalid,22,true).empty(),"interruptible wait incorrectly required movement anchors");
  else Reject([&]{write(invalid,22,true);},"source transition without anchors accepted");
  if(state!=4){invalid=source;invalid.animationController.OriginalLocomotion=false;
   Reject([&]{write(invalid,22,true);},"legacy model admitted new source transition");}
 }
 auto invalid=Paired(0);invalid.animationController.OriginalLifecycle=false;Reject([&]{write(invalid);},"legacy controller claimed source owned script state");
 invalid=Paired(0);invalid.animationController.ScriptActive=0;Check(!write(invalid).empty(),"historical state0 incorrectly required active script admission");
 invalid.person[0]=7;Check(!write(invalid).empty(),"unsupported posture lost literal retained model state");
 invalid=Paired(1);invalid.person[0]=2;Check(!write(invalid).empty(),"historical posture-reset model state was derived from current posture");
 auto valid=Paired(0);auto raw=write(valid);raw.pop_back();const auto live=f.Actor->TS1AnimationController;
 Reject([&]{auto staged=read(raw,0);InteractionCheckpointCodec::publishAnimationController(*f.Actor,staged);},"late controller truncation published paired state");
 Check(f.Actor->TS1AnimationController.ScriptActive==live.ScriptActive&&f.Actor->TS1AnimationController.ModelState==live.ModelState,"failed paired restore changed live controller");
}

void Compatibility(){
 Fixture f;
 for(uint32_t version:{10u,15u,18u,19u,20u}){
  AvatarRecord pristine;Check(f.Write(pristine,version).empty(),"pristine old output changed layout");
  auto old=f.Read({},version);Check(!old.animationController.OriginalLifecycle&&old.animationController.EventsRun==0&&old.animationController.ScriptActive==0,"legacy input invented source admission/event history");
  AvatarRecord legacy;legacy.animationController.OriginalLifecycle=false;legacy.animationController.OriginalLocomotion=false;legacy.animations.push_back(nullptr);
  Check(f.Write(legacy,version).empty(),"legacy old active foreground rejected");
  AvatarRecord owned;owned.animations.push_back(nullptr);Reject([&]{f.Write(owned,version);},"source policy lost behind old active clip");
  FSO::Vitaboy::Animation backgroundResource;AvatarRecord background; background.background=std::make_shared<Model::VMAnimationState>(&backgroundResource,false);
  Reject([&]{f.Write(background,version);},"old projection retained default background with unknown event history");
  background.animationController.OriginalLifecycle=false;Check(f.Write(background,version).empty(),"legacy policy metadata rejected default background");
  owned=AvatarRecord{};owned.animationController.Events={100};Reject([&]{f.Write(owned,version);},"old output discarded pending model event");
  owned=AvatarRecord{};owned.animationController.OriginalLifecycle=false;owned.animationController.EventsRun=1;Reject([&]{f.Write(owned,version);},"old output discarded consumed source count");
 }
 AvatarRecord legacy;legacy.animationController.OriginalLifecycle=false;legacy.animationController.OriginalLocomotion=false;legacy.animationController.Events={-123};Check(!f.Read(f.Write(legacy)).animationController.OriginalLifecycle,"Native21 lost legacy adapter policy");
 // The new interruptible bit has its own version gate. It is not inferred
 // from Loop, TS1ScriptAnimation or the legacy produced-event counter.
 FSO::Vitaboy::Animation animation;Model::VMAnimationState state(&animation,false);state.TS1ScriptInterruptible=true;
 auto bytes=f.Timing(state,21);Model::VMAnimationState decoded(&animation,false);f.ReadTiming(bytes,decoded,21);Check(decoded.TS1ScriptInterruptible,"interruptible bit lost");
 Reject([&]{f.Timing(state,20);},"old output discarded interruptible bit");
 state.TS1ScriptInterruptible=false;auto old=f.Timing(state,20);Check(bytes.size()==old.size()+17,"Native21 changed historical timing layout");
 decoded.TS1ScriptInterruptible=true;f.ReadTiming(old,decoded,20);Check(!decoded.TS1ScriptInterruptible,"old input inherited live interruptible bit");
 animation.Motions.resize(2);animation.Motions[0].TimeProperties.resize(1);animation.Motions[0].TimeProperties[0].Items.resize(2);
 animation.Motions[1].TimeProperties.resize(1);animation.Motions[1].TimeProperties[0].Items.resize(1);
 Model::VMAnimationState bindings(&animation,false);bindings.TS1BindingEventCursors={2,-1};bindings.TS1CompositionPriority=-10;bindings.TS1ClaimsBonePriority=true;
 auto bindingBytes=f.Timing(bindings,21);Model::VMAnimationState rebound(&animation,false);f.ReadTiming(bindingBytes,rebound,21);
 Check(rebound.TS1BindingEventCursors==std::vector<int32_t>({2,-1})&&rebound.TS1CompositionPriority==-10&&rebound.TS1ClaimsBonePriority,"per-binding event history/composition lost");
 Reject([&]{f.Timing(bindings,20);},"old output discarded binding history");
 bindings.TS1ClaimsBonePriority=false;bindings.TS1BindingEventCursors={3,0};Reject([&]{f.Timing(bindings,21);},"cursor beyond individual motion event count accepted");
 bindings.TS1BindingEventCursors={0};Reject([&]{f.Timing(bindings,21);},"resource motion/cursor count mismatch accepted");
 bindings.TS1BindingEventCursors={0,0};auto legacyBindingBytes=f.Timing(bindings,20);rebound.TS1ClaimsBonePriority=true;f.ReadTiming(legacyBindingBytes,rebound,20);
 Check(rebound.TS1BindingEventCursors==std::vector<int32_t>({0,0})&&!rebound.TS1ClaimsBonePriority&&rebound.TS1CompositionPriority==1,"legacy timing inherited binding state");
 Check(!f.Timing(rebound,21).empty(),"legacy clip cannot be captured in Native21 with derived binding storage");
 for(int32_t raw:{int32_t(7),int32_t(-2),int32_t(INT32_MAX)}){
  bindings.TS1ScriptInterruptible=raw;bindings.TS1BindingPreviousEventTime=-raw;
  auto rawBytes=f.Timing(bindings,21);Model::VMAnimationState literal(&animation,false);f.ReadTiming(rawBytes,literal,21);
  Check(literal.TS1ScriptInterruptible==raw&&literal.TS1BindingPreviousEventTime==-raw,"raw interruptible/previous event time normalized");
  Reject([&]{f.Timing(bindings,20);},"old output discarded raw instance history");
 }
 bindings.TS1ScriptInterruptible=0;bindings.TS1BindingPreviousEventTime=INT32_MIN;
 auto signedBytes=f.Timing(bindings,21);Model::VMAnimationState signedTime(&animation,false);f.ReadTiming(signedBytes,signedTime,21);
 Check(signedTime.TS1BindingPreviousEventTime==INT32_MIN,"signed previous event time lost");
 Reject([&]{f.Timing(bindings,20);},"old output discarded previous event time");

}
}
int main(){try{VM::UseWorld(false);Persistence();LocomotionPersistence();AnalyticMotionPersistence();Compatibility();CoupledOwnership();ResourceBinding();std::cout<<"PASS TS1 animation controller checkpoint: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
