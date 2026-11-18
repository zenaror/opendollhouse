// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "simantics/engine/Routing/VMTS1RouteGraph.h"
#include "simantics/engine/Routing/VMTS1RouteScene.h"
namespace FSO { namespace SimAntics {
class VMEntity;
namespace Engine { namespace Routing {
// Uses the original manager traversal, not room caches or sorted dispatch.
// Unknown ordering provenance fails explicitly instead of guessing an order.
std::vector<TS1RouteRect> TS1CollectRouteObjectObstacles(VMEntity& caller,
                                                       VMEntity* ignoredBlocker=nullptr);
TS1RouteSceneInput TS1CollectRouteRoomScene(VMEntity& caller, bool optionalPhase,
                                           bool terrainOverride);
int32_t TS1RouteRoomObjectCount(VMEntity& caller);
}}}}
