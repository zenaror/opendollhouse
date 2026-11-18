// SPDX-License-Identifier: MPL-2.0
// Source-derived synthetic contracts for the TS1 OP27 producer and real slot
// consumer. No installation, profile, world renderer, VM tick or game assets.
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
#include "ts1_relative_position_vectors.inc"

static int checks=0;
static void check(bool ok,const std::string& label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
static SLOTItem makeSlot(int location,int facing=-1,int flags=0) {
    VMGotoRelativePositionOperand operand;
    operand.Location=static_cast<VMGotoRelativeLocation>(location);
    operand.Direction=static_cast<VMGotoRelativeDirection>(facing);
    operand.Flags=static_cast<VMGotoRelativeFlags>(flags);
    return VMGotoRelativePosition::CreateSlot(operand);
}

// Observe the virtual footprint hook reached through the REAL PositionValid /
// GetAvatarPlace consumer. Keep its real footprint and placement result.
class ObservedAvatar final : public VMAvatar {
public:
    struct Observation { VMEntity::DirectionT DirectionArgument; float Radians; int Extension; std::array<int,4> Bounds; };
    std::vector<Observation> Placement;
    explicit ObservedAvatar(FSO::Content::GameObject* object) : VMAvatar(object) {}
    std::unique_ptr<Routing::VMObstacle> GetObstacle(const LotTilePos& p,VMEntity::DirectionT d) override {
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

static void literalCandidateContracts() {
    static_assert(sizeof(candidateCases)/sizeof(candidateCases[0])==80,"all heading/location cases required");
    for (const auto& c:candidateCases) {
        const auto slot=makeSlot(c.location);
        check(ValidTS1RelativeSlot(slot),"production OP27 descriptor enters original consumer");
        const auto got=TS1RelativeCandidates(slot,st::Point(72,72),unsigned(c.heading),0);
        const std::string label="heading="+std::to_string(c.heading)+" location="+std::to_string(c.location);
        check(got.size()==size_t(c.count),"ordered count "+label);
        for (int i=0;i<c.count;++i) {
            const auto& e=c.goals[i];const auto& g=got[size_t(i)];
            check(g.Position==st::Point(72+e.x,72+e.y) && g.Score==e.score &&
                g.Region==e.region && g.Heading==e.heading,"fixed source candidate "+label+" index="+std::to_string(i));
        }
    }
    const auto loc0=makeSlot(0),loc1=makeSlot(1);
    check(TS1RelativeCandidates(loc0,{72,72},0,1).empty(),"target north wall rejects north search edge");
    const auto near=TS1RelativeCandidates(loc0,{72,72},0,2);
    check(near.size()==1 && near[0].Score==110,"adjacent target wall removes literal25 bonus");
    const auto preferred=TS1RelativeCandidates(loc1,{72,72},0,0,{72,56});
    check(preferred.size()==2 && preferred[0].Score==155 && preferred[1].Score==135,"preferred point adds literal20 without sorting candidate order");
    check(TS1ObjectWallMask(0x100,0)==1 && TS1ObjectWallMask(int16_t(0x8000),1)==0,"signed/wide object wall mask fold");
}

static void randomContracts() {
    VMContext context(nullptr);VM vm(&context,nullptr);vm.TS1=true;
    for (const auto& c:randomCases) {
        context.RandomSeed=c.seed;
        check(context.NextRandom(UINT64_C(0x100000000))==c.result && context.RandomSeed==c.state,
            "actual VMContext carry-aware raw random seed="+std::to_string(c.seed));
    }
    context.RandomSeed=0xffffffffu;
    check(context.NextRandom(12)==UINT32_C(0x4e6cbe39)%12 && context.RandomSeed==UINT32_C(0xbe39e1cc),"unsigned caller modulus follows raw generator");
    const auto before=context.RandomSeed;
    check(context.NextRandom(0)==0 && context.RandomSeed==before,"native max0 API retains no-draw policy");
    vm.TS1=false;context.RandomSeed=1;
    check(context.NextRandom(UINT64_MAX)==UINT64_C(5180492295206395165) && context.RandomSeed==UINT64_C(33554433),"non-TS1 consumer retains legacy xorshift stream");
}

static void realConsumerContracts() {
    Fixture f;
    for (const auto& c:candidateCases) {
        auto slot=makeSlot(c.location);f.Target->Direction(Direction(1u<<c.heading));
        f.Actor->Placement.clear();const auto seed=f.Context.RandomSeed;
        const float radians=f.Actor->RadianDirection();
        const auto got=f.find(slot);
        const std::string label="real parser heading="+std::to_string(c.heading)+" location="+std::to_string(c.location);
        check(got.size()==size_t(c.count),label+" count");
        for (int i=0;i<c.count;++i) {
            const auto& e=c.goals[i];const auto& g=got[size_t(i)];
            check(g.Position==LotTilePos(int16_t(72+e.x),int16_t(72+e.y),1) &&
                g.Score==e.score && int(g.RouteEntryFlags)==e.region,label+" literal ordered result");
        }
        check(f.Context.RandomSeed==seed,label+" directed/uncapped branch consumes no RNG");
        check(f.Actor->RadianDirection()==radians && f.Actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0,
            label+" restores exact radian cache and clears temporary PD73");
        check(f.Actor->Placement.size()==got.size(),label+" each accepted result reaches real footprint consumer");
        for (const auto& p:f.Actor->Placement) check(p.Extension==8,label+" PD72 copied into PD73 during acceptance");
    }
    f.Target->Direction(Direction::NORTH);
    auto north=makeSlot(0);
    f.Context.EnvironmentMaps[0][4*8+4]=0x00020002u;
    check(f.find(north).empty(),"caller/target canonical room mismatch rejects despite same routing room");
    f.Actor->Thread=std::make_unique<VMThread>(&f.Context,f.Actor.get(),16);
    auto initRoute=[&](){
        VMRoutingFrame route;route.Caller=f.Actor.get();route.Thread=f.Actor->Thread.get();
        return route.InitRoutes(&north,f.Target.get());
    };
    const auto beforeRoute=f.Actor->Position();
    check(!initRoute() && f.Actor->Position()==beforeRoute && f.Actor->Thread->Stack.empty(),
        "relative request rejects canonical room mismatch before portal execution or candidate parsing");
    f.Context.Architecture()->Rooms[0].Map[4*8+4]=1;
    f.Actor->SetPersonData(static_cast<VMPersonDataVariable>(84),1);
    check(initRoute(),"raw PD84 bit0 preserves early exception without traversing target room portals");
    f.Actor->SetPersonData(static_cast<VMPersonDataVariable>(84),0);
    f.Actor->SetPersonData(VMPersonDataVariable::IsGhost,1);
    check(initRoute(),"ghost bit0 preserves original early room-gate exception");
    f.Actor->SetPersonData(VMPersonDataVariable::IsGhost,0);
    f.Context.Architecture()->Rooms[0].Map[4*8+4]=0;
    f.Context.EnvironmentMaps[0][4*8+4]=0x00010001u;
    f.Context.EnvironmentMaps[0][3*8+4]=0x00020002u;
    check(f.find(north).empty(),"destination canonical room mismatch rejects despite same routing room");
    f.Context.EnvironmentMaps[0][3*8+4]=0x00010001u;
    check(f.find(north).size()==1,"canonical room restoration admits destination");

    f.PackedHeights[(3*64+4)*4]=1;
    f.Context.Architecture()->Terrain->SetTS1HeightSource(f.PackedHeights);
    check(f.find(north).empty(),"actual parser rejects a nonflat packed destination H4");
    auto alternate=makeSlot(0,-1,4);
    check(f.find(alternate).size()==1,"operandbit4 bypasses packed H4 gate in actual parser");
    f.PackedHeights[(3*64+4)*4]=0;
    f.Context.Architecture()->Terrain->SetTS1HeightSource(f.PackedHeights);

    f.wall(4,4,2);check(f.find(north).empty(),"raw target TR bit2 blocks canonical north edge");f.wall(4,4,0);
    f.wall(4,3,8);check(f.find(north).empty(),"raw destination BL bit8 blocks opposite-facing acceptance");f.wall(4,3,0);
    f.WallObject->SetValue(VMStackObjectVariable::WallBlockFlags,1);
    f.WallObject->Direction(Direction::NORTH);
    f.Context.SpatialTileHeads[0][4*8+4]=f.WallObject->ObjectID;
    check(f.find(north).empty(),"canonical tile-head OD53 contributes target wall mask");
    f.Context.SpatialTileHeads[0][4*8+4]=0;
    check(f.find(north).size()==1,"non-head object is not broadened into wall traversal");

    f.Target->Direction(Direction::EAST);auto top=makeSlot(-2,-2);
    f.Actor->Placement.clear();const auto savedRadians=f.Actor->RadianDirection();
    const auto at=f.find(top);
    check(at.size()==1 && at[0].Position==f.Target->Position(),"OnTop FE exact target result");
    check(f.Actor->Placement.size()==1 && f.Actor->Placement[0].DirectionArgument==Direction::WEST,
        "OnTop FE target2 collision acceptance uses opposite heading6");
    check(at.size()==1 && at[0].RadianDirection==float(M_PI/2),"OnTop FE final same-point facing is target2");
    check(f.Actor->RadianDirection()==savedRadians,"OnTop temporary facing restores original exact radians");

    f.Target->Direction(Direction::NORTH);auto fallback=makeSlot(-1);
    f.Context.EnvironmentRooms[1].Environment.HalfTiles=501;f.Context.RandomSeed=3;
    check(f.find(fallback).size()==12 && f.Context.RandomSeed==3,"canonical501 half-visits retain48 cap and no random draws");
    f.Context.EnvironmentRooms[1].Environment.HalfTiles=502;f.Context.RandomSeed=3;
    const auto capped=f.find(fallback);const auto& full=candidateCases[1];
    check(capped.size()==sizeof(capIndices)/sizeof(capIndices[0]),"canonical502 half-visits trigger source8 attempts");
    for (size_t i=0;i<capped.size();++i) {
        const auto& e=full.goals[capIndices[i]];
        check(capped[i].Position==LotTilePos(int16_t(72+e.x),int16_t(72+e.y),1) && capped[i].Score==e.score,
            "real cap fixed random scan result "+std::to_string(i));
    }
    check(f.Context.RandomSeed==capFinalSeed,"real cap consumes exactly8 shared source draws");
}

static void scoringFootprintScopeContracts() {
    Fixture f;
    f.Actor->Thread=std::make_unique<VMThread>(&f.Context,f.Actor.get(),16);
    f.Actor->SetPersonData(VMPersonDataVariable::FootprintExtension,0x20);
    f.Actor->Footprint=f.Actor->GetObstacle(f.Actor->Position(),f.Actor->Direction());
    const auto* occupied=f.Actor->Footprint.get();
    const std::array<int,4> bounds={occupied->x1,occupied->y1,occupied->x2,occupied->y2};
    const auto position=f.Actor->Position();const float radians=f.Actor->RadianDirection();
    auto preserved=[&](const std::string& label) {
        check(f.Actor->Position()==position&&f.Actor->RadianDirection()==radians,label+" does not move or turn the actual actor");
        const auto* current=f.Actor->Footprint.get();
        check(current==occupied&&std::array<int,4>{current->x1,current->y1,current->x2,current->y2}==bounds,
            label+" leaves the occupied footprint cache unchanged");
    };
    SLOTItem authored;authored.Type=3;authored.Rsflags=SLOTFlags(255);
    authored.MinProximity=authored.MaxProximity=authored.OptimalProximity=16;
    authored.Standing=1;authored.Sitting=0;authored.Ground=0;authored.Height=5;
    authored.Facing=SLOTFacing::FaceTowardsObject;authored.SnapTargetSlot=-1;
    f.Actor->Placement.clear();const auto goals=f.find(authored);
    check(!goals.empty()&&!f.Actor->Placement.empty(),"authored SLOT reaches actual candidate placement checks");
    for(const auto& observation:f.Actor->Placement)
        check(observation.Extension==8,"authored candidate validation applies captured requested PD72");
    check(f.Actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0,
        "authored scoring clears PD73 instead of restoring its nonzero entry value");
    check(f.Actor->GetPersonData(VMPersonDataVariable::RouteFootprintMask)==8,
        "authored scoring preserves requested PD72");
    preserved("authored scoring");

    auto relative=makeSlot(0);f.Actor->SetPersonData(VMPersonDataVariable::FootprintExtension,0x40);
    f.Actor->Placement.clear();const auto generated=f.find(relative);
    check(!generated.empty(),"generated relative SLOT still validates candidates");
    for(const auto& observation:f.Actor->Placement)
        check(observation.Extension==8,"generated relative checks share requested-footprint scope");
    check(f.Actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0,
        "generated relative scoring also clears a nonzero prior PD73");
    preserved("relative scoring");

    f.Actor->SetPersonData(VMPersonDataVariable::FootprintExtension,0x40);
    f.Context.EnvironmentMaps[0][4*8+4]=0x00020002u;
    check(f.find(relative).empty()&&f.Actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0,
        "relative early room rejection leaves no temporary applied footprint");
    preserved("rejected scoring");

    f.Context.EnvironmentMaps[0][4*8+4]=0x00010001u;
    for(const auto& facing:std::array<std::array<int,3>,3>{{{{0,6,14}},{{2,14,6}},{{1,6,6}}}}) {
        authored.Facing=SLOTFacing(facing[0]);f.Actor->Placement.clear();
        check(!f.find(authored).empty(),"direction-sensitive authored candidates remain valid");
        for(const auto& observation:f.Actor->Placement) {
            check(observation.DirectionArgument==Direction(1u<<facing[0]),"authored candidate acceptance passes its selected heading");
            check(observation.Bounds[2]-observation.Bounds[0]==facing[1]&&
                observation.Bounds[3]-observation.Bounds[1]==facing[2],
                "actual candidate footprint rotates extension8 for north/east and omits it on diagonals");
        }
        preserved("direction-sensitive authored scoring");
    }
    const float freeRadians=float(M_PI/2)+.03125f;f.Actor->RadianDirection(freeRadians);
    authored.Facing=SLOTFacing::FaceAnywhere;f.Actor->Placement.clear();f.find(authored);
    check(!f.Actor->Placement.empty(),"FaceAnywhere candidate admission remains reachable");
    for(const auto& observation:f.Actor->Placement)
        check(observation.DirectionArgument==Direction::EAST&&observation.Radians==freeRadians,
            "FaceAnywhere validates using the untouched original actor heading");
    check(f.Actor->RadianDirection()==freeRadians,"FaceAnywhere preserves exact original radians on exit");
    f.Actor->RadianDirection(radians);

    f.Target->Direction(Direction::EAST);authored.Rsflags=SLOTFlags(255|512);authored.Facing=SLOTFacing(2);
    f.Actor->Placement.clear();f.find(authored);
    for(const auto& observation:f.Actor->Placement)
        check(observation.DirectionArgument==Direction::EAST,"absolute authored facing uses zero rotation base");
    authored.Rsflags=SLOTFlags(0);authored.Facing=SLOTFacing::FaceAnywhere;f.Actor->Placement.clear();
    check(!f.find(authored).empty(),"low-mask-zero authored point still validates");
    for(const auto& observation:f.Actor->Placement)
        check(observation.DirectionArgument==Direction::WEST,"low-mask-zero admission retains the source opposite-target heading exception");
    f.Target->Direction(Direction::NORTH);authored.Rsflags=SLOTFlags(255);authored.Facing=SLOTFacing::FaceTowardsObject;

    f.Machine.TS1=false;f.Actor->SetPersonData(VMPersonDataVariable::FootprintExtension,0x20);
    f.Actor->Placement.clear();f.find(authored);
    check(!f.Actor->Placement.empty(),"TSO authored placement remains reachable");
    for(const auto& observation:f.Actor->Placement)
        check(observation.Extension==0x20&&observation.DirectionArgument==Direction::NORTH,
            "TSO scoring keeps its preexisting applied footprint and north acceptance");
    check(f.Actor->GetPersonData(VMPersonDataVariable::FootprintExtension)==0x20,
        "TSO scoring does not inherit TS1 temporary-field cleanup");
    preserved("TSO scoring");
}

int main() {
    try {
        VM::UseWorld(false);
        literalCandidateContracts();randomContracts();realConsumerContracts();scoringFootprintScopeContracts();
        std::cout<<"PASS TS1 relative-position synthetic contracts: "<<checks<<" checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"FAIL TS1 relative-position synthetic contract after "<<checks<<" checks: "<<e.what()<<'\n';
        return 1;
    }
}
