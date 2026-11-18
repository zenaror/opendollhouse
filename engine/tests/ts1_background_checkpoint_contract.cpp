// SPDX-License-Identifier: MPL-2.0
// Native channel persistence controls. Original background event-dispatch and
// complete classic C2 parity require their separate source/runtime acceptance.
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
 FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"background checkpoint",true};
 OBJD Definition;FSO::Content::GameObject Object;std::unique_ptr<VMAvatar> Actor;
 Fixture(){Definition.GUID=0xf0459020;Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;Actor=std::make_unique<VMAvatar>(&Object);Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);}
 std::shared_ptr<Model::VMAnimationState> Clip(){
  auto resource=FSO::Content::Content::Get()->AvatarAnimations->Get("fixture-background-checkpoint.anim");
  Check(resource!=nullptr,"missing isolated animation provider");resource->Duration=1000;resource->Motions.resize(1);resource->Motions[0].FrameCount=30;
  resource->Motions[0].TimeProperties.resize(1);auto& properties=resource->Motions[0].TimeProperties[0].Items;properties.resize(2);properties[0].ID=100;properties[1].ID=500;
  FSO::Vitaboy::PropertyListItem property;property.KeyPairs={{"xevt","1"}};properties[0].Properties.Items={property};property.KeyPairs={{"sound","fixture"}};properties[1].Properties.Items={property};
  auto state=std::make_shared<Model::VMAnimationState>(resource,false);state->StartTS1Clock(1234,1.25f);state->TS1Phase=.375f;state->TS1EventCursor=1;
  state->Loop=true;state->TS1ScriptAnimation=true;state->Weight=.6f;state->EventsRun=2;state->EventQueue={1,101,2};return state;
 }
 std::vector<uint8_t> Write(AvatarRecord record,uint32_t version=20){st::Writer bytes(st::Endian::Little);Archive fields(bytes);InteractionCheckpointCodec codec(Machine,fields,version);codec.background(record);return bytes.ToArray();}
 AvatarRecord Read(const std::vector<uint8_t>& bytes,uint32_t version=20){st::Reader input(bytes);Archive fields(input);InteractionCheckpointCodec codec(Machine,fields,version);AvatarRecord record;record.background=Actor->TS1BackgroundAnimationState;record.backgroundPosture=Actor->TS1BackgroundPosture;codec.background(record);Check(!input.has_more(),"background channel left trailing bytes");return record;}
};
void Persistence(){
 Fixture f;AvatarRecord original;original.background=f.Clip();original.backgroundPosture=7;
 auto foreground=f.Clip(),carry=f.Clip();foreground->EventQueue={9};carry->EventQueue={10};
 f.Actor->Animations={foreground};f.Actor->CarryAnimationState=carry;
 const auto bytes=f.Write(original);auto restored=f.Read(bytes);
 Check(restored.background&&restored.backgroundPosture==7,"literal unsupported cached posture was normalized");
 Check(restored.background->Anim==original.background->Anim&&restored.background->Loop&&restored.background->TS1Clock&&restored.background->TS1ScriptAnimation,"background resource/clock/origin lost");
 Check(restored.background->TS1Phase==.375f&&restored.background->TS1Timestamp==1234&&restored.background->TS1EventCursor==1,"background playback/event cursor lost");
 Check(restored.background->EventQueue==std::vector<int16_t>({1,101,2})&&restored.background->EventsRun==2,"background queued/consumed events lost");
 Check(f.Write(restored)==bytes,"background record was not byte stable");
 InteractionCheckpointCodec::publishBackground(*f.Actor,restored);
 Check(f.Actor->TS1BackgroundAnimationState&&f.Actor->TS1BackgroundPosture==7,"successful staging did not publish background");
 Check(f.Actor->CurrentAnimationState()==foreground.get()&&f.Actor->CarryAnimationState==carry&&foreground->EventQueue==std::vector<int16_t>({9})&&carry->EventQueue==std::vector<int16_t>({10}),"background publication replaced foreground/carry or their events");
 for(int16_t posture:{int16_t(-1),int16_t(0),int16_t(1),int16_t(2),int16_t(INT16_MIN),int16_t(INT16_MAX)}){AvatarRecord empty;empty.backgroundPosture=posture;auto decoded=f.Read(f.Write(empty));Check(!decoded.background&&decoded.backgroundPosture==posture,"empty background lost literal posture cache");}
 const auto live=f.Actor->TS1BackgroundAnimationState;const auto posture=f.Actor->TS1BackgroundPosture;
 // Empty optional state replaces a seeded staging reference, while ownership
 // of the prior live channel remains untouched until successful publication.
 {auto staged=live;const std::vector<uint8_t> absent{0};st::Reader input(absent);Archive fields(input);InteractionCheckpointCodec codec(f.Machine,fields,20);codec.animation(staged);
  Check(!staged&&!input.has_more(),"common optional animation reader inherited a stale pointer");
  Check(f.Actor->TS1BackgroundAnimationState==live&&f.Actor->CurrentAnimationState()==foreground.get()&&f.Actor->CarryAnimationState==carry,"staging absence cleared a live animation channel");}
 auto load=[&](const std::vector<uint8_t>& raw){auto candidate=f.Read(raw);InteractionCheckpointCodec::publishBackground(*f.Actor,candidate);};
 auto invalid=bytes;invalid.pop_back();Reject([&]{load(invalid);},"truncated late posture published partial channel");
 Check(f.Actor->TS1BackgroundAnimationState==live&&f.Actor->TS1BackgroundPosture==posture&&f.Actor->CurrentAnimationState()==foreground.get(),"failed late decode mutated live channels");
 invalid=bytes;const auto nameSize=original.background->Anim->Name.size();invalid.at(1+4+nameSize+1)^=1;
 Reject([&]{load(invalid);},"changed background resource signature admitted");
 Check(f.Actor->TS1BackgroundAnimationState==live&&f.Actor->TS1BackgroundPosture==posture,"resource failure published background state");
 for(uint32_t version:{10u,15u,18u,19u}){
  Reject([&]{f.Write(original,version);},"older output silently dropped background");
  AvatarRecord cached;cached.backgroundPosture=0;Reject([&]{f.Write(cached,version);},"older output silently dropped posture history");
  AvatarRecord empty;Check(f.Write(empty,version).empty(),"older output acquired background bytes");
  const auto migrated=f.Read({},version);Check(!migrated.background&&migrated.backgroundPosture==-1,"older input inferred background from foreground loop");
 }
}
}
int main(){try{VM::UseWorld(false);Persistence();std::cout<<"PASS TS1 background checkpoint: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
