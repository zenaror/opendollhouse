// SPDX-License-Identifier: MPL-2.0
// DVD4338a5->476f33 and481593: numeric rectangle wall admission, independent
// of the SLOT center-wall test and registered occupancy.
#include "simantics/engine/TS1FootprintWall.h"
#include "simantics/engine/VMThread.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/VMArchitecture.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMGameObject.h"
#include "simantics/entities/VMMultitileGroup.h"
#include "simantics/primitives/VMExpression.h"
#include "files/formats/iff/chunks/BHAV.h"
#include <iostream>
#include <stdexcept>
using namespace FSO::SimAntics;
using namespace FSO::SimAntics::Engine;
using namespace FSO::SimAntics::Model;
using namespace FSO::Files::Formats::IFF;
using namespace FSO::Files::Formats::IFF::Chunks;
using namespace FSO::LotView::Model;
static void Need(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
static void Lines() {
    std::array<uint16_t,6> styles{1,1,1,1,1,1};
    const auto hit=[&](std::array<int32_t,4> b,int x,int y,uint8_t mask) {
        return TS1FootprintCrossesWall(b,x,y,mask,styles);
    };
    // Actual first divergent DVD probe234893: center423,494, numeric half3.
    Need(hit({420,491,426,497},26,31,2),"captured source horizontal crossing lost");
    Need(!hit({420,490,426,496},26,31,2),"edge contact became a crossing");
    Need(hit({29,20,35,26},2,1,1),"vertical crossing lost");
    Need(!hit({32,20,38,26},2,1,1),"vertical edge contact rejected");
    Need(hit({20,20,28,28},1,1,16),"descending diagonal crossing lost");
    Need(!hit({16,16,24,24},1,1,16),"diagonal edge contact rejected");
    Need(hit({20,20,28,28},1,1,32),"ascending diagonal crossing lost");
    Need(!hit({20,20,28,28},1,1,4|8),"neighbor-owned sides were tested twice");
    Need(!hit({20,20,28,28},1,1,1|32),"source subsequent-bit32 sentinel changed");
    //476f33 does not normalize literal endpoints or discard empty rectangles
    //before its tile loops. Its line predicate is strict in either direction.
    Need(hit({35,20,29,26},2,1,1),"inverted numeric endpoint order normalized");
    Need(hit({24,29,24,35},1,2,2),"zero-width numeric rectangle discarded categorically");
    for (auto style : {3,5,6,15,23}) {
        styles.fill(uint16_t(style));
        Need(!hit({20,20,28,28},1,1,16|32),"source nonblocking logical style rejected");
    }
}
static void Placement() {
    VMContext context(nullptr); VM machine(&context,nullptr); machine.TS1=true;
    context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
    context.Architecture()->Rooms[0].Map.assign(64,0);context.RoomInfo.resize(1);
    context.RebuildEnvironmentGeometry();
    IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"wall-contract",true);
    OBJD definition; definition.GUID=0x12345678;
    FSO::Content::GameObject object;
    object.OBJ=&definition;object.GUID=definition.GUID;object.Resource=&resource;
    Entities::VMMultitileGroup group; VMAvatar actor(&object);
    actor.MultitileGroup=&group;group.AddObject(&actor);machine.AddEntity(&actor);
    actor.Position(LotTilePos(24,24,1));
    actor.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
    auto& wall=context.Architecture()->Walls[0][2*8+1];
    wall.Segments=WallSegments::TopRight;wall.TopRightStyle=1;
    const auto before=context.TS1SimulationRandomSeed;
    const auto query=[&](int y) { return actor.PositionValid(LotTilePos(24,int16_t(y),1),Direction::NORTH,&context,true).Status; };
    Need(query(30)==VMPlacementError::CantBeThroughWall,"real avatar placement ignored footprint crossing beyond center tile");
    Need(query(29)==VMPlacementError::Success,"touching edge rejected in real placement");
    Need(actor.Position()==LotTilePos(24,24,1) && context.TS1SimulationRandomSeed==before,
        "placement query moved actor or consumed simulation RNG");
    actor.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain)|0x400);
    Need(query(30)==VMPlacementError::Success,"source InWall bypass ignored");
    actor.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
    for (int style : {3,5,6,15,23}) {
        context.Architecture()->TS1WallStyleTopRight[0].assign(64,0);
        context.Architecture()->TS1WallStyleTopRight[0][2*8+1]=uint16_t(style);
        Need(query(30)==VMPlacementError::Success,"logical authored wall-style cache ignored");
    }
    context.Architecture()->TS1WallStyleTopRight[0][2*8+1]=1;
    Need(query(30)==VMPlacementError::CantBeThroughWall,"blocking logical style lost");
    definition.TileWidth=6;
    Entities::VMMultitileGroup propGroup; VMGameObject prop(&object,nullptr);
    prop.MultitileGroup=&propGroup;propGroup.AddObject(&prop);machine.AddEntity(&prop);
    prop.SetValue(VMStackObjectVariable::AllowedHeightFlags,1);
    prop.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
    Need(context.GetObjPlace(&prop,LotTilePos(24,30,1),Direction::NORTH).Status==VMPlacementError::CantBeThroughWall,
        "ordinary numeric object placement lost the shared wall gate");
    prop.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain)|0x400);
    Need(context.GetObjPlace(&prop,LotTilePos(24,30,1),Direction::NORTH).Status==VMPlacementError::Success,
        "ordinary object's source InWall bypass ignored");
    prop.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
    prop.Position(LotTilePos(24,24,1));
    prop.SetFlag(VMEntityFlags::HasZeroExtent,true);
    Need(!prop.GetObstacle(LotTilePos(24,30,1),Direction::NORTH) && !prop.Footprint,
        "zero-extent fixture unexpectedly owns physical occupancy");
    Need(prop.GetTS1NumericFootprint(LotTilePos(24,30,1),Direction::NORTH)==std::array<int32_t,4>{21,27,27,33},
        "zero-extent fixture lost its independent source numeric rectangle");
    Need(prop.PositionValid(LotTilePos(24,30,1),Direction::NORTH,&context).Status==VMPlacementError::CantBeThroughWall,
        "actual zero-extent placement bypassed source numeric wall admission");
    Need(prop.PositionValid(LotTilePos(24,29,1),Direction::NORTH,&context).Status==VMPlacementError::Success,
        "zero-extent numeric rectangle touching wall edge was rejected");
    prop.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain)|0x400);
    Need(prop.PositionValid(LotTilePos(24,30,1),Direction::NORTH,&context).Status==VMPlacementError::Success,
        "zero-extent source InWall bypass ignored");
    machine.TS1=false;
    Need(query(30)==VMPlacementError::Success,"TSO placement changed");
    prop.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
    Need(prop.PositionValid(LotTilePos(24,30,1),Direction::NORTH,&context).Status==VMPlacementError::Success,
        "TSO zero-extent placement changed");
}
static void CrossRoomObjectPrecedence() {
    using Scope=FSO::SimAntics::Engine::Scopes::VMVariableScope;
    using namespace FSO::SimAntics::Primitives;
    VMContext context(nullptr); VM machine(&context,nullptr);machine.TS1=true;
    context.Architecture(std::make_unique<VMArchitecture>(8,8,nullptr,&context));
    context.Architecture()->Rooms[0].Map.assign(64,0);
    context.Architecture()->Rooms[0].Map[2*8+3]=1;context.RoomInfo.resize(2);
    context.RebuildEnvironmentGeometry();
    IffFile iff;
    FSO::Content::GameObjectResource resource(&iff,nullptr,nullptr,"placement-callback",true);
    std::array<OBJD,3> definitions;
    std::array<FSO::Content::GameObject,3> objects;
    std::array<Entities::VMMultitileGroup,3> groups;
    for (size_t k=0;k<3;++k) {
        definitions[k].GUID=0xa0501000+uint32_t(k);
        definitions[k].ObjectType=k==0?OBJDType::Person:OBJDType::Normal;
        definitions[k].TileWidth=k==2?32:8;
        definitions[k].NumAttributes=3;
        objects[k].OBJ=&definitions[k];objects[k].GUID=definitions[k].GUID;objects[k].Resource=&resource;
    }
    VMAvatar actor(&objects[0]);VMGameObject blocker(&objects[1],nullptr),north(&objects[2],nullptr);
    VMEntity* entities[]={&actor,&blocker,&north};
    for (size_t k=0;k<3;++k) {
        auto& e=*entities[k];e.MultitileGroup=&groups[k];groups[k].AddObject(&e);
        machine.AddEntity(&e);e.Thread=std::make_unique<VMThread>(&context,&e,16);
        e.Direction(Direction::NORTH);
        e.SetValue(VMStackObjectVariable::PlacementFlags,int16_t(VMPlacementFlags::OnTerrain));
        for (int a=0;a<5;++a)e.Thread->TempRegisters[a]=int16_t(100+int(k)*100+a);
    }
    const auto position=[&](VMEntity& e,int x,int y) {
        e.PrePositionChange(&context);e.Position(LotTilePos(int16_t(x),int16_t(y),1));e.PositionChange(&context,true);
    };
    position(actor,40,40);position(blocker,52,40);
    blocker.SetFlag(VMEntityFlags::HasZeroExtent,true);
    blocker.SetFlag(VMEntityFlags::DisallowPersonIntersection,true);
    auto& wall=context.Architecture()->Walls[0][2*8+3];wall.Segments=WallSegments::TopLeft;wall.TopLeftStyle=1;
    Need(context.GetRoomAt(actor.Position())!=context.GetRoomAt(blocker.Position()) &&
        context.SpatialTileHeads[0][2*8+3]==blocker.ObjectID && !blocker.Footprint,
        "cross-room fixture did not register a physical-null numeric blocker");
    const auto expression=[](int attribute,Scope rhs,int value,VMExpressionOperator op) {
        BHAVInstruction i;i.Opcode=2;VMExpressionOperand p;
        p.LhsOwner=Scope::MyObjectAttributes;p.LhsData=int16_t(attribute);
        p.RhsOwner=rhs;p.RhsData=int16_t(value);p.Operator=op;p.Write(i.Operand.data());return i;
    };
    const auto callback=[&](VMEntity& e,int id,bool allow) {
        auto* b=new BHAV();b->ChunkID=uint16_t(id);b->ChunkProcessed=true;b->Args=5;
        b->Instructions={expression(0,Scope::Literal,1,VMExpressionOperator::PlusEquals),
            expression(1,Scope::StackObjectID,0,VMExpressionOperator::Assign),
            expression(2,Scope::Parameters,4,VMExpressionOperator::Assign)};
        for (size_t k=0;k<b->Instructions.size();++k) {
            b->Instructions[k].TruePointer=k+1==b->Instructions.size()?(allow?254:255):uint8_t(k+1);
            b->Instructions[k].FalsePointer=255;
        }
        iff.AddChunk(b);e.EntryPoints[5].ActionFunction=uint16_t(id);
    };
    callback(actor,4096,false);callback(blocker,4097,false);
    std::vector<int16_t> order;
    machine.OnTraceInstruction=[&](VMStackFrame* f,uint16_t,int) {
        if (f && f->Routine && f->InstructionPointer==0 && f->Routine->ID>=4096 && f->Routine->ID<=4099)
            order.push_back(f->Caller->ObjectID);
    };
    const auto seed=context.TS1SimulationRandomSeed;
    const auto query=[&] { return actor.PositionValid(LotTilePos(47,40,1),Direction::NORTH,&context,true); };
    auto result=query();
    Need(result.Status==VMPlacementError::CantIntersectOtherObjects && result.Object==&blocker,
        "neighboring-room numeric blocker did not precede footprint wall rejection");
    Need(order==std::vector<int16_t>{actor.ObjectID,blocker.ObjectID} &&
        actor.GetAttribute(0)==1 && blocker.GetAttribute(0)==1 &&
        actor.GetAttribute(1)==blocker.ObjectID && blocker.GetAttribute(1)==actor.ObjectID &&
        actor.GetAttribute(2)==104 && blocker.GetAttribute(2)==204,
        "entry5 order/identity/NULL temporary-bank arguments differ");
    callback(blocker,4098,true);order.clear();result=query();
    Need(result.Status==VMPlacementError::CantBeThroughWall &&
        order==std::vector<int16_t>{actor.ObjectID,blocker.ObjectID},
        "authored intersection permission suppressed the later independent wall gate");
    blocker.EntryPoints[5].ActionFunction=4097; //reuse the already-authored FALSE tree
    callback(north,4099,false);position(north,40,24);order.clear();
    result=query();
    Need(result.Status==VMPlacementError::CantIntersectOtherObjects && result.Object==&north &&
        order==std::vector<int16_t>{actor.ObjectID,north.ObjectID},
        "source N-before-E tile traversal or first-blocker return changed");
    Need(context.TS1SimulationRandomSeed==seed && actor.Position()==LotTilePos(40,40,1),
        "placement callbacks consumed RNG or published candidate position");

    OBJD peerDefinition=definitions[0];peerDefinition.GUID=0xa0501003;
    FSO::Content::GameObject peerObject;
    peerObject.OBJ=&peerDefinition;peerObject.GUID=peerDefinition.GUID;peerObject.Resource=&resource;
    Entities::VMMultitileGroup peerGroup;VMAvatar peer(&peerObject);
    peer.MultitileGroup=&peerGroup;peerGroup.AddObject(&peer);machine.AddEntity(&peer);
    peer.Thread=std::make_unique<VMThread>(&context,&peer,16);
    peer.SetFlag(VMEntityFlags::AllowPersonIntersection,true);callback(peer,4100,false);position(peer,47,40);
    actor.TS1AnimationController.OriginalLifecycle=true; //carry setup uses source model, no renderer resource lookup
    north.SetFlag(VMEntityFlags::AllowPersonIntersection,true);
    // Source0 is the tile successor, not physical hand0 (source1).
    position(north,40,40);position(actor,40,40);
    Need(context.TS1SourceSlot(&actor,0)==&north && !actor.GetSlot(0),
        "fixture could not install an independent source0 tile successor");
    actor.SetPersonData(static_cast<VMPersonDataVariable>(84),1);
    auto count=actor.GetAttribute(0);
    result=query();
    Need(result.Status==VMPlacementError::Success && actor.GetAttribute(0)==count,
        "uncontained special person incorrectly treated own slot0 child as its parent");
    Need(actor.PlaceInSlot(&north,0,true,&context),"fixture could not occupy source1/physical0");
    result=query();
    Need(result.Status==VMPlacementError::Success && actor.GetAttribute(0)==count+1 && peer.GetAttribute(0)==0,
        "occupied special person bypassed checks or treated a walkable person as blocking");
    actor.ClearSlot(0);
    auto supported=context.TS1CaptureHierarchy();
    supported.Records.at(blocker.ObjectID).Node.Slots.resize(2,0);
    context.TS1PublishHierarchy(std::move(supported),true);
    Need(blocker.PlaceInSlot(&actor,0,true,&context),"fixture could not install special person ancestry");
    actor.SetValue(VMStackObjectVariable::SlotHierNumber,0);
    blocker.SetValue(VMStackObjectVariable::SlotHierNumber,-1);
    count=actor.GetAttribute(0);const auto containedPosition=actor.Position();
    result=query();
    Need(result.Status==VMPlacementError::Success && actor.GetAttribute(0)==count,
        "zero SlotHierNumber did not collapse to the root with no placement parent");
    actor.SetValue(VMStackObjectVariable::SlotHierNumber,1);
    result=query();
    Need(result.Status==VMPlacementError::Success && actor.GetAttribute(0)==count+1 &&
        actor.ContainerSlot==0 && actor.Position()==containedPosition && context.TS1SimulationRandomSeed==seed,
        "raw SlotHierNumber was confused with native ContainerSlot or query changed state");
    blocker.ClearSlot(0);
}
int main() { try { Lines();Placement();CrossRoomObjectPrecedence();std::cout<<"TS1 footprint wall contract passed\n";return 0; }
    catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
