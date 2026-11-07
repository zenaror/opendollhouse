// SPDX-License-Identifier: MPL-2.0
// FreeSO VMContext.Register/UnregisterObjectPos and Add/Remove*Portal,
// revision 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once
#include "odh/object_position_valid.hpp"
#include "odh/prepared_architecture.hpp"
#include "odh/room_map.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/room_info_obstacles.hpp"
#include "odh/definition_index.hpp"

namespace odh {
struct ObjectPositionRegistryFootprint {
    ObjectPositionIdentity owner;
    CollisionBox box{};
    std::uint64_t instance=0; // Source obstacle object identity, distinct from Parent.
};
struct ObjectPositionRegistryFootprintBinding {
    ObjectPositionIdentity owner;
    bool present=false;
    std::optional<std::uint16_t> room; // Actual Footprint.Set/Dynamic binding.
    CollisionBox box{};
    std::uint64_t instance=0;
};
struct ObjectPositionRegistryPortal {
    ObjectPositionIdentity owner;
    std::uint16_t target_room=0;
};
struct ObjectPositionRegistryRoom {
    std::uint16_t id=0;
    std::vector<ObjectPositionIdentity> entities; // Source ID-sorted unique list.
    std::vector<ObjectPositionRegistryFootprint> static_obstacles,dynamic_obstacles;
    std::vector<ObjectPositionRegistryPortal> portals,window_portals; // Source insertion order.
    std::array<std::vector<RoomObstacleRect>,2> boundaries;
    std::array<bool,2> boundaries_known{{false,false}};
    std::optional<std::int16_t> room_score;
};
struct ObjectPositionRegistryTile {
    std::int32_t offset=-1; // Source flattened XYZ; -1 is OUT_OF_WORLD.
    std::vector<ObjectPositionIdentity> entities;
};
struct ObjectPositionRegistryMotion {ObjectPositionIdentity owner;bool moved_self=false;};
struct ObjectPositionRegistrySnapshot {
    std::uint32_t width=0,height=0;
    bool complete=false;
    std::vector<ObjectPositionRegistryRoom> rooms;
    std::vector<ObjectPositionRegistryTile> tiles; // Sorted offset, sorted unique IDs.
    std::vector<std::uint16_t> deferred_lighting; // Source HashSet insertion order.
    std::vector<std::uint16_t> score_dirty; // Source immediate score updates available in rooms.
    std::vector<ObjectPositionRegistryMotion> motion; // Known source MovedSelf, sorted owner ID.
    std::uint64_t next_footprint_instance=0;
    std::vector<ObjectPositionRegistryFootprintBinding> footprints; // Sorted owner ID.
    // Actual deletion/reuse provenance for opaque historical references.
    // Lexicographically sorted (id,generation); never query authority and only
    // permitted while this registry is explicitly incomplete.
    std::vector<ObjectPositionIdentity> retired_owners;
};
struct ObjectPositionRegistryServices {
    const DefinitionIndex* definitions=nullptr;
    // Actual physical group order; empty means known absent group, null unknown.
    std::function<std::optional<std::vector<ObjectPositionIdentity>>(ObjectPositionIdentity)> group;
    // Actual immutable SLOT type3 count (known absent is zero) and source
    // VMEntity.MovedSelf history. Avatar/container recursion is handled here.
    std::function<std::optional<std::size_t>(ObjectPositionIdentity)> routing_slot_count;
    std::function<std::optional<bool>(ObjectPositionIdentity)> moved_self;
    // Source Container pointer survives numeric retirement. Only this narrow
    // read-only property may resolve a retained parent; the provider must prove
    // its exact active Delete lease and real resource/MovedSelf evidence.
    std::function<std::optional<bool>(ObjectPositionIdentity)> retained_moves_often;
};
void validate_object_position_registry_snapshot(const ObjectPositionRegistrySnapshot&);
// Read-only fact for a real, complete event registry after source Pre reset
// skipped Post for an old OOW body. Does not register it or alter metadata.
// Caller owns/validates the actual registry and exact live Entity. Requires
// known native OOW scalar, unique exact unbound Footprint+MovedSelf metadata,
// and absence by NUMERIC ID from every active query/obstacle/portal collection.
// A present-but-unbound Footprint and MovedSelf=true are retained verbatim.
bool object_position_known_absent_oow(const ObjectPositionRegistrySnapshot&,const Entity&,std::string& diagnostic);
std::optional<bool> known_object_moved_self(const ObjectPositionRegistrySnapshot&,ObjectPositionIdentity);
bool record_object_moved_self(ObjectPositionRegistrySnapshot&,const World&,ObjectPositionIdentity,bool,
    const std::function<bool()>&);
bool record_object_footprint_absent(ObjectPositionRegistrySnapshot&,const World&,ObjectPositionIdentity,
    const std::function<bool()>&);
// Platform-independent event-owned RoomInfo, tile queries and deferred lighting.
// World/geometry/providers must outlive this instance. New construction starts
// an incomplete source registry; only a complete actual load may certify it.
class ObjectPositionRegistry {
public:
    ObjectPositionRegistry(World&,const PreparedArchitecture&,const GeometricRoomMap&,ObjectPositionRegistryServices);
    ObjectPositionRegistry(World&,const PreparedArchitecture&,const GeometricRoomMap&,ObjectPositionRegistryServices,
        ObjectPositionRegistrySnapshot);
    bool register_position(ObjectPositionIdentity,std::uint16_t actual_room,bool room_change,const std::function<bool()>&);
    bool unregister_position(ObjectPositionIdentity,bool room_change,const std::function<bool()>&);
    bool finish_initial_load(const std::vector<ObjectPositionIdentity>& actual_source_entities,const std::function<bool()>&);
    bool record_moved_self(ObjectPositionIdentity,bool value,const std::function<bool()>&);
    // Refresh can enqueue during an initial-load position callback. It changes
    // only the source's ordered dirty-room set, never spatial registrations.
    bool defer_lighting(std::uint16_t room,const std::function<bool()>&);
    std::optional<bool> query_moves_often(ObjectPositionIdentity,const std::function<bool()>&)const;
    bool static_footprint_changed(ObjectPositionIdentity,bool new_static,const std::function<bool()>&);
    bool update_footprint(ObjectPositionIdentity,const std::function<bool()>&);
    const ObjectPositionRegistrySnapshot& snapshot() const noexcept{return state_;}
    RoomInfoObstacleQuery query(std::uint16_t,const CollisionBox&,std::int32_t target_id) const;
private:
    World& world_;const PreparedArchitecture& architecture_;const GeometricRoomMap& rooms_;
    ObjectPositionRegistryServices services_;ObjectPositionRegistrySnapshot state_;bool executing_=false;
    const Entity* entity(ObjectPositionIdentity,const std::function<bool()>&)const;
    std::optional<bool> moves_often(ObjectPositionIdentity,const std::function<bool()>&,std::vector<std::int16_t>&)const;
    bool apply(ObjectPositionIdentity,std::optional<std::uint16_t>,bool,bool,const std::function<bool()>&);
    bool regenerate_boundary(std::uint16_t,std::int16_t,const std::function<bool()>&);
    bool refresh_score(std::uint16_t,const std::function<bool()>&);
    std::optional<CollisionShapeResult> footprint_shape(ObjectPositionIdentity,const std::function<bool()>&)const;
};
}
