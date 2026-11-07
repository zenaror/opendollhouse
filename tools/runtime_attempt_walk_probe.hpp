#pragma once

#include "odh/world_attempt_walk.hpp"
#include "odh/rect_router.hpp"
#include "saved_architecture_resources.hpp"
#include "saved_wall_appearance.hpp"
#include <set>
#include <vector>

// Complete architectural input only. This never enumerates entity flags or
// executes EP5; production walking adds the live ordered entity checks later.
struct RuntimeAttemptWalkArchitecture {
    bool complete=false;
    odh::GeometricRoomMap rooms;
    odh::RoomObstacleCandidateSet room_obstacles;
    odh::WallObstacleCandidateSet wall_obstacles;
    std::vector<odh::RectRouteObstacle> architectural_and_bounds;
    std::size_t architectural_count=0;
    bool portal_inventory_complete=false;
    std::size_t portal_openings=0;
    std::vector<odh::RoomPortalEdge> portal_edges;
    std::set<std::int16_t> stair_lower_portal_ids,stair_upper_portal_ids;
};
RuntimeAttemptWalkArchitecture build_runtime_attempt_walk_architecture(
    const odh::SavedWorld&,const odh::World&,const odh::DefinitionIndex&,
    const SavedArchitectureResource&,const odh::PreparedArchitecture&,
    std::uint16_t room,std::uint8_t level);

struct RuntimeAttemptWalkProbe {
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
