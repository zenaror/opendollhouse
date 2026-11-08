#pragma once

#include "odh/world_attempt_walk.hpp"
#include "odh/room_obstacles.hpp"
#include "odh/rect_router.hpp"
#include "saved_architecture_resources.hpp"
#include "saved_wall_appearance.hpp"
#include <set>
#include <vector>

// Typed refusal provenance; unknown inventory never means an empty graph.
enum class RuntimeGeometryFailure : std::uint8_t {
 none, input, room_identity, saved_architecture, live_architectural_identity,
 live_entity_inventory, new_architectural_entity, saved_group_identity,
 portal_resource, stair_group_identity, portal_member_identity,
 portal_inventory, local_wall_geometry, local_obstacle_geometry
};
const char* runtime_geometry_failure_name(RuntimeGeometryFailure) noexcept;
struct RuntimeGeometryDiagnostic {
 RuntimeGeometryFailure failure=RuntimeGeometryFailure::none;
 std::int16_t entity_id=0;
 std::optional<odh::WorldEntityReference> entity;
 std::string detail;
};
struct RuntimeRoomPortalGraph {
 bool complete=false;
 odh::GeometricRoomMap rooms;
 odh::RoomPortalInventory inventory{false};
 RuntimeGeometryDiagnostic diagnostic;
};
// DoRoomRoute's authenticated graph phase. No AttemptWalk/EP5 resolver is
// accepted: local collision/footprint admission belongs to the later walk.
RuntimeRoomPortalGraph build_runtime_room_portal_graph(
 const odh::SavedWorld&,const odh::World&,const odh::DefinitionIndex&,
 const SavedArchitectureResource&,const odh::PreparedArchitecture&,
 std::uint16_t room,std::uint8_t level);

// Complete architectural input only. This validates live architectural flags
// and poses but never executes EP5; walking adds live collision checks later.
struct RuntimeAttemptWalkArchitecture {
    RuntimeGeometryDiagnostic diagnostic;
    bool complete=false;
    odh::GeometricRoomMap rooms;
    odh::RoomObstacleCandidateSet room_obstacles;
    odh::WallObstacleCandidateSet wall_obstacles;
    std::vector<odh::RectRouteObstacle> architectural_and_bounds;
    std::size_t architectural_count=0;
    bool portal_inventory_complete=false;
    std::size_t portal_openings=0;
    std::vector<odh::RoomPortalEdge> portal_edges;
    odh::RoomPortalInventory portal_inventory{false};
    std::set<std::int16_t> stair_lower_portal_ids,stair_upper_portal_ids;
};
RuntimeAttemptWalkArchitecture build_runtime_attempt_walk_architecture(
    const odh::SavedWorld&,const odh::World&,const odh::DefinitionIndex&,
    const SavedArchitectureResource&,const odh::PreparedArchitecture&,
    std::uint16_t room,std::uint8_t level);

struct RuntimeAttemptWalkProbe {
    RuntimeGeometryDiagnostic architecture_diagnostic;
    odh::AttemptWalkObstacleSnapshot snapshot;
    bool architecture_complete = false;
    bool portal_inventory_complete = false;
    bool avatar_consideration_complete = false;
    std::size_t other_avatars_in_room = 0;
    std::size_t roughbalanced_architecture_rectangles = 0;
    bool entity_obstacle_order_matches_reference = false;
    std::size_t portal_openings = 0;
    std::vector<odh::RoomPortalEdge> portal_edges;
    std::size_t room_obstacle_rectangles = 0;
    std::size_t wall_obstacle_rectangles = 0;
    std::set<std::int16_t> stair_lower_portal_ids;
    std::set<std::int16_t> stair_upper_portal_ids;
};

RuntimeAttemptWalkProbe build_runtime_attempt_walk_probe(
    const odh::SavedWorld&, const odh::World&, const odh::DefinitionIndex&,
    const SavedArchitectureResource&, const odh::PreparedArchitecture&,
    std::uint16_t room, std::uint8_t level, std::int16_t actor_id,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const odh::AttemptWalkEntryPoint5Resolver&,
    std::optional<odh::EntityPosition> actor_position_override = {});
