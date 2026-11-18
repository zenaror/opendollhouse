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
    bool Reject = false, BlockCorner = false;
    int BlockedMoves = 0;
    VMPlacementResult SetPosition(const LotTilePos& position, DirectionT direction, VMContext*) override {
        if (Reject) return VMPlacementResult(VMPlacementError::CantBeThroughWall);
        if (BlockCorner && GetObstacle(position,direction)->Intersects(
                Model::Routing::VMObstacle(388,452,396,460))) {
            ++BlockedMoves;
            return VMPlacementResult(VMPlacementError::CantIntersectOtherObjects);
        }
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
        Actor->RadianDirection(float(M_PI/2.0));
        Actor->WalkAnimations[20]="walk-fixture";
        Walk.Name="walk-fixture";Walk.Duration=1000;Walk.Distance=3;
        Walk.Motions.resize(1);Walk.Motions[0].FrameCount=30;
        auto clip=std::make_shared<VMAnimationState>(&Walk,false);
        Actor->StartTS1Animation(clip,1.4f);clip->Loop=true;Actor->Animations.push_back(clip);
        Route.Caller=Actor.get();Route.Thread=Actor->Thread.get();
        // Direct-step controls retain the ODHI15/16 motion adapter. The
        // shared post-script model is exercised by the application journey.
        Route.TS1SharedModelRouting=false;
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
    const auto pastCorner = TS1SegmentLookahead(8,0,0,0,10,0,8);
    const auto nextSegment = TS1SegmentLookahead(8,0,10,0,10,10,8);
    check(!pastCorner.Found && nextSegment.Found && nextSegment.X==10 && nextSegment.Y==4,
        "source projection advances before the corner and seeks half-radius into the next segment");
    const auto reverseSegment = TS1SegmentLookahead(3,0,10,0,0,0,4);
    check(reverseSegment.Found && reverseSegment.X==1 && reverseSegment.Y==0,
        "source projection samples the descending segment with integer truncation");
    const auto constrainedCorner=TS1ConstrainToSegment(387,451,385,449,397,445);
    check(constrainedCorner.X==386 && constrainedCorner.Y==449,
        "source integer proposal projects to the selected physical path segment");
    const auto endpoint=TS1ConstrainToSegment(30,40,10,10,20,20);
    check(endpoint.X==20 && endpoint.Y==20,
        "source segment projection clamps beyond an endpoint");
    struct TurnCase { float units; int code; float authored; };
    const TurnCase turns[] = {
        {-4,5,-4},{-3.01f,5,-4},{-3,7,-2},{-1.51f,7,-2},{-1.5f,9,-1},
        {-0.51f,9,-1},{-0.5f,10,0},{0.5f,10,0},{0.51f,8,1},{1.5f,8,1},
        {1.51f,6,2},{3,6,2},{3.01f,4,4},{4,5,-4},{8.75f,8,1}
    };
    for (const auto& input:turns) {
        const auto turn=TS1SelectTurn(input.units,1.4f,0);
        check(turn.Animate && turn.ClipCode==input.code && turn.AuthoredTurn==input.authored &&
            near(turn.Residual,turn.Difference-turn.AuthoredTurn),
            "source signed turn boundaries select authored clip and residual");
    }
    check(!TS1SelectTurn(0.17f,1.4f,0).Animate && TS1SelectTurn(0.18f,1.4f,0).Animate,
        "source in-place turn threshold depends on model rate");
    check(TS1SelectTurn(0.1f,1.4f,2).Animate,
        "pet in-place orientation uses its smaller source threshold");
    Fixture f;f.step();
    check(f.Actor->Animations.size()==1 && f.Actor->Animations.front()->TS1Clock,"actual TS1 route keeps one authored source-clock clip");
    check(near(f.Actor->TS1MotionX,24.9408f) && f.Actor->Position().x==25,"actual route rounds physical placement while retaining fractional source displacement");
    check(f.Route.MoveFrames==0,"source motion does not advance legacy integer timeline");
    f.step();check(near(f.Actor->TS1MotionX,25.8816f) && f.Actor->Position().x==26,"fraction survives rounded placement between ticks");
    f.Walk.Distance=6;f.step();check(near(f.Actor->TS1MotionX,27.7632f),"actual route reads active clip metadata each step");
    f.Actor->Position(LotTilePos(40,24,1));f.step();
    check(near(f.Actor->TS1MotionX,41.8816f),"external placement rebases fractional motion instead of teleporting back");
    f.Route.TS1SourceLocomotion=false;
    check(!f.Route.UsesTS1SourceLocomotion(),"legacy route explicitly retains prior motion policy");
    Fixture corner;
    corner.Actor->Position(LotTilePos(32,24,1));
    corner.Route.PreviousPosition=LotTilePos(24,24,1);
    corner.Route.CurrentWaypoint=LotTilePos(34,24,1);
    corner.Route.CurRoute->Position=LotTilePos(44,34,1);
    corner.Route.WalkTo=std::make_shared<std::list<st::Point>>(
        std::initializer_list<st::Point>{{34,34},{44,34}});
    corner.Route.Tick();
    check(corner.Route.PreviousPosition==LotTilePos(34,24,1) &&
        corner.Route.CurrentWaypoint==LotTilePos(34,34,1),
        "actual route advances the segment cursor before reaching the corner");
    check(corner.Actor->TS1MotionX>32 && corner.Actor->TS1MotionY>24 &&
        corner.Actor->TS1MotionX<34,
        "actual route follows a lookahead point rather than stopping at the bend");
    check(near(corner.Actor->RadianDirection(),float(M_PI/2.0+0.2*(M_PI/4.0))),
        "actual walking heading uses the source per-tick angular limit");

    check(corner.Actor->Position().x==34 && corner.Actor->TS1MotionX<34 &&
        corner.Actor->TS1MotionPublishedX==34,
        "physical path projection is separate from the curved model position");
    corner.Route.Tick();
    check(corner.Actor->TS1MotionX<34,
        "next tick retains unconstrained model position instead of rebasing it to projected placement");

    Fixture obstacleCorner;
    obstacleCorner.Actor->BlockCorner=true;
    obstacleCorner.Actor->Position(LotTilePos(385,451,1));
    obstacleCorner.Actor->TS1MotionPositionKnown=true;
    obstacleCorner.Actor->TS1MotionX=385.94635009765625f;
    obstacleCorner.Actor->TS1MotionY=451.78472900390625f;
    obstacleCorner.Actor->TS1MotionLevel=1;
    obstacleCorner.Actor->TS1MotionPublishedX=385;
    obstacleCorner.Actor->TS1MotionPublishedY=451;
    obstacleCorner.Actor->RadianDirection(0.92392063140869141f);
    obstacleCorner.Route.PreviousPosition=LotTilePos(385,449,1);
    obstacleCorner.Route.CurrentWaypoint=LotTilePos(397,445,1);
    obstacleCorner.Route.CurRoute->Position=LotTilePos(397,445,1);
    obstacleCorner.Route.Tick();
    check(obstacleCorner.Actor->BlockedMoves==0 && obstacleCorner.Actor->Position().y==449 &&
        obstacleCorner.Actor->TS1MotionY>451,
        "physical segment constraint avoids a corner obstacle without excluding it or enlarging planner padding");

    Fixture initialHeading;
    initialHeading.Actor->RadianDirection(float(M_PI/4.0));
    initialHeading.Route.TargetDirection=58.0*(M_PI/180.0);
    initialHeading.Route.BeginWalk();
    check(initialHeading.Route.State==VMRoutingFrameState::WALKING &&
        near(initialHeading.Actor->RadianDirection(),float(M_PI/4.0)),
        "path entry preserves heading and steers a small mismatch instead of starting an in-place turn");
    Fixture extreme;
    extreme.Walk.Duration=0.001f;
    extreme.Route.CurrentWaypoint=LotTilePos(34,24,1);
    extreme.Route.WalkTo=std::make_shared<std::list<st::Point>>(
        std::initializer_list<st::Point>{{34,34},{44,34}});
    extreme.Route.CurRoute->Position=LotTilePos(44,34,1);
    const auto safePosition=extreme.Actor->Position();
    extreme.Route.Tick();
    check(extreme.Actor->Position()==safePosition &&
        near(extreme.Actor->TS1MotionX,24) && near(extreme.Actor->TS1MotionY,24),
        "actual nonfinal lookahead rejects finite out-of-range displacement before integer conversion");

    Fixture turning;
    for(int code=4;code<=10;++code)
        turning.Actor->WalkAnimations[code]="source-turn-fixture-"+std::to_string(code);
    turning.Actor->RadianDirection(0);
    turning.Route.TargetDirection=0.75*(M_PI/4.0);
    check(turning.Route.Turn(turning.Route.TargetDirection),
        "actual source route starts authored turn below the inherited45degree threshold");
    auto clip=turning.Actor->CurrentAnimationState();
    check(clip && clip->Anim->Name=="source-turn-fixture-8" && clip->TS1Clock,
        "actual source route selects the45degree root-turn clip");
    turning.Route.State=VMRoutingFrameState::END_TURN;
    clip->TS1Phase=0.5f;
    turning.Route.Tick();
    check(near(turning.Actor->RadianDirection(),float(-0.125*(M_PI/4.0))),
        "source route samples residual from phase without adding the authored root rotation twice");
    turning.Route.Tick();
    check(near(turning.Actor->RadianDirection(),float(-0.125*(M_PI/4.0))),
        "same-phase route tick cannot accumulate a fixed angular tweak");
    turning.Route.State=VMRoutingFrameState::BEGIN_TURN;
    clip->TS1Phase=1;clip->EndReached=false;
    turning.Route.Tick();
    check(turning.Route.State==VMRoutingFrameState::WALKING &&
        near(turning.Actor->RadianDirection(),float(turning.Route.TargetDirection)),
        "source phase endpoint restores exact final orientation without renderer completion");
    Fixture legacyTurn;legacyTurn.Route.TS1SourceModelRouting=false;
    check(!legacyTurn.Route.Turn(0.75*(M_PI/4.0)),
        "pre16 source-clock route retains its inherited turn selection");
    std::cout << "TS1 locomotion contract: " << checks << " PASS\n"; return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n';return 1; } }
