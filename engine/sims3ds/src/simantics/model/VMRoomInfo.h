/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// FSO.SimAntics.Model.VMRoomInfo / VMRoom / VMRoomPortal
#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <unordered_map>
#include "core/math.h"
#include "common/TS1/TS1Environment.h"
#include "world/model/RoomLighting.h"
#include "simantics/model/Routing/VMObstacle.h"
namespace FSO { namespace SimAntics {
class VMEntity;
namespace Model {

struct VMRoom {
    uint16_t RoomID = 0;
    uint16_t LightBaseRoom = 0;
    bool IsOutside = false;
    uint16_t Area = 0;
    bool IsPool = false;
    bool Unroutable = false;
    std::vector<Routing::VMObstacle> WallObs;
    std::vector<Routing::VMObstacle> RoomObs;
    st::Rectangle Bounds;
};

class VMRoomPortal {
public:
    int16_t ObjectID = 0;
    uint16_t TargetRoom = 0;
    VMRoomPortal() {}
    VMRoomPortal(int16_t obj, uint16_t target) : ObjectID(obj), TargetRoom(target) {}
    bool operator==(const VMRoomPortal& c2) const { return ObjectID == c2.ObjectID && TargetRoom == c2.TargetRoom; }
    bool operator!=(const VMRoomPortal& c2) const { return !(*this == c2); }
    int32_t GetHashCode() const { return ObjectID + TargetRoom; }
};

// Original environment partitions are independent of visual/routing rooms.
struct VMEnvironmentRoom {
    FSO::Common::TS1::EnvironmentInputs Environment;
    // Original flood-fill can visit a tile more than once. Preserve multiplicity.
    std::unordered_map<int32_t, uint32_t> EnvironmentTileVisits;
    // Original collector order, including duplicate coordinates. The score's
    // unordered multiplicity cache cannot drive the stateful two-source grid.
    std::vector<int32_t> SpatialTileOrder;
    // Last room-manager collection, distinct from lazy score/render counts.
    // The next comparison evaluates these with the CURRENT Overhead flag.
    float SpatialLightFactor = 0;
    int SpatialPeople = 0, SpatialLamps = 0;
    bool SpatialPool = false;
    float Score = 0, LightFactor = 0;
    bool Outside = false;
    uint16_t CanonicalID = 0; // original record ID, independent of the global vector index
    uint8_t Story = 0;
    bool Pool = false;
    int VisualPeople = -1, VisualLamps = -1;
    float VisualInputLight = -1;
    uint8_t VisualMapByte = 0;
    int VisualEventByte = -1;
    uint64_t VisualEventScoreVersion = 0;
};

struct VMRoomInfo {
    std::vector<VMRoomPortal> Portals;
    std::vector<VMEntity*> Entities;
    FSO::LotView::Model::RoomLighting Light;
    VMRoom Room;

};

}}}
namespace std {
template<> struct hash<FSO::SimAntics::Model::VMRoomPortal> {
    size_t operator()(const FSO::SimAntics::Model::VMRoomPortal& p) const { return (size_t)p.GetHashCode(); }
};
}
