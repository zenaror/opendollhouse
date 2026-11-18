// SPDX-License-Identifier: MPL-2.0
#include "simantics/engine/Routing/VMTS1RouteWorld.h"
#include "simantics/engine/Routing/VMTS1RouteScene.h"
#include "simantics/engine/VMTS1RelativePosition.h"
#include "simantics/engine/VMThread.h"
#include "simantics/engine/VMTS1Neighbourhood.h"
#include "simantics/VM.h"
#include "simantics/VMContext.h"
#include "simantics/entities/VMEntity.h"
#include "simantics/entities/VMAvatar.h"
#include "simantics/entities/VMMultitileGroup.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {
using Model::VMStackObjectVariable;
using Files::Formats::IFF::Chunks::OBJDType;
namespace {
unsigned Heading(VMEntity& entity) {
    const auto direction=unsigned(entity.Direction());
    for (unsigned i=0;i<8;++i) if (direction==(1u<<i)) return i;
    throw std::runtime_error("Original routing requires an eight-way object direction");
}
TS1RouteRect NumericBounds(const VMEntity& entity) {
    if (!entity.TS1NumericFootprintKnown)
        throw std::runtime_error("Original routing lacks numeric object bounds");
    const auto& bounds=entity.TS1NumericFootprint;
    return {bounds[0],bounds[1],bounds[2],bounds[3],0};
}
}
std::vector<TS1RouteRect> TS1CollectRouteObjectObstacles(VMEntity& caller, VMEntity* ignoredBlocker) {
    auto* context=caller.Thread ? caller.Thread->Context : nullptr;
    if (!context || !context->VM || !context->VM->TS1)
        throw std::runtime_error("Original routing lacks a live scene");
    auto& vm=*context->VM;
    if (!vm.TS1ObjectManagerOrderKnown)
        throw std::runtime_error("Original routing lacks object-manager order");
    const auto rawRoom=caller.GetValue(VMStackObjectVariable::Room);
    if (rawRoom<0) return {};
    const auto room=context->GetEnvironmentRoomAt(caller.Position());
    // 48265b captures the source rectangle once, before any check trees run.
    // Native placement occupancy may differ or be absent while seated.
    const auto callerBounds=NumericBounds(caller);
    std::vector<TS1RouteRect> result;
    const auto collect=[&](VMEntity* object) {
        if (!object || object==&caller || object->Dead || object->GhostImage ||
            !object->Object || !object->Object->OBJ ||
            object->GetValue(VMStackObjectVariable::Room)<0 ||
            context->GetEnvironmentRoomAt(object->Position())!=room) return;
        if (object->Container) return; // 43270d resolves the root attachment.
        const auto flags=uint16_t(object->GetValue(VMStackObjectVariable::Flags));
        const auto penalty=object->GetValue(VMStackObjectVariable::RoutePenalty);
        const bool walkable=(flags&4) ? !(flags&2) : bool(flags&16);
        if (walkable && penalty==0) return;
        const auto admittedType=object->Object->OBJ->ObjectType;
        if (admittedType==OBJDType::Person) {
            if (!dynamic_cast<VMAvatar*>(object) || object==ignoredBlocker) return;
            const auto a=caller.Position(),b=object->Position();
            const double dx=int(a.x)-int(b.x),dy=int(a.y)-int(b.y),dz=(int(a.Level)-int(b.Level))*20;
            if (std::sqrt(dz*dz+(dx*dx+dy*dy)/256.0)>3.0) return;
        } else if (object->EntryPoints.size()>5 && object->EntryPoints[5].ActionFunction) {
            if (object->ExecuteEntryPoint(5,context,true,&caller)) return;
        }
        // A false check can move or contain its object and clear occupancy.
        // 48272f..482766 rereads penalty, numeric bounds and type, without
        // repeating the earlier room/container/flags admission.
        const auto bounds=NumericBounds(*object);
        const auto currentPenalty=object->GetValue(VMStackObjectVariable::RoutePenalty);
        const auto type=object->Object->OBJ->ObjectType;
        const auto mask=type==OBJDType::Portal
            ? TS1ObjectWallMask(object->GetValue(VMStackObjectVariable::WallBlockFlags),Heading(*object)) : 0;
        // Geometry and portal-side precedence are shared with the pure source
        // contract. Source tuning006c296c=6 gives a clearance of three units.
        TS1RouteRect obstacle;
        if (TS1MakeRouteObjectObstacle(bounds,int(type),currentPenalty,uint8_t(mask),callerBounds,3,obstacle))
            result.push_back(obstacle);
    };
    for (size_t cursor=0;cursor<vm.TS1ObjectManagerOrder.size();) {
        auto* object=vm.TS1ObjectManagerOrder[cursor];
        collect(object);
        // Source482911 reads the current object's live successor. A check may
        // prepend new objects, which must neither be revisited nor shift the
        // iterator past the original successor. The normal path stays linear.
        if (cursor>=vm.TS1ObjectManagerOrder.size() || vm.TS1ObjectManagerOrder[cursor]!=object) {
            const auto found=std::find(vm.TS1ObjectManagerOrder.begin(),vm.TS1ObjectManagerOrder.end(),object);
            if (found==vm.TS1ObjectManagerOrder.end())
                throw std::runtime_error("Original routing lost its active object-manager entry");
            cursor=size_t(found-vm.TS1ObjectManagerOrder.begin());
        }
        ++cursor;
    }
    return result;
}
TS1RouteSceneInput TS1CollectRouteRoomScene(VMEntity& caller, bool optionalPhase,
                                           bool terrainOverride) {
    auto& context=*caller.Thread->Context;
    const auto& arch=*context.Architecture();
    const int room=context.GetEnvironmentRoomAt(caller.Position());
    const int level=caller.Position().Level;
    const auto count=size_t(arch.Width)*arch.Height;
    if (arch.Width!=arch.Height || room<=0 || size_t(room)>=context.EnvironmentRooms.size() ||
        level<1 || size_t(level)>arch.TS1TileFlags.size() ||
        arch.TS1TileFlags[level-1].size()!=count || arch.TS1WaterTiles.size()!=count ||
        arch.TS1FloorPresent[level-1].size()!=count)
        throw std::runtime_error("Original routing lacks authoritative tile layers");
    TS1RouteSceneInput scene;
    scene.WorldSize=arch.Width;scene.RequestedRoom=int16_t(room);scene.Story=int8_t(level);
    scene.OptionalPhaseRequested=optionalPhase;scene.TerrainOverride=terrainOverride;
    scene.WorldZoningKind=Engine::Utils::VMTS1Neighbourhood::IsResidentialLot(
        context.VM->GetGlobalValue(10)) ? 1 : 0;
    scene.Tiles.resize(count);
    const auto& map=context.EnvironmentMaps[level-1];
    if (map.size()!=count) throw std::runtime_error("Original routing lacks a room map");
    auto style=[&](int x,int y,bool left) {
        const auto off=size_t(y)*arch.Width+x;
        const auto& logical=left?arch.TS1WallStyleTopLeft[level-1]:arch.TS1WallStyleTopRight[level-1];
        return logical.size()==count ? logical[off] : (left?arch.Walls[level-1][off].TopLeftStyle:arch.Walls[level-1][off].TopRightStyle);
    };
    for (int y=0;y<arch.Height;++y) for (int x=0;x<arch.Width;++x) {
        const auto off=size_t(y)*arch.Width+x;
        auto& tile=scene.Tiles[off];
        const auto low=uint16_t(map[off]),high=uint16_t(map[off]>>16);
        tile.Room=x>0 && y>0 && x<arch.Width-1 && y<arch.Height-1 && low && low==high
            ? int16_t(low) : int16_t(-5);
        tile.FloorWord=arch.TS1FloorPresent[level-1][off];
        tile.GroundByte=arch.TS1WaterTiles[off];tile.TerrainFlags=arch.TS1TileFlags[level-1][off];
        tile.WallBits=uint8_t(arch.Walls[level-1][off].Segments)&0x33;
        if (y+1<arch.Height && (uint8_t(arch.Walls[level-1][off+arch.Width].Segments)&2)) tile.WallBits|=8;
        if (x+1<arch.Width && (uint8_t(arch.Walls[level-1][off+1].Segments)&1)) tile.WallBits|=4;
        if (y+1<arch.Height) tile.WallStyle8=int16_t(style(x,y+1,false));
        if (x+1<arch.Width) tile.WallStyle4=int16_t(style(x+1,y,true));
    }
    const auto& visits=context.EnvironmentRooms[room].SpatialTileOrder;
    bool matched=false;
    for (const auto off : visits) if (off>=0 && size_t(off)<count && scene.Tiles[off].Room==room) {
        scene.RoomRouteFlag72=(scene.Tiles[off].TerrainFlags&4)!=0;matched=true;break;
    }
    if (!matched) scene.RoomRouteFlag72=visits.size()==1;
    for (auto* entity : context.VM->Entities) {
        if (!entity || entity->Dead || entity->GhostImage) continue;
        const auto pos=entity->Position();
        if (pos.Level!=level || pos.TileX()<0 || pos.TileY()<0 ||
            pos.TileX()>=arch.Width || pos.TileY()>=arch.Height) continue;
        auto& tile=scene.Tiles[size_t(pos.TileY())*arch.Width+pos.TileX()];
        tile.HasObjectFlag1000 |= (uint16_t(entity->GetValue(VMStackObjectVariable::Flags))&0x1000)!=0;
        if (!entity->Object || !entity->Object->OBJ || entity->Object->OBJ->ObjectType!=OBJDType::Portal ||
            entity->EntryPoints.size()<=15 || !entity->EntryPoints[15].ActionFunction || !entity->MultitileGroup) continue;
        for (auto* other : entity->MultitileGroup->Objects)
            if (other && other!=entity && !other->Dead && context.GetEnvironmentRoomAt(other->Position())==room)
                tile.PortalToRequestedRoom=true;
    }
    return scene;
}
int32_t TS1RouteRoomObjectCount(VMEntity& caller) {
    const auto& context=*caller.Thread->Context;
    const auto room=context.GetEnvironmentRoomAt(caller.Position());
    if (room<=0 || size_t(room)>=context.EnvironmentRooms.size())
        throw std::runtime_error("Original routing lacks a room record");
    return context.EnvironmentRooms[room].SpatialObjects;
}
}}}}
