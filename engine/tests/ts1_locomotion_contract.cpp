// SPDX-License-Identifier: MPL-2.0
// Source-derived numeric controls plus the real native route consumer. The
// synthetic room/clip do not prove source waypoint, portal or turn-FSM parity.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define private public
#include "simantics/engine/VMRoutingFrame.h"
#undef private
#include "simantics/engine/Routing/VMTS1Locomotion.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/engine/VMThread.h"
#include <iostream>
#include <limits>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Engine::Routing;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
static int checks = 0;
static void check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
static bool near(float a, float b) { return std::abs(a - b) < 0.00001f; }
class PlacementAvatar : public VMAvatar {
public:
    using VMAvatar::VMAvatar;
    bool Reject = false;
    VMPlacementResult SetPosition(const LotTilePos& position, DirectionT, VMContext*) override {
        if (Reject) return VMPlacementResult(VMPlacementError::CantBeThroughWall);
        Position(position); return VMPlacementResult(VMPlacementError::Success);
    }
};
struct Fixture {
    VMContext Context{nullptr}; VM Machine{&Context,nullptr};
    IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"motion-contract",true};
    OBJD Definition; FSO::Content::GameObject Object;
    std::unique_ptr<PlacementAvatar> Actor;
    FSO::Vitaboy::Animation Walk;
    VMRoutingFrame Route;
    Fixture() {
        Machine.TS1=true;
        Context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&Context));
        Context.Architecture()->Rooms[0].Map.assign(64,0); Context.RoomInfo.resize(1);
        Object.OBJ=&Definition;Object.GUID=0x12345678;Object.Resource=&Resource;
        Actor=std::make_unique<PlacementAvatar>(&Object);
        Actor->Thread=std::make_unique<VMThread>(&Context,Actor.get(),16);
        Actor->Position(LotTilePos(24,24,1));
        Actor->WalkAnimations[20]="walk-fixture";
        Walk.Name="walk-fixture";Walk.Duration=1000;Walk.Distance=3;
        Walk.Motions.resize(1);Walk.Motions[0].FrameCount=30;
        auto clip=std::make_shared<VMAnimationState>(&Walk,false);
        Actor->StartTS1Animation(clip,1.4f);clip->Loop=true;Actor->Animations.push_back(clip);
        Route.Caller=Actor.get();Route.Thread=Actor->Thread.get();
        Route.State=VMRoutingFrameState::WALKING;
        Route.WalkTo=std::make_shared<std::list<st::Point>>();
        Route.CurRoute=std::make_shared<VMFindLocationResult>();
        Route.CurRoute->Position=LotTilePos(72,24,1);Route.CurRoute->FaceAnywhere=true;
        Route.CurrentWaypoint=Route.CurRoute->Position;
        Route.PreviousPosition=Actor->Position();Route.MoveTotalFrames=480;
        Actor->TS1MotionRate=1.4f;
    }
    void step() { Route.Tick(); ++Context.Clock->Ticks; }
};
int main() { try {
    check(near(TS1TargetMotionRate(0,9,0),1.4f),"ordinary walking rate comes from model factor");
    check(near(TS1TargetMotionRate(1,9,0),2.8f),"human running state selects source hurry factor");
    check(near(TS1TargetMotionRate(1,10,0),1.4f),"state10 does not use state9 hurry factor");
    check(near(TS1TargetMotionRate(1,4,2),1.2f),"pet turn/run state source branch");
    check(near(TS1TargetMotionRate(1,0,2),2.8f),"scripted state0 branch precedes species restriction");
    check(near(TS1AdvanceMotionRate(1,2.8f),1.2f),"rate accelerates by source0.2");
    check(near(TS1AdvanceMotionRate(2.8f,1.4f),1.4f),"rate deceleration is immediate");
    check(near(TS1MotionStep(3,1000,1.4f),0.9408f),"metadata distance step uses nominal42ms");
    check(near(TS1MotionStep(6,1000,1.4f),1.8816f),"distance metadata changes travel speed");
    check(near(TS1MotionStep(3,2000,1.4f),0.4704f),"duration metadata changes travel speed");
    check(TS1MotionStep(3,0,1.4f)==0 && TS1MotionStep(3,1000,0)==0,"invalid denominator never creates nonfinite position");
    check(TS1MotionStep(std::numeric_limits<float>::max(),1,1)==0 &&
          TS1MotionStep(3,std::numeric_limits<float>::denorm_min(),
              std::numeric_limits<float>::max())==0,
        "finite extreme metadata cannot overflow into an instant waypoint jump");
    Fixture f;f.step();
    check(f.Actor->Animations.size()==1 && f.Actor->Animations.front()->TS1Clock,"actual TS1 route keeps one authored source-clock clip");
    check(near(f.Actor->TS1MotionX,24.9408f) && f.Actor->Position().x==24,"actual route retains fractional source displacement");
    check(f.Route.MoveFrames==0,"source motion does not advance legacy integer timeline");
    f.step();check(near(f.Actor->TS1MotionX,25.8816f) && f.Actor->Position().x==25,"fraction survives placement between ticks");
    f.Walk.Distance=6;f.step();check(near(f.Actor->TS1MotionX,27.7632f),"actual route reads active clip metadata each step");
    f.Actor->Position(LotTilePos(40,24,1));f.step();
    check(near(f.Actor->TS1MotionX,41.8816f),"external placement rebases fractional motion instead of teleporting back");
    f.Route.TS1SourceLocomotion=false;
    check(!f.Route.UsesTS1SourceLocomotion(),"legacy route explicitly retains prior motion policy");
    std::cout << "TS1 locomotion contract: " << checks << " PASS\n"; return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; } }
