// SPDX-License-Identifier: MPL-2.0
// Source-derived synthetic clock/event contracts; no commercial assets or window.
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/IffFile.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
static int count=0;
static void check(bool value,const char* name) { if(!value) throw std::runtime_error(name);++count; }
static bool near(float a,float b) { return std::abs(a-b)<0.00001f; }
static FSO::Vitaboy::TimePropertyListItem event(unsigned time,const char* value) {
 FSO::Vitaboy::TimePropertyListItem item;item.ID=time;
 FSO::Vitaboy::PropertyListItem property;property.KeyPairs.emplace_back("xevt",value);
 item.Properties.Items.push_back(property);return item;
}
int main() {
 try {
  FSO::Vitaboy::Animation asset;asset.Name="synthetic-clock";asset.Duration=1000;
  asset.Motions.resize(1);auto& motion=asset.Motions[0];motion.FrameCount=30;
  motion.TimeProperties.resize(1);motion.TimeProperties[0].Items={event(0,"0"),event(100,"1"),event(1000,"2")};
  VMAnimationState direct(&asset,false);direct.StartTS1Clock(10000,1);
  direct.AdvanceTS1Clock(10000);check(direct.CurrentFrame==0,"new clip starts at phase zero at its creation clock");
  direct.AdvanceTS1Clock(10050);check(near(direct.TS1Phase,.05f)&&near(direct.CurrentFrame,1.5f),"ordinary fifty-millisecond clock uses authored duration");
  direct.AdvanceTS1Clock(10050);check(near(direct.CurrentFrame,1.5f),"repeated clock cannot advance the phase twice");
  direct.AdvanceTS1Clock(11000);check(direct.EndReached&&direct.TS1Phase==1,"non-loop reaches its exact duration boundary");
  VMAnimationState looping(&asset,false);looping.Loop=true;looping.StartTS1Clock(0,1);
  check(!looping.AdvanceTS1Clock(1000)&&looping.TS1Phase==1,"loop equality retains the terminal phase");
  check(looping.AdvanceTS1Clock(1050)&&near(looping.TS1Phase,.05f),"loop wraps only beyond the endpoint");
  VMAnimationState backwards(&asset,true);backwards.StartTS1Clock(0,1);backwards.AdvanceTS1Clock(42);
  check(near(backwards.TS1Phase,.958f),"reverse animation starts at one and uses forty-two-millisecond clock");
  VMAnimationState retained(&asset,false);retained.StartTS1Clock(5000,1);retained.AdvanceTS1Clock(4242);
  check(near(retained.TS1Phase,-.758f),"absolute model-clock change retains its signed delta without invented rebasing");

  VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
  FSO::Files::Formats::IFF::IffFile iff;
  FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"animation-clock-contract",true);
  FSO::Files::Formats::IFF::Chunks::OBJD definition;
  FSO::Content::GameObject object;object.OBJ=&definition;object.Resource=&resource;
  VMAvatar actor(&object);actor.Thread=std::make_unique<Engine::VMThread>(&context,&actor,4096);
  context.Clock->Ticks=100;
  auto foreground=std::make_shared<VMAnimationState>(&asset,false);actor.StartTS1Animation(foreground);actor.Animations.push_back(foreground);
  auto carry=std::make_shared<VMAnimationState>(&asset,false);actor.StartTS1Animation(carry);actor.CarryAnimationState=carry;
  actor.AdvanceAnimations(false);
  check(foreground->EventQueue==std::vector<int16_t>{0}&&carry->EventQueue==std::vector<int16_t>{0},"headless phase-zero events target their own foreground and carry channels");
  actor.AdvanceAnimations(false);
  check(foreground->EventQueue.size()==1&&carry->EventQueue.size()==1,"repeated model clock does not duplicate events");
  context.Clock->Ticks=102;actor.AdvanceAnimations(false);
  check(foreground->EventQueue==std::vector<int16_t>({0,1})&&carry->EventQueue==std::vector<int16_t>({0,1}),"event boundary uses asset milliseconds independently of posing");
  check(foreground->TimePropertyLists.size()==3&&foreground->TS1EventCursor==2,"source property table remains intact and cursor records consumption");
  actor.TS1RawAnimationClock=true;context.Clock->Ticks=103;
  auto raw=std::make_shared<VMAnimationState>(&asset,false);actor.StartTS1Animation(raw);actor.Animations={raw};actor.CarryAnimationState.reset();
  actor.AdvanceAnimations(false);check(raw->CurrentFrame==0,"new clip samples the changed model clock at initialization");
  ++context.Clock->Ticks;actor.AdvanceAnimations(false);check(near(raw->TS1Phase,.042f),"raw operand flag changes model clock increment without frame-rate guessing");
  auto zeroWeight=std::make_shared<VMAnimationState>(&asset,false);actor.StartTS1Animation(zeroWeight);zeroWeight->Weight=0;actor.Animations={zeroWeight};
  ++context.Clock->Ticks;actor.AdvanceAnimations(false);
  check(near(zeroWeight->TS1Phase,.042f)&&zeroWeight->EventQueue.empty(),"zero-weight source channel advances time without emitting events");
  auto switchClock=std::make_shared<VMAnimationState>(&asset,false);actor.TS1RawAnimationClock=false;context.Clock->Ticks=100;actor.StartTS1Animation(switchClock);actor.Animations={switchClock};actor.AdvanceAnimations(false);
  actor.TS1RawAnimationClock=true;context.Clock->Ticks=101;actor.AdvanceAnimations(false);
  check(near(switchClock->TS1Phase,-.758f)&&switchClock->EventQueue==std::vector<int16_t>({0,1,2}),"source event-time reversal traverses the ending interval even without a phase wrap");
  FSO::Vitaboy::Animation unbound;unbound.Name="unbound";unbound.Duration=1000;unbound.Motions.resize(1);unbound.Motions[0].TimeProperties=motion.TimeProperties;
  auto noFrames=std::make_shared<VMAnimationState>(&unbound,false);actor.StartTS1Animation(noFrames);actor.Animations={noFrames};++context.Clock->Ticks;actor.AdvanceAnimations(false);
  check(noFrames->EventQueue.empty()&&noFrames->TS1Phase==0&&noFrames->TS1Timestamp==actor.TS1AnimationTimestamp(),"unbound source animation updates its timestamp but skips phase and events");
  auto extreme=std::make_shared<VMAnimationState>(&asset,false);actor.StartTS1Animation(extreme);extreme->TS1Phase=-3e38f;extreme->SyncTS1Frame();actor.Animations={extreme};
  ++context.Clock->Ticks;actor.AdvanceAnimations(false);
  check(std::isfinite(extreme->CurrentFrame)&&extreme->TS1PoseFrame()==0&&extreme->TS1EventTime()==INT32_MIN,"extreme finite saved phase cannot overflow pose or event integer conversion");
  VMAnimationState invalidRate(&asset,false);invalidRate.StartTS1Clock(0,3e38f);invalidRate.AdvanceTS1Clock(INT32_MAX);
  check(std::isfinite(invalidRate.TS1Phase)&&std::isfinite(invalidRate.CurrentFrame),"non-finite phase intermediate cannot enter runtime state");
  motion.TimeProperties[0].Items.push_back(event(2000,"99"));
  actor.TS1RawAnimationClock=false;context.Clock->Ticks=100;
  auto bounded=std::make_shared<VMAnimationState>(&asset,false);bounded->Loop=true;actor.StartTS1Animation(bounded);actor.Animations={bounded};actor.AdvanceAnimations(false);
  context.Clock->Ticks=120;actor.AdvanceAnimations(false);context.Clock->Ticks=121;actor.AdvanceAnimations(false);
  check(bounded->EventQueue==std::vector<int16_t>({0,1,2,0})&&bounded->TS1EventCursor==1,"loop cursor skips out-of-duration properties without dispatching them");
  context.Clock->Ticks=100;auto boundedReversal=std::make_shared<VMAnimationState>(&asset,false);actor.StartTS1Animation(boundedReversal);actor.Animations={boundedReversal};actor.AdvanceAnimations(false);
  actor.TS1RawAnimationClock=true;context.Clock->Ticks=101;actor.AdvanceAnimations(false);
  check(boundedReversal->EventQueue==std::vector<int16_t>({0,1,2}),"clock reversal skips out-of-duration ending properties");
  auto legacy=std::make_shared<VMAnimationState>(&asset,false);legacy->Speed=1.2f;actor.Animations={legacy};actor.AdvanceAnimations(false);
  check(!legacy->TS1Clock&&near(legacy->CurrentFrame,1.2f),"legacy clip keeps its original frame policy after a new clock flag");
  std::printf("PASS %d TS1 animation clock contracts\n",count);return 0;
 } catch(const std::exception& error) {std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}
}
