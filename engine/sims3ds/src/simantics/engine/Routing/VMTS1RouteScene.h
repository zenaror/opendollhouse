// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/engine/Routing/VMTS1RouteGraph.h"
#include <cstdint>
#include <vector>
#include <optional>
namespace FSO { namespace SimAntics { namespace Engine { namespace Routing {
// Explicit source-layer values; the VM adapter owns their authoritative mapping.
// PortalToRequestedRoom already includes type8, nonzero entry15 action and the
// other portal half's requested-room identity. Object order is irrelevant here.
struct TS1RouteSceneTile {
    int16_t Room=0;
    uint16_t FloorWord=0;
    int8_t GroundByte=-1;
    uint8_t TerrainFlags=0,WallBits=0;
    int16_t WallStyle8=0,WallStyle4=0;
    bool HasObjectFlag1000=false,PortalToRequestedRoom=false;
};
struct TS1RouteSceneInput {
    int32_t WorldSize=0; // Square source grid. Tiles use [y*WorldSize+x].
    int16_t RequestedRoom=0;
    int8_t Story=1;
    bool OptionalPhaseRequested=true,RoomRouteFlag72=false;
    int32_t WorldZoningKind=0;
    bool TerrainOverride=false;
    std::vector<TS1RouteSceneTile> Tiles;
    uint32_t MaxRectangles=65536;
};
enum class TS1RouteSceneStatus : uint8_t { Ready,NoRoom,InvalidInput,ResourceLimit };
struct TS1RouteSceneResult {
    TS1RouteSceneStatus Status=TS1RouteSceneStatus::InvalidInput;
    std::vector<TS1RouteRect> Rectangles;
    uint32_t InflatedPrefix=0;
    int32_t MinX=0,MinY=0,MaxX=0,MaxY=0;
};
bool TS1RouteSceneTileAdmitted(const TS1RouteSceneTile&,int16_t requestedRoom,int8_t story);
TS1RouteSceneResult BuildTS1RouteScene(const TS1RouteSceneInput&);
struct TS1RouteObjectObstacleInput {
    TS1RouteRect Bounds,CallerBounds;
    int16_t ObjectType=0,RoutePenalty=0;
    uint8_t PortalMask=0;
    int32_t Margin=3;
};
// Admission and object traversal order belong to the VM adapter. Portal bits
// overwrite the same rectangle in source order1/4/10/40, so the last bit wins.
std::optional<TS1RouteRect> BuildTS1RouteObjectObstacle(const TS1RouteObjectObstacleInput&);
bool TS1MakeRouteObjectObstacle(const TS1RouteRect& bounds,int type,int16_t penalty,
    uint8_t mask,const TS1RouteRect& callerBounds,int margin,TS1RouteRect& result);
}}}}
