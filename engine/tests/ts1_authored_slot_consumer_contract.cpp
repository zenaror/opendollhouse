// SPDX-License-Identifier: MPL-2.0
// Source-derived generic authored SLOT consumer contracts. Real VM placement,
// rooms, H4, tile-head lookup and RNG; no renderer, VM tick or game assets.
#include "simantics/engine/VMTS1RelativePosition.h"
#include "simantics/engine/VMSlotParser.h"
#include "simantics/engine/VMRoutingFrame.h"
#include "simantics/engine/VMThread.h"
#include "simantics/primitives/VMGotoRelativePosition.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/model/VMArchitectureTerrain.h"
#include "common/TS1/TS1Random.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Primitives;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using FSO::LotView::Model::LotTilePos;
using FSO::LotView::Model::Direction;


static int checks=0;
static void check(bool ok,const std::string& label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
// Observe the virtual footprint hook reached through the REAL PositionValid /
// GetAvatarPlace consumer. Keep its real footprint and placement result.
class ObservedAvatar final : public VMAvatar {
public:
    struct Observation { VMEntity::DirectionT DirectionArgument; float Radians; int Extension; std::array<int,4> Bounds; };
    std::vector<Observation> Placement;
    explicit ObservedAvatar(FSO::Content::GameObject* object) : VMAvatar(object) {}
    std::unique_ptr<FSO::SimAntics::Model::Routing::VMObstacle> GetObstacle(const LotTilePos& p,VMEntity::DirectionT d) override {
        auto result=VMAvatar::GetObstacle(p,d);
        Placement.push_back({d,RadianDirection(),GetPersonData(VMPersonDataVariable::FootprintExtension),
            {result->x1,result->y1,result->x2,result->y2}});
        return result;
    }
};

struct Fixture {
    VMContext Context{nullptr};
    VM Machine{&Context,nullptr};
    IffFile Iff;
    FSO::Content::GameObjectResource Resource{&Iff,nullptr,nullptr,"relative-contract",true};
    OBJD Definition;
    FSO::Content::GameObject Object;
    Entities::VMMultitileGroup ActorGroup,TargetGroup,WallObjectGroup;
    std::unique_ptr<ObservedAvatar> Actor;
    std::unique_ptr<VMGameObject> Target,WallObject;
    std::vector<uint8_t> PackedHeights=std::vector<uint8_t>(64u*64u*4u,0);
    static constexpr int Width=8,Center=72;
    Fixture() {
        Machine.TS1=true;
        Context.Architecture(std::make_unique<VMArchitecture>(Width,Width,nullptr,&Context));
        // The FreeSO routing map deliberately has a different ID. The source
        // contract consults the canonical room map for candidate admission/cap.
        Context.Architecture()->Rooms[0].Map.assign(Width*Width,0);
        Context.RoomInfo.resize(1);
        Context.EnvironmentMaps.resize(1);
        Context.EnvironmentMaps[0].assign(Width*Width,0x00010001u);
        Context.EnvironmentRooms.resize(3);
        Context.SpatialTileHeads[0].assign(Width*Width,0);
        check(Context.Architecture()->Terrain->SetTS1HeightSource(PackedHeights),"fixture installs packed H4 source");
        Definition.GUID=0x12345678;
        Object.OBJ=&Definition;Object.GUID=Definition.GUID;Object.Resource=&Resource;
        Actor=std::make_unique<ObservedAvatar>(&Object);
        Target=std::make_unique<VMGameObject>(&Object,nullptr);
        WallObject=std::make_unique<VMGameObject>(&Object,nullptr);
        Actor->MultitileGroup=&ActorGroup;ActorGroup.AddObject(Actor.get());
        Target->MultitileGroup=&TargetGroup;TargetGroup.AddObject(Target.get());
        WallObject->MultitileGroup=&WallObjectGroup;WallObjectGroup.AddObject(WallObject.get());
        Machine.AddEntity(Actor.get());Machine.AddEntity(Target.get());Machine.AddEntity(WallObject.get());
        Actor->Position(LotTilePos(24,24,1));Target->Position(LotTilePos(Center,Center,1));
        WallObject->Position(LotTilePos(Center,Center,1));
        Actor->SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
        Actor->SetPersonData(VMPersonDataVariable::RouteFootprintMask,8);
        Actor->RadianDirection(0.1234567f);
        Context.RandomSeed=3;
    }
    std::vector<VMFindLocationResult> find(SLOTItem& slot) {
        VMSlotParser parser(&slot);
        return parser.FindAvaliableLocations(Target.get(),&Context,Actor.get());
    }
    void wall(int x,int y,uint8_t bits) {
        Context.Architecture()->Walls[0][size_t(y)*Width+x].Segments=static_cast<FSO::LotView::Model::WallSegments>(bits);
    }
};

static SLOTItem authored(int flags=1,int minimum=16,int maximum=32) {
    SLOTItem slot;slot.Type=3;slot.Rsflags=SLOTFlags(flags);slot.Standing=1;
    slot.MinProximity=minimum;slot.MaxProximity=maximum;slot.OptimalProximity=minimum;
    slot.Resolution=16;slot.Gradient=.1875f;slot.Facing=SLOTFacing::FaceAnywhere;
    return slot;
}

static void pointAndMetadata() {
    Fixture f;auto slot=authored(0);slot.Offset=st::Vector3(.75f,1.25f,99.f);
    slot.MinProximity=slot.MaxProximity=slot.OptimalProximity=0;slot.Resolution=0;
    slot.Height=8;slot.SnapTargetSlot=1;
    const auto got=f.find(slot);
    check(got.size()==1 && got[0].Position==LotTilePos(72,73,1) && got[0].Score==100,
        "raw point branch truncates after center, ignores region domain and preserves caller level");
    f.Target->Direction(Direction::EAST);
    const auto east=f.find(slot);
    check(east.size()==1 && east[0].Position==LotTilePos(70,72,1),
        "cardinal2 truncates original localX before exchanging float axes");
    slot.Rsflags=SLOTFlags(0x200);
    const auto absolute=f.find(slot);
    check(absolute.size()==1 && absolute[0].Position==LotTilePos(72,73,1),
        "Absolute also affects source low-mask-zero point branch");
    slot=authored(1,0,32);
    check(f.find(slot).empty(),"authored region min0 is rejected without managed normalization");
    slot=authored(1|0x200,16,16);slot.Facing=SLOTFacing(2);f.Actor->Placement.clear();
    const auto faced=f.find(slot);
    check(faced.size()==1 && faced.front().RadianDirection==float(M_PI),
        "final facing uses actual target heading even when Absolute changes search orientation");
    check(!f.Actor->Placement.empty() && f.Actor->Placement.back().DirectionArgument==Direction::EAST,
        "temporary collision facing independently uses Absolute zero search base");
    slot=authored(0);slot.Facing=SLOTFacing::FaceAwayFromObject;
    const auto away=f.find(slot);
    check(away.size()==1 && away.front().RadianDirection==float(6*(M_PI/4)),
        "same-position point FaceAway adds opposite heading after target fallback");
}

static void admissionRows() {
    Fixture f;auto slot=authored(1,32,80);
    f.wall(2,2,8);
    const auto got=f.find(slot);
    check(got.size()==4,"wall status3 finishes every tangent of current radial row");
    for (size_t i=0;i<got.size();++i)
        check(got[i].Position==LotTilePos(int16_t(56+i*16),40,1),
            "wall status3 skips subsequent radial rows, retains source order");
    f.wall(2,2,0);
    f.Context.EnvironmentMaps[0][2*8+2]=0x00020002u;
    const auto differentRoom=f.find(slot);
    check(differentRoom.size()>got.size(),"room status2 does not terminate radial region");
    slot.Rsflags=SLOTFlags(1|0x800);
    const auto ignore=f.find(slot);
    check(!ignore.empty() && ignore.front().Position==LotTilePos(40,40,1),
        "IgnoreRooms admits distinct canonical room without target-wall filtering");
    f.Context.EnvironmentMaps[0][2*8+2]=0x00010001u;
    slot=authored(1,16,16);f.PackedHeights[(3*64+4)*4]=1;
    f.Context.Architecture()->Terrain->SetTS1HeightSource(f.PackedHeights);
    VMSlotParser altitude(&slot);
    check(altitude.FindAvaliableLocations(f.Target.get(),&f.Context,f.Actor.get()).empty() &&
        altitude.FailCode==FSO::SimAntics::Model::Routing::VMRouteFailCode::AltsDontMatch,"authored candidate enforces packed flat H4 gate and source failure12");
    slot.Rsflags=SLOTFlags(1|0x8000);
    check(f.find(slot).size()==1,"AllowDifferentAlts bypasses original H4 gate");
}

static void chairs() {
    Fixture f;auto slot=authored(1,16,32);slot.Standing=0;slot.Sitting=1;
    auto* chair=f.WallObject.get();chair->Position(LotTilePos(72,40,1));chair->Direction(Direction::SOUTH);
    f.Definition.ChairEntryFlags=2;chair->EntryPoints[26].ActionFunction=1;chair->Contained.resize(2);
    f.Context.SpatialTileHeads[0][2*8+4]=chair->ObjectID;
    const auto got=f.find(slot);
    check(got.size()==1 && got[0].Chair==chair && got[0].RouteEntryFlags==SLOTFlags::NORTH_EAST,
        "generic chair consumer finds independent neighbor1 through canonical tile head");
    check(got[0].Position==LotTilePos(56,56,1) && got[0].Score==132,
        "chair neighbor scores its radius but retains original cell destination");
    chair->Contained[1]=f.Target.get();
    VMSlotParser occupied(&slot);const auto blocked=occupied.FindAvaliableLocations(f.Target.get(),&f.Context,f.Actor.get());
    check(blocked.empty() && occupied.Blocker==f.Target.get() && occupied.FailCode==FSO::SimAntics::Model::Routing::VMRouteFailCode::DestChairOccupied,
        "occupied second chair slot rejects and records occupant with source failure9 priority");
    chair->Contained[1]=f.Actor.get();
    check(f.find(slot).size()==1,"caller already occupying chair slot remains eligible");
    chair->Contained[1]=nullptr;chair->SetValue(VMStackObjectVariable::Flags,16);
    check(f.find(slot).empty(),"tile-head object lookup excludes original flag16 objects");
    chair->SetValue(VMStackObjectVariable::Flags,6);
    check(f.find(slot).size()==1,"tile-head flags4+2 admit the original alternative object class");
    chair->SetValue(VMStackObjectVariable::Flags,0);f.Context.SpatialTileHeads[0][2*8+4]=0;
    check(f.find(slot).empty(),"coincident chair not in canonical tile chain is not invented by broad cache search");
    f.Context.SpatialTileHeads[0][2*8+4]=chair->ObjectID;
    slot.Standing=1;slot.Sitting=2;slot.Rsflags=SLOTFlags(0x55);slot.MaxProximity=48;
    auto all=f.find(slot);check(all.size()>8,"mixed chair ring exceeds canonical small-room cap");
    std::vector<VMFindLocationResult> expected;
    for (auto& entry:all) if (entry.Chair) { expected.push_back(entry);entry.Score=0; }
    const auto chairCount=expected.size();check(chairCount>0,"higher sitting weight probes chairs before fallback standing");
    uint64_t state=3;
    const size_t attempts=8>chairCount?8-chairCount:0;
    for (size_t draw=0;draw<attempts;++draw) {
        const size_t start=FSO::Common::TS1::RandomStep(state)%all.size();
        for (size_t i=start;i<all.size();++i) if (all[i].Score!=0) {
            expected.push_back(all[i]);all[i].Score=0;break;
        }
    }
    f.Context.EnvironmentRooms[1].Environment.HalfTiles=502;f.Context.RandomSeed=3;
    const auto reduced=f.find(slot);
    check(reduced.size()==expected.size() && f.Context.RandomSeed==state,
        "source cap retains all chairs first, draws only remaining slots and keeps forward holes");
    for (size_t i=0;i<reduced.size();++i)
        check(reduced[i].Position==expected[i].Position && reduced[i].Chair==expected[i].Chair && reduced[i].Score==expected[i].Score,
            "mixed chair reduction retains source order and associated entry identity");
}

static void groupAndPreferred() {
    Fixture f;auto point=authored(0x10000);
    f.TargetGroup.MultiTile=true;f.TargetGroup.Objects.push_back(f.WallObject.get());
    f.WallObject->Position(LotTilePos(73,74,1));
    const auto average=f.find(point);
    check(average.size()==1 && average[0].Position==LotTilePos(72,73,1),
        "source group center integer-divides summed parts before offset projection");
    f.WallObject->Position(LotTilePos(75,72,1));point.Facing=SLOTFacing::FaceTowardsObject;
    const auto towardTarget=f.find(point);
    check(towardTarget.size()==1 && towardTarget.front().Position==LotTilePos(73,72,1) &&
        towardTarget.front().RadianDirection==float(6*(M_PI/4)),
        "group-centered point still finishes facing actual target position");
    f.TargetGroup.MultiTile=false;f.TargetGroup.Objects.pop_back();
    auto* chair=f.WallObject.get();chair->Position(LotTilePos(72,40,1));chair->Direction(Direction::SOUTH);
    f.Definition.ChairEntryFlags=2;chair->EntryPoints[26].ActionFunction=1;
    f.Actor->Position(LotTilePos(72,40,1));f.Actor->Container=chair;
    auto slot=authored(1,16,32);const auto seated=f.find(slot);
    check(!seated.empty() && seated.front().Position==LotTilePos(56,56,1) && seated.front().Score==154,
        "seated preferred point uses entry-mask exit and adds20 to integer-local comparison");
    f.Actor->Container=nullptr;
    const auto standing=f.find(slot);
    check(!standing.empty() && standing.front().Score==134,
        "same source candidate without chair exit preference retains unbonused score");
}

static void scoresAndReduction() {
    Fixture f;auto slot=authored(0x55,16,32);
    const auto unseeded=f.find(slot);check(unseeded.size()==12,"authored broad standing ring keeps source12 order");
    const auto seed=f.Context.RandomSeed;
    check(seed==3,"directed uncapped authored generation consumes no random draws");
    slot.Rsflags=SLOTFlags(0x55|0x2000);f.Context.RandomSeed=3;
    auto expectedState=f.Context.RandomSeed;
    for (size_t i=0;i<unseeded.size();++i) FSO::Common::TS1::RandomStep(expectedState);
    const auto random=f.find(slot);
    check(random.size()==unseeded.size() && f.Context.RandomSeed==expectedState,
        "explicit score noise draws once per eligible probe before admission");
    slot.Rsflags=SLOTFlags(0x55);f.Context.EnvironmentRooms[1].Environment.HalfTiles=502;
    f.Context.RandomSeed=3;expectedState=3;
    auto all=unseeded;std::vector<VMFindLocationResult> expected;
    for (size_t n=0;n<8;++n) {
        const auto draw=FSO::Common::TS1::RandomStep(expectedState);
        for (size_t i=draw%all.size();i<all.size();++i) if (all[i].Score!=0) {
            expected.push_back(all[i]);all[i].Score=0;break;
        }
    }
    const auto capped=f.find(slot);
    check(capped.size()==expected.size() && f.Context.RandomSeed==expectedState,
        "actual authored reduction performs source8 bounded forward-only random scans");
    for (size_t i=0;i<capped.size();++i)
        check(capped[i].Position==expected[i].Position && capped[i].Score==expected[i].Score,
            "actual authored reduction preserves zero-score holes and draw order");
}

int main() {
    try { pointAndMetadata();admissionRows();chairs();groupAndPreferred();scoresAndReduction();
        std::cout << "TS1 authored SLOT consumer: " << checks << " checks passed\n";return 0;
    } catch (const std::exception& error) {
        std::cerr << "TS1 authored SLOT consumer failed: " << error.what() << "\n";return 1;
    }
}
