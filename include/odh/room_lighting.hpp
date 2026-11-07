// SPDX-License-Identifier: MPL-2.0
// Native adaptation of FreeSO7e76 VMContext.RefreshLighting and RoomLighting.
#pragma once
#include "odh/object_position_registry.hpp"
#include <array>
#include <map>
namespace odh {
struct RoomLightingBounds {std::int32_t x=0,y=0,width=0,height=0;};
struct RoomLightSource {
    float x=0,y=0,size=160,intensity=1,weight=1,height=2.2125f;
    std::uint16_t room=0;
    std::uint8_t level=0;
    bool outdoors_color=false;
    std::array<std::uint8_t,4> color{{255,255,255,255}};
    std::optional<std::uint16_t> window_room;
    std::optional<ObjectPositionIdentity> component;
    RoomLightingBounds bounds; // Sixteenth-tile units, as LightData.LightBounds.
};
struct RoomLighting {
    std::uint16_t base_room=0,ambient_light=0,outside_light=0;
    std::int16_t room_score=0;
    RoomLightingBounds bounds; // Tile units, as VMRoom.Bounds.
    std::vector<RoomLightSource> lights;
    std::vector<RoomObstacleRect> object_footprints; // Sixteenth-tile units.
    std::vector<ObjectPositionIdentity> components;
};
struct RoomLightingVisualRoom {ObjectPositionIdentity owner;std::uint16_t room=0;};
struct RoomLightingSnapshot {
    std::uint32_t version=1;
    std::map<std::uint16_t,RoomLighting> by_base_room;
    std::vector<RoomLightingVisualRoom> visual_rooms; // Sorted by (id,generation), event-owned.
};
// Optional absence means a known absent visual particle component, not unknown
// object metadata. Offsets already include the source rotation/clamp and *16.
struct RoomLightParticle {float offset_x=0,offset_y=0,height=2.2125f;};
struct RoomLightingObjectState {
    std::uint8_t disabled=0;
    bool moves_often=false;
    std::array<std::uint8_t,4> color{{255,255,255,255}};
    std::optional<RoomLightParticle> particle;
};
struct RoomLightingServices {
    const DefinitionIndex* definitions=nullptr;
    std::function<std::optional<std::vector<ObjectPositionIdentity>>(ObjectPositionIdentity)> group;
    std::function<std::optional<RoomLightingObjectState>(ObjectPositionIdentity)> object_state;
};
struct RoomLightingLimits {
    std::size_t rooms=32768,entities=4096,group_members=4096;
    std::size_t lights=16384,components=16384,footprints=16384;
    std::uint64_t cluster_comparisons=16777216;
    std::size_t deferred_requests=32768;
};
struct RoomLightingReceipt {
    std::vector<std::uint16_t> processed_rooms,refreshed_bases,affected_bases;
    bool visual_commit=false;
};
void validate_room_lighting_snapshot(const RoomLightingSnapshot&,const RoomLightingLimits& = {});
void validate_room_lighting_snapshot(const RoomLightingSnapshot&,const GeometricRoomMap&,const RoomLightingLimits& = {});
// Missing/unprocessed bases and room0 have no record; callers must distinguish
// this from an actual zero-valued processed room. GetRoomScore defaults to0.
const RoomLighting* room_lighting_for_room(const RoomLightingSnapshot&,const GeometricRoomMap&,std::uint16_t) noexcept;
std::int16_t room_lighting_score(const RoomLightingSnapshot&,const GeometricRoomMap&,std::uint16_t) noexcept;
// Room0 is a valid no-op. Unknown metadata/resources throw FormatError.
std::optional<RoomLighting> recalculate_room_lighting(const World&,const GeometricRoomMap&,
    const ObjectPositionRegistrySnapshot&,std::uint16_t,const RoomLightingServices&,const RoomLightingLimits& = {},
    std::vector<std::uint16_t>* affected_bases=nullptr);
// Clears requests only after actual successful calculation. Requests appended
// during read-only service callbacks are processed in the same bounded drain.
// A failure leaves lighting unchanged and preserves the queue, including adds.
RoomLightingReceipt consume_deferred_lighting(const World&,const GeometricRoomMap&,
    ObjectPositionRegistrySnapshot&,RoomLightingSnapshot&,const RoomLightingServices&,const RoomLightingLimits& = {});
// RefreshAllLighting/RegeneratePortalInfo use one visited-base set across the
// room-ID loop. This recalculates each base once and never consumes the dirty
// queue; the final nonzero room commits even if its base was already visited.
RoomLightingReceipt refresh_all_room_lighting(const World&,const GeometricRoomMap&,
    ObjectPositionRegistrySnapshot&,RoomLightingSnapshot&,const RoomLightingServices&,const RoomLightingLimits& = {});
// RefreshRoomScore uses every registered RoomImpact (including disabled
// groups), changing only the shared score; it never consumes/enqueues lighting.
bool refresh_room_lighting_score(const World&,const GeometricRoomMap&,ObjectPositionRegistrySnapshot&,
    RoomLightingSnapshot&,std::uint16_t,const RoomLightingLimits& = {});
// VMGameObject.RefreshLight: flags2 bit10+positive51 =>65535 unless window/door;
// disabled>=LotCategoryWrong(4) =>65533. Invalid identity/data returns absence.
std::optional<std::uint16_t> room_lighting_visual_room(const World&,ObjectPositionIdentity,std::uint8_t disabled);
bool refresh_room_lighting_visual(RoomLightingSnapshot&,const World&,ObjectPositionIdentity,std::uint8_t disabled,
    const RoomLightingLimits& = {});
std::optional<std::uint16_t> room_lighting_cached_visual_room(const RoomLightingSnapshot&,ObjectPositionIdentity) noexcept;
void erase_room_lighting_visual(RoomLightingSnapshot&,ObjectPositionIdentity);
} // namespace odh
