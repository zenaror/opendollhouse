#pragma once

#include "odh/world_attempt_walk.hpp"
#include "saved_architecture_resources.hpp"
#include "saved_wall_appearance.hpp"

struct RuntimeAttemptWalkProbe {
    odh::AttemptWalkObstacleSnapshot snapshot;
    bool architecture_complete = false;
    bool portal_inventory_complete = false;
    bool avatar_consideration_complete = false;
    std::size_t other_avatars_in_room = 0;
    std::size_t roughbalanced_architecture_rectangles = 0;
    bool entity_order_matches_reference = false;
    std::size_t portal_openings = 0;
    std::size_t room_obstacle_rectangles = 0;
    std::size_t wall_obstacle_rectangles = 0;
};

RuntimeAttemptWalkProbe build_runtime_attempt_walk_probe(
    const odh::SavedWorld&, const odh::World&, const odh::DefinitionIndex&,
    const SavedArchitectureResource&, const odh::PreparedArchitecture&,
    std::uint16_t room, std::uint8_t level, std::int16_t actor_id,
    bool avatar_consideration_complete,
    const std::vector<std::int16_t>& avatars_to_consider,
    const odh::AttemptWalkEntryPoint5Resolver&);
