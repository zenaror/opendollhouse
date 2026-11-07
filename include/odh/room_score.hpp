#pragma once

#include "odh/room_map.hpp"
#include "odh/world.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace odh {

struct RoomScoreContribution {
    std::int32_t x = 0, y = 0; // Sixteenths of a tile, matching EntityPosition.
    std::int16_t level = 0;
    std::int16_t impact = 0; // VMStackObjectVariable.RoomImpact (ObjectData[7]).
};

struct RoomScoreUpdate {
    std::uint16_t room = 0;
    std::uint16_t light_base_room = 0;
    std::int16_t score = 0;
    bool updated = false; // Room 0 is a valid no-op in the Simitone service.
};

// Shared source arithmetic: float division, unchecked signed16 conversion,
// then INT baseline subtraction and clamp. This does not filter contributors.
std::int16_t source_room_score(std::int32_t total_impact,std::uint64_t area,bool outside) noexcept;

// Reproduces Simitone VMContext.RefreshRoomScore for a resolved geometric map.
// Returns absence when map/entity placement data cannot support a reliable
// calculation; callers must fail closed instead of substituting zero.
std::optional<RoomScoreUpdate> calculate_room_score(
    const GeometricRoomMap&, const std::vector<RoomScoreContribution>&,
    std::int32_t target_x, std::int32_t target_y, std::int16_t target_level);

// Capture positioned entities and their RoomImpact values without copying the
// World banks. UI ghosts are excluded by World::next_entity, as in Simitone.
std::vector<RoomScoreContribution> room_score_contributions(const World&);

} // namespace odh
