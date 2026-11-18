// SPDX-License-Identifier: MPL-2.0
// DVD 597a90/597c25/597c54/59478f: resource bindings, admission and cursors.
#include "simantics/entities/VMAvatar.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "files/formats/iff/IffFile.h"
#include "vitaboy/SimAvatar.h"
#include "vitaboy/Skeleton.h"
#include <cstdio>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Model;
using namespace FSO::Vitaboy;
static int checks=0;
static void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);++checks;}
static TimePropertyListItem event(uint32_t time,const char* value) {
 TimePropertyListItem item;item.ID=time;PropertyListItem pairs;
 pairs.KeyPairs.emplace_back("xevt",value);item.Properties.Items.push_back(pairs);return item;
}
static void motion(Animation& asset,const char* bone,std::vector<TimePropertyListItem> events) {
 AnimationMotion m;m.BoneName=bone;m.FrameCount=30;
 m.TimeProperties.resize(1);m.TimeProperties[0].Items=std::move(events);asset.Motions.push_back(std::move(m));
}
int main() {try {
 VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
 FSO::Files::Formats::IFF::IffFile iff;
 FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"binding-events",true);
 FSO::Files::Formats::IFF::Chunks::OBJD definition;
 FSO::Content::GameObject object;object.OBJ=&definition;object.Resource=&resource;
 VMAvatar actor(&object);actor.Thread=std::make_unique<Engine::VMThread>(&context,&actor,4096);
 actor.Avatar=std::make_unique<SimAvatar>();
 for(const char* name:{"a","b"}) {auto bone=std::make_unique<Bone>();bone->Name=name;actor.Avatar->Skeleton->Bones.push_back(std::move(bone));}
 auto& fifo=actor.TS1AnimationController.Events;
 const auto start=[&](Animation& asset,bool backwards=false) {
  auto state=std::make_shared<VMAnimationState>(&asset,backwards);state->StartTS1Clock(0,1);
  actor.Animations={state};actor.TS1BackgroundAnimationState.reset();actor.CarryAnimationState.reset();fifo.clear();return state;
 };
 Animation ordered;ordered.Duration=1000;
 motion(ordered,"a",{event(200,"20")});motion(ordered,"b",{event(100,"10")});motion(ordered,"absent",{event(0,"99")});
 auto state=start(ordered);actor.DispatchTS1AnimationEvents(state.get(),0,200,false);
 check(fifo==std::vector<int32_t>({20,10}),"callbacks preserve resource order rather than globally sorting times");
 check(state->TS1BindingEventCursors==std::vector<int32_t>({1,1,0}),"unresolved bone never consumes its cursor");
 actor.DispatchTS1AnimationEvents(state.get(),200,200,false);check(fifo.size()==2,"same clock never duplicates consumed events");
 Animation claiming;claiming.Duration=1000;motion(claiming,"a",{});
 auto carry=std::make_shared<VMAnimationState>(&claiming,false);carry->StartTS1Clock(0,1);carry->TS1CompositionPriority=10;carry->Weight=0;
 state=start(ordered);actor.CarryAnimationState=carry;
 actor.DispatchTS1AnimationEvents(state.get(),0,200,false);check(fifo==std::vector<int32_t>({20,10}),"default false claim does not mask even with higher priority");
 state->StartTS1Clock(0,1);fifo.clear();carry->TS1ClaimsBonePriority=true;
 actor.DispatchTS1AnimationEvents(state.get(),0,200,false);
 check(fifo==std::vector<int32_t>({10})&&state->TS1BindingEventCursors[0]==0,"zero-weight claimant suppresses only its bound bone without consuming cursor");
 carry->TS1ClaimsBonePriority=false;actor.DispatchTS1AnimationEvents(state.get(),200,200,false);
 check(fifo==std::vector<int32_t>({10,20}),"unmasking resumes retained cursor");
 state=start(ordered);state->Weight=0;actor.DispatchTS1AnimationEvents(state.get(),0,200,false);
 check(fifo.empty()&&state->TS1BindingEventCursors[0]==0,"zero-weight emitter leaves binding cursors untouched");
 state->Weight=.001f;actor.DispatchTS1AnimationEvents(state.get(),0,200,false);
 check(fifo==std::vector<int32_t>({20,10}),"positive tiny weight emits without pose translation or rotation channels");
 Animation bounds;bounds.Duration=1000;
 motion(bounds,"a",{event(UINT32_MAX,"91"),event(2000,"92"),event(0,"0"),event(500,"5"),event(1000,"10")});
 state=start(bounds);actor.DispatchTS1AnimationEvents(state.get(),0,0,false);
 check(fifo==std::vector<int32_t>({0})&&state->TS1BindingEventCursors[0]==3,"invalid signed and out-of-duration timestamps advance immediately without callbacks");
 actor.DispatchTS1AnimationEvents(state.get(),0,600,false);actor.DispatchTS1AnimationEvents(state.get(),600,100,false);
 check(fifo==std::vector<int32_t>({0,5,10,0})&&state->TS1BindingEventCursors[0]==3,"forward wrap drains tail then resets and visits new head");
 Animation reverse;reverse.Duration=1000;motion(reverse,"a",{event(0,"0"),event(500,"5"),event(1000,"10")});
 state=start(reverse,true);check(state->TS1BindingEventCursors[0]==0,"backwards creation starts cursor at zero");
 actor.DispatchTS1AnimationEvents(state.get(),0,1000,false);
 check(fifo==std::vector<int32_t>({10})&&state->TS1BindingEventCursors[0]==1,"initial reverse wrap seeks last without replaying head");
 actor.DispatchTS1AnimationEvents(state.get(),1000,500,false);actor.DispatchTS1AnimationEvents(state.get(),500,0,false);
 check(fifo==std::vector<int32_t>({10,5,0})&&state->TS1BindingEventCursors[0]==-1,"backwards inclusive boundaries exhaust to minus one");
 actor.DispatchTS1AnimationEvents(state.get(),0,900,false);
 check(fifo==std::vector<int32_t>({10,5,0,10})&&state->TS1BindingEventCursors[0]==1,"reverse wrap from exhausted sentinel restarts at last");
 state=start(reverse,true);state->TS1BindingEventCursors[0]=2;
 actor.DispatchTS1AnimationEvents(state.get(),100,900,false);
 check(fifo==std::vector<int32_t>({5,0,10}),"reverse wrap decrements before emitting the ending interval");
 Animation grouped;grouped.Duration=1000;motion(grouped,"a",{event(50,"1")});
 grouped.Motions[0].TimeProperties.push_back(TimePropertyList{});
 auto pair=event(50,"2");pair.Properties.Items[0].KeyPairs.emplace_back("xevt","70000");
 grouped.Motions[0].TimeProperties[1].Items.push_back(pair);
 state=start(grouped);actor.DispatchTS1AnimationEvents(state.get(),0,50,false);
 check(fifo==std::vector<int32_t>({1,2,70000}),"group order and all signed model event key pairs survive without int16 narrowing");
 // Exercise the real clock-to-binding integration, not just direct dispatch.
 context.Clock->Ticks=200;state=start(reverse,true);actor.StartTS1Animation(state);
 actor.AdvanceAnimations(false);
 check(fifo==std::vector<int32_t>({10})&&state->TS1BindingPreviousEventTime==1000,
       "real backwards first update uses independent previous time zero");
 state=start(ordered);actor.StartTS1Animation(state);carry->TS1ClaimsBonePriority=true;
 actor.CarryAnimationState=carry;context.Clock->Ticks=220;actor.AdvanceAnimations(false);
 check(state->EndReached&&state->Speed==0&&state->TS1Phase==1&&fifo==std::vector<int32_t>({10}),
       "nonloop terminal freezes source speed while masked binding retains its events");
 carry->TS1ClaimsBonePriority=false;actor.AdvanceAnimations(false);
 check(fifo==std::vector<int32_t>({10,20}),"ended unmasked instance still dispatches pending binding callbacks");
 const float terminalPhase=state->TS1Phase;context.Clock->Ticks=219;actor.AdvanceAnimations(false);
 check(state->TS1Phase==terminalPhase&&state->Speed==0,"terminal instance does not resurrect on a backwards model clock");
 Animation missing;missing.Duration=1000;motion(missing,"missing-only",{event(0,"999")});
 state=start(missing);actor.StartTS1Animation(state);context.Clock->Ticks=230;actor.AdvanceAnimations(false);
 check(state->TS1Phase==0&&state->TS1BindingPreviousEventTime==0&&fifo.empty()&&
       state->TS1Timestamp==actor.TS1AnimationTimestamp(),"no resolved binding samples clock but skips phase and event history");
 Animation fg,bg,held;fg.Duration=bg.Duration=held.Duration=1000;
 motion(fg,"a",{event(0,"1002")});motion(bg,"a",{event(0,"1001")});motion(held,"a",{event(0,"1003")});
 state=start(fg);actor.StartTS1Animation(state);
 actor.TS1BackgroundAnimationState=std::make_shared<VMAnimationState>(&bg,false);
 actor.CarryAnimationState=std::make_shared<VMAnimationState>(&held,false);
 actor.StartTS1Animation(actor.TS1BackgroundAnimationState);actor.StartTS1Animation(actor.CarryAnimationState);
 actor.TS1BackgroundAnimationState->TS1CompositionPriority=30;
 actor.CarryAnimationState->TS1CompositionPriority=-20;
 actor.AdvanceAnimations(false);
 check(fifo==std::vector<int32_t>({1003,1002,1001}),"real update honors stable sorted persisted priorities beyond the ordinary channel defaults");
 std::printf("PASS %d original per-binding animation event contracts\n",checks);return 0;
 } catch(const std::exception& e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
